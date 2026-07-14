#include "PwaServer.h"

#include <utility>

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>
#include <QWebSocket>
#include <QWebSocketServer>

PwaServer::PwaServer(QObject *parent)
    : QObject(parent)
{
}

PwaServer::~PwaServer()
{
    stop();
}

void PwaServer::start(quint16 port)
{
    stop();

    m_server = new QWebSocketServer(QStringLiteral("GremblingEx PWA Server"), QWebSocketServer::NonSecureMode, this);
    if (!m_server->listen(QHostAddress::Any, port)) {
        qWarning() << "PwaServer: failed to listen on port" << port << "-" << m_server->errorString();
        m_server->deleteLater();
        m_server = nullptr;
        return;
    }
    connect(m_server, &QWebSocketServer::newConnection, this, &PwaServer::onNewConnection);

    // HTTP (Fase 16 Part 3): serves the embedded PWA on a fixed port,
    // independent of whatever port the WebSocket server ends up on - the
    // pairing URL (see RemoteControlPopup.qml) always points here.
    m_httpServer = new QTcpServer(this);
    if (!m_httpServer->listen(QHostAddress::Any, kHttpPort)) {
        qWarning() << "PwaServer: failed to listen on HTTP port" << kHttpPort << "-" << m_httpServer->errorString();
        m_httpServer->deleteLater();
        m_httpServer = nullptr;
        // Not fatal to the WebSocket side - fall through and keep it running.
    } else {
        connect(m_httpServer, &QTcpServer::newConnection, this, &PwaServer::onNewHttpConnection);
    }

    if (m_securityToken.isEmpty()) {
        // Defensive fallback - normally init()/regenerateSecurityToken()
        // already populated this before start() is ever called.
        m_securityToken = generateSecurityToken();
    }
    m_serverIp = detectLanIp();

    emit isRunningChanged();
    emit serverIpChanged();
    emit serverPortChanged();
    emit securityTokenChanged();

    qInfo() << "PwaServer: listening on" << m_serverIp << ":" << m_server->serverPort()
            << "(HTTP:" << kHttpPort << ") - PIN" << m_securityToken;
}

void PwaServer::stop()
{
    if (!m_server) {
        return;
    }

    const auto sockets = m_clients.keys();
    for (QWebSocket *socket : sockets) {
        disconnectClient(socket);
    }

    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;

    if (m_httpServer) {
        m_httpServer->close();
        m_httpServer->deleteLater();
        m_httpServer = nullptr;
    }

    // Tear down whatever HTTP socket is still mid-request right now
    // immediately, rather than leaving it for m_httpServer's own deferred
    // deleteLater() (and the parent-child cascade that eventually follows)
    // to close out later - abort() is safe to call on one that's already
    // finished its one-shot response and is simply waiting on its own
    // disconnected->deleteLater() (QPointer reads null in that case, so
    // this loop just skips it).
    for (const QPointer<QTcpSocket> &socketPtr : std::as_const(m_httpSockets)) {
        if (socketPtr) {
            socketPtr->abort();
        }
    }
    m_httpSockets.clear();

    emit isRunningChanged();
}

bool PwaServer::isRunning() const
{
    return m_server != nullptr && m_server->isListening();
}

void PwaServer::init()
{
    QSettings settings;
    m_serverEnabled = settings.value(QStringLiteral("pwa/serverEnabled"), true).toBool();

    // 6 digits, matching generateSecurityToken()'s own format - anything
    // else (missing key, corrupted value, a hand-edited settings file) is
    // treated as "no valid token yet" rather than trusted as-is.
    static const QRegularExpression kTokenPattern(QStringLiteral("^\\d{6}$"));
    const QString storedToken = settings.value(QStringLiteral("pwa/securityToken")).toString();
    if (kTokenPattern.match(storedToken).hasMatch()) {
        m_securityToken = storedToken;
    } else {
        m_securityToken = generateSecurityToken();
        settings.setValue(QStringLiteral("pwa/securityToken"), m_securityToken);
    }

    if (m_serverEnabled) {
        start(kDefaultPort);
    }
}

bool PwaServer::serverEnabled() const
{
    return m_serverEnabled;
}

void PwaServer::setServerEnabled(bool enabled)
{
    if (m_serverEnabled == enabled) {
        return;
    }
    m_serverEnabled = enabled;
    QSettings().setValue(QStringLiteral("pwa/serverEnabled"), enabled);

    if (enabled) {
        start(kDefaultPort);
    } else {
        stop();
    }

    emit serverEnabledChanged();
}

