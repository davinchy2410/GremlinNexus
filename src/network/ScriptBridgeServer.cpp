#include "ScriptBridgeServer.h"

#include <QDebug>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>

ScriptBridgeServer::ScriptBridgeServer(QObject *parent)
    : QObject(parent)
{
}

ScriptBridgeServer::~ScriptBridgeServer()
{
    stop();
}

bool ScriptBridgeServer::start()
{
    stop();

    m_server = new QTcpServer(this);
    // LocalHost, not AnyIPv4/Any - see this class' own docs on why this
    // socket must never be reachable from outside the machine.
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        qWarning() << "ScriptBridgeServer: failed to listen on 127.0.0.1 -" << m_server->errorString();
        m_server->deleteLater();
        m_server = nullptr;
        return false;
    }
    connect(m_server, &QTcpServer::newConnection, this, &ScriptBridgeServer::onNewConnection);

    qInfo() << "ScriptBridgeServer: listening on 127.0.0.1:" << m_server->serverPort();
    return true;
}

void ScriptBridgeServer::stop()
{
    if (!m_server) {
        return;
    }

    const auto sockets = m_clients.keys();
    for (QTcpSocket *socket : sockets) {
        disconnectClient(socket);
    }

    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
}

bool ScriptBridgeServer::isRunning() const
{
    return m_server != nullptr && m_server->isListening();
}

quint16 ScriptBridgeServer::port() const
{
    return m_server ? m_server->serverPort() : 0;
}

QString ScriptBridgeServer::registerScriptToken()
{
    // Long, random, and non-guessable is more important than "readable" -
    // unlike PwaServer's 6-digit PIN (a human types it while pairing a
    // phone), nobody ever types this; it only ever travels from
    // registerScriptToken()'s caller to the QProcess via env var.
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_validTokens.insert(token);
    return token;
}

void ScriptBridgeServer::unregisterScriptToken(const QString &token)
{
    m_validTokens.remove(token);

    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value().token == token) {
            disconnectClient(it.key());
            return; // A token maps to at most one connected client - see m_validTokens' own docs.
        }
    }
}

void ScriptBridgeServer::sendToScript(const QString &token, const QJsonObject &message)
{
    for (auto it = m_clients.constBegin(); it != m_clients.constEnd(); ++it) {
        if (it.value().authenticated && it.value().token == token) {
            QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact);
            line += '\n';
            it.key()->write(line);
            return;
        }
    }
    // Not connected right now - silently dropped, same policy VJoyDevice's
    // update() uses for a device that isn't currently owned: the caller
    // shouldn't have to track connection state itself just to push state.
}

void ScriptBridgeServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket) {
            continue;
        }

        connect(socket, &QTcpSocket::readyRead, this, &ScriptBridgeServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &ScriptBridgeServer::onSocketDisconnected);

        ClientState state;
        state.authTimeoutTimer = new QTimer(socket);
        state.authTimeoutTimer->setSingleShot(true);
        connect(state.authTimeoutTimer, &QTimer::timeout, this, [this, socket]() {
            if (!m_clients.value(socket).authenticated) {
                qInfo() << "ScriptBridgeServer: closing unauthenticated connection (timeout)";
                disconnectClient(socket);
            }
        });
        state.authTimeoutTimer->start(kAuthTimeoutMs);

        m_clients.insert(socket, state);
    }
}

void ScriptBridgeServer::onReadyRead()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket || !m_clients.contains(socket)) {
        return;
    }
    onSocketBytesReady(socket);
}

void ScriptBridgeServer::onSocketBytesReady(QTcpSocket *socket)
{
    ClientState &state = m_clients[socket];
    state.buffer += socket->readAll();

    int newlineIndex;
    while ((newlineIndex = state.buffer.indexOf('\n')) != -1) {
        const QByteArray line = state.buffer.left(newlineIndex);
        state.buffer.remove(0, newlineIndex + 1);
        handleLine(socket, line);
        if (!m_clients.contains(socket)) {
            return; // handleLine() (e.g. a bad auth attempt) may have disconnected this socket already.
        }
    }

    if (state.buffer.size() > kMaxLineBytes) {
        qWarning() << "ScriptBridgeServer: client exceeded" << kMaxLineBytes
                   << "bytes without a newline - disconnecting";
        disconnectClient(socket);
    }
}

void ScriptBridgeServer::handleLine(QTcpSocket *socket, const QByteArray &line)
{
    const QJsonDocument doc = QJsonDocument::fromJson(line);
    if (!doc.isObject()) {
        return; // Malformed line - ignored, not a disconnect offense on its own.
    }
    const QJsonObject json = doc.object();
    const QString type = json.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("auth")) {
        handleAuthMessage(socket, json);
        return;
    }

    const ClientState &state = m_clients.value(socket);
    if (!state.authenticated) {
        return; // Anything before a successful auth is ignored - only the auth timeout may close this socket for it.
    }
    emit messageReceived(state.token, json);
}

void ScriptBridgeServer::handleAuthMessage(QTcpSocket *socket, const QJsonObject &json)
{
    ClientState state = m_clients.value(socket);
    if (state.authenticated) {
        return;
    }

    const QString token = json.value(QStringLiteral("token")).toString();
    if (!m_validTokens.contains(token)) {
        qWarning() << "ScriptBridgeServer: rejected auth attempt with an unrecognized token";
        return; // Leave the auth timeout to close it.
    }

    state.authenticated = true;
    state.token = token;
    if (state.authTimeoutTimer) {
        state.authTimeoutTimer->stop();
    }
    m_clients.insert(socket, state);

    const QJsonObject response{{QStringLiteral("type"), QStringLiteral("authResult")}, {QStringLiteral("success"), true}};
    QByteArray responseLine = QJsonDocument(response).toJson(QJsonDocument::Compact);
    responseLine += '\n';
    socket->write(responseLine);

    qInfo() << "ScriptBridgeServer: script authenticated";
    emit scriptConnected(state.token);
}

void ScriptBridgeServer::onSocketDisconnected()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) {
        return;
    }
    const ClientState state = m_clients.value(socket);
    m_clients.remove(socket);
    socket->deleteLater();

    if (state.authenticated) {
        emit scriptDisconnected(state.token);
    }
}

void ScriptBridgeServer::disconnectClient(QTcpSocket *socket)
{
    if (!socket) {
        return;
    }
    const ClientState state = m_clients.value(socket);
    m_clients.remove(socket);
    socket->close();
    socket->deleteLater();

    if (state.authenticated) {
        emit scriptDisconnected(state.token);
    }
}