void PwaServer::regenerateSecurityToken()
{
    m_securityToken = generateSecurityToken();
    QSettings().setValue(QStringLiteral("pwa/securityToken"), m_securityToken);
    emit securityTokenChanged();

    // Kick every currently-connected client - including one already
    // authenticated under the old PIN - by cycling the server, rather than
    // leaving them connected under a token nobody else can use to
    // reconnect. Capture the port before stop() clears m_server.
    if (isRunning()) {
        const quint16 port = m_server->serverPort();
        stop();
        start(port);
    }
}

QString PwaServer::serverIp() const
{
    return m_serverIp;
}

quint16 PwaServer::serverPort() const
{
    return m_server ? m_server->serverPort() : 0;
}

QString PwaServer::securityToken() const
{
    return m_securityToken;
}

void PwaServer::broadcastTelemetry(const QString &button, bool state)
{
    if (m_clients.isEmpty()) {
        return;
    }

    const QJsonObject payload{
        {QStringLiteral("action"), QStringLiteral("telemetry")},
        {QStringLiteral("button"), button},
        {QStringLiteral("state"), state},
    };
    const QString message = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));

    for (auto it = m_clients.constBegin(); it != m_clients.constEnd(); ++it) {
        if (it.value().authenticated) {
            it.key()->sendTextMessage(message);
        }
    }
}

void PwaServer::onNewConnection()
{
    QWebSocket *socket = m_server->nextPendingConnection();
    if (!socket) {
        return;
    }

    connect(socket, &QWebSocket::textMessageReceived, this, &PwaServer::onTextMessageReceived);
    connect(socket, &QWebSocket::disconnected, this, &PwaServer::onSocketDisconnected);

    ClientState state;
    state.authTimeoutTimer = new QTimer(socket);
    state.authTimeoutTimer->setSingleShot(true);
    connect(state.authTimeoutTimer, &QTimer::timeout, this, [this, socket]() {
        if (!m_clients.value(socket).authenticated) {
            qInfo() << "PwaServer: closing unauthenticated connection (timeout)";
            disconnectClient(socket);
        }
    });
    state.authTimeoutTimer->start(kAuthTimeoutMs);

    m_clients.insert(socket, state);

    qInfo() << "PwaServer: new connection from" << socket->peerAddress().toString() << "- awaiting auth";
}

void PwaServer::onTextMessageReceived(const QString &message)
{
    auto *socket = qobject_cast<QWebSocket *>(sender());
    if (!socket || !m_clients.contains(socket)) {
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject json = doc.object();
    const QString action = json.value(QStringLiteral("action")).toString();

    if (action == QStringLiteral("auth")) {
        handleAuthMessage(socket, json);
        return;
    }

    if (!m_clients.value(socket).authenticated) {
        // Anything before a successful auth is ignored outright - only the
        // auth timeout timer is allowed to close this socket for it.
        return;
    }

    if (action == QStringLiteral("buttonPress")) {
        handleButtonMessage(socket, json);
    } else if (action == QStringLiteral("setLayout")) {
        handleSetLayoutMessage(json);
    } else if (action == QStringLiteral("getLayout")) {
        handleGetLayoutMessage(socket, json);
    } else if (action == QStringLiteral("listDevices")) {
        handleListDevicesMessage(socket);
    }
}

void PwaServer::handleAuthMessage(QWebSocket *socket, const QJsonObject &json)
{
    ClientState state = m_clients.value(socket);
    if (state.authenticated) {
        return;
    }

    const QString token = json.value(QStringLiteral("token")).toString();
    if (token != m_securityToken) {
        qWarning() << "PwaServer: rejected auth attempt from" << socket->peerAddress().toString();
        return; // Leave the auth timeout to close it - lets a mistyped PIN be retried.
    }

    state.authenticated = true;
    state.deviceId = json.value(QStringLiteral("deviceId")).toString();
    if (state.deviceId.isEmpty()) {
        state.deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    state.deviceName = json.value(QStringLiteral("deviceName")).toString();
    if (state.deviceName.isEmpty()) {
        state.deviceName = QStringLiteral("WebPad-") + state.deviceId.left(4);
    }
    // Fase 16 Addendum: the client's own viewport size, so the Desktop
    // Editor can size its canvas to this exact device - see
    // handleListDevicesMessage(). Absent on older PWA builds; toInt(0)
    // leaves screenWidth/screenHeight at 0 in that case.
    state.screenWidth = json.value(QStringLiteral("screenWidth")).toInt(0);
    state.screenHeight = json.value(QStringLiteral("screenHeight")).toInt(0);
    if (state.authTimeoutTimer) {
        state.authTimeoutTimer->stop();
    }
    m_clients.insert(socket, state);

    const QJsonObject response{
        {QStringLiteral("action"), QStringLiteral("authResult")},
        {QStringLiteral("success"), true},
        {QStringLiteral("deviceId"), state.deviceId},
    };
    socket->sendTextMessage(QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact)));

    qInfo() << "PwaServer: client authenticated, deviceId" << state.deviceId << "deviceName" << state.deviceName;
    emit clientConnected(state.deviceId, state.deviceName);

    // Fase 16: hands the client back whatever layout it saved last time
    // (via setLayout) the instant it reconnects, so a paired tablet re-opens
    // showing its own custom panel layout instead of the built-in default.
    sendLayoutIfExists(socket, state.deviceName);
}

void PwaServer::handleButtonMessage(QWebSocket *socket, const QJsonObject &json)
{
    if (!json.contains(QStringLiteral("button"))) {
        return;
    }
    // The PWA's grid is 1-based for the user (button labels start at "1"),
    // but DeviceManager/EventRouter index buttons 0-based like real hardware
    // - subtract 1 here so PWA button 1 lands on index 0, matching the
    // Device Tester's Button 1 instead of showing up one slot over.
    const int buttonId = json.value(QStringLiteral("button")).toInt(0) - 1;
    if (buttonId < 0) {
        return;
    }
    const bool pressed = json.value(QStringLiteral("state")).toBool();

    // Routed by deviceName (Fase 17), not deviceId: EventRouter::
    // pwaSystemPath() keys the routing table off the user-chosen name, set
    // once at auth time (see handleAuthMessage()), so it stays stable across
    // a reconnect/new-device cache wipe the way a random deviceId can't.
    emit remoteInputReceived(m_clients.value(socket).deviceName, buttonId, pressed);
}

void PwaServer::handleSetLayoutMessage(const QJsonObject &json)
{
    const QString deviceName = json.value(QStringLiteral("deviceName")).toString();
    if (deviceName.isEmpty() || !json.contains(QStringLiteral("layout"))) {
        return;
    }

    QJsonObject layouts = loadLayouts();
    layouts[deviceName] = json.value(QStringLiteral("layout"));
    saveLayouts(layouts);

    qInfo() << "PwaServer: saved PWA layout for" << deviceName;
}

void PwaServer::handleGetLayoutMessage(QWebSocket *socket, const QJsonObject &json)
{
    const QString targetDevice = json.value(QStringLiteral("targetDevice")).toString();
    if (targetDevice.isEmpty()) {
        return;
    }
    sendLayoutIfExists(socket, targetDevice);
}

void PwaServer::handleListDevicesMessage(QWebSocket *socket)
{
    const QJsonObject layouts = loadLayouts();

    // Fase 16 Addendum: live viewport size per currently-connected device
    // (see handleAuthMessage()), so the Desktop Editor can size its canvas
    // to the exact device it's about to edit. A device with a saved layout
    // that isn't connected right now still gets listed - just without
    // dimensions until it reconnects.
    QHash<QString, std::pair<int, int>> liveDimensions;
    for (auto it = m_clients.constBegin(); it != m_clients.constEnd(); ++it) {
        const ClientState &state = it.value();
        if (state.authenticated && state.screenWidth > 0 && state.screenHeight > 0) {
            liveDimensions.insert(state.deviceName, {state.screenWidth, state.screenHeight});
        }
    }

    QJsonArray devices;
    for (auto it = layouts.constBegin(); it != layouts.constEnd(); ++it) {
        QJsonObject device{{QStringLiteral("name"), it.key()}};
        const auto dimsIt = liveDimensions.constFind(it.key());
        if (dimsIt != liveDimensions.constEnd()) {
            device[QStringLiteral("width")] = dimsIt->first;
            device[QStringLiteral("height")] = dimsIt->second;
        }
        devices.append(device);
    }

    const QJsonObject response{
        {QStringLiteral("action"), QStringLiteral("deviceList")},
        {QStringLiteral("devices"), devices},
    };
    socket->sendTextMessage(QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact)));
}

void PwaServer::sendLayoutIfExists(QWebSocket *socket, const QString &deviceName)
{
    if (deviceName.isEmpty()) {
        return;
    }

    const QJsonObject layouts = loadLayouts();
    if (!layouts.contains(deviceName)) {
        return;
    }

    const QJsonObject response{
        {QStringLiteral("action"), QStringLiteral("layoutData")},
        {QStringLiteral("layout"), layouts.value(deviceName)},
    };
    socket->sendTextMessage(QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact)));
}

QString PwaServer::layoutsFilePath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/pwa_layouts.json");
}

QJsonObject PwaServer::loadLayouts()
{
    QFile file(layoutsFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    return doc.isObject() ? doc.object() : QJsonObject();
}

bool PwaServer::saveLayouts(const QJsonObject &layouts)
{
    QFile file(layoutsFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "PwaServer: could not write" << file.fileName() << ":" << file.errorString();
        return false;
    }
    file.write(QJsonDocument(layouts).toJson(QJsonDocument::Indented));
    return true;
}

void PwaServer::onSocketDisconnected()
{
    auto *socket = qobject_cast<QWebSocket *>(sender());
    if (!socket) {
        return;
    }
    const ClientState state = m_clients.value(socket);
    m_clients.remove(socket);
    socket->deleteLater();

    if (state.authenticated) {
        emit clientDisconnected(state.deviceId, state.deviceName);
    }
}

void PwaServer::disconnectClient(QWebSocket *socket)
{
    if (!socket) {
        return;
    }
    m_clients.remove(socket);
    socket->close();
    socket->deleteLater();
}

void PwaServer::onNewHttpConnection()
{
    while (m_httpServer->hasPendingConnections()) {
        QTcpSocket *socket = m_httpServer->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, &PwaServer::onHttpReadyRead);
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
        m_httpSockets.append(socket);
    }
}

void PwaServer::onHttpReadyRead()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) {
        return;
    }
    handleHttpRequest(socket, socket->readAll());
}

void PwaServer::handleHttpRequest(QTcpSocket *socket, const QByteArray &request)
{
    // Only the request line ("GET /path HTTP/1.1") matters - headers/body
    // (if any) are irrelevant since this server only ever serves one static
    // file and ignores everything else.
    const QByteArray requestLine = request.left(request.indexOf("\r\n"));
    const QList<QByteArray> parts = requestLine.split(' ');
    const QString rawPath = parts.size() >= 2 ? QString::fromUtf8(parts.at(1)) : QString();
    // Strip the query string (the pairing URL always has one - "?token=...",
    // see RemoteControlPopup.qml) so "/", "/index.html" and their "?..."
    // variants all resolve to the same static page.
    const QString path = rawPath.section(QLatin1Char('?'), 0, 0);

    QByteArray status;
    QByteArray contentType;
    QByteArray body;
    if (path.isEmpty() || path == QStringLiteral("/") || path == QStringLiteral("/index.html")) {
        status = "200 OK";
        contentType = "text/html; charset=utf-8";
        body = loadPwaHtml();
    } else {
        status = "404 Not Found";
        contentType = "text/plain; charset=utf-8";
        body = "Not Found";
    }

    QByteArray response = "HTTP/1.1 " + status + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

QByteArray PwaServer::loadPwaHtml()
{
    QFile file(QStringLiteral(":/pwa/index.html"));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "PwaServer: failed to load embedded index.html";
        return QByteArrayLiteral("<!doctype html><html><body>PWA not available</body></html>");
    }
    return file.readAll();
}

QString PwaServer::generateSecurityToken()
{
    return QStringLiteral("%1").arg(QRandomGenerator::global()->bounded(0, 1000000), 6, 10, QLatin1Char('0'));
}

QString PwaServer::detectLanIp()
{
    const auto addresses = QNetworkInterface::allAddresses();
    QString fallbackIp = QStringLiteral("127.0.0.1");

    for (const QHostAddress &address : addresses) {
        if (address.protocol() != QAbstractSocket::IPv4Protocol) {
            continue;
        }
        if (address.isLoopback() || address.isLinkLocal()) {
            continue;
        }
        
        QString ipStr = address.toString();
        // Priorizar rangos típicos de red local hogareña (Wi-Fi/Ethernet)
        if (ipStr.startsWith(QStringLiteral("192.168.")) || 
            ipStr.startsWith(QStringLiteral("10.")) ||
            ipStr.startsWith(QStringLiteral("172."))) {
            return ipStr;
        }
        
        // Guardar la primera IP válida encontrada por si acaso
        if (fallbackIp == QStringLiteral("127.0.0.1")) {
            fallbackIp = ipStr;
        }
    }
    return fallbackIp;
}
