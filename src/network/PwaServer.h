#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class QWebSocket;
class QWebSocketServer;
class QTcpServer;
class QTcpSocket;
class QTimer;

/**
 * @brief WebSocket + HTTP bridge between the PWA remote-control web client
 *        (tablets/phones on the same LAN) and the desktop app's EventRouter
 *        (Fase 16).
 *
 * Two servers, started together by start():
 * - A QWebSocketServer on kDefaultPort (8080) for live input. Listens for
 *   plaintext (ws://) connections and requires every client to authenticate
 *   with a random 6-digit PIN (regenerated each start()) before any input is
 *   accepted: a client that never sends a correct
 *   {"action":"auth","token":"123456"} within kAuthTimeoutMs is disconnected.
 *   Once authenticated, a client's {"action":"buttonPress","deviceId":"...",
 *   "button":N,"state":bool} messages are re-emitted as remoteInputReceived()
 *   for whoever wires that signal to EventRouter::onRemoteButtonInput() to
 *   dispatch through the exact same routing table real hardware uses.
 * - A plain QTcpServer on kHttpPort (8081, Fase 16 Part 3) that serves the
 *   PWA's single static index.html (embedded via Qt resources - see
 *   loadPwaHtml()) for any GET request, so scanning the pairing QR code (see
 *   RemoteControlPopup.qml) takes a phone straight to a working page with no
 *   separate web server to run.
 *
 * Deliberately ws:// (not wss://) with a 6-digit PIN rather than a full
 * credential/TLS scheme - scoped to a trusted home LAN for casual remote
 * control, not a hardened multi-tenant service.
 */
class PwaServer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString serverIp READ serverIp NOTIFY serverIpChanged)
    Q_PROPERTY(quint16 serverPort READ serverPort NOTIFY serverPortChanged)
    Q_PROPERTY(QString securityToken READ securityToken NOTIFY securityTokenChanged)
    Q_PROPERTY(bool isRunning READ isRunning NOTIFY isRunningChanged)
    Q_PROPERTY(bool serverEnabled READ serverEnabled WRITE setServerEnabled NOTIFY serverEnabledChanged)

public:
    static constexpr quint16 kDefaultPort = 8080;
    static constexpr quint16 kHttpPort = 8081;

    explicit PwaServer(QObject *parent = nullptr);
    ~PwaServer() override;

    /// Starts listening for both WebSocket input (port, default
    /// kDefaultPort) and HTTP (always kHttpPort) connections. Does NOT
    /// touch securityToken - that is owned entirely by init() (persisted
    /// across restarts via QSettings) and regenerateSecurityToken() (an
    /// explicit user action); a defensive fallback generates one on the
    /// spot only if m_securityToken is still empty (a direct start() call
    /// that bypassed init()). Safe to call while already running - stops
    /// first.
    void start(quint16 port = kDefaultPort);

    /// Stops listening and disconnects every currently-connected client.
    void stop();

    /// Reads persisted settings ("pwa/serverEnabled", default true;
    /// "pwa/securityToken", generating and persisting a fresh PIN on first
    /// run or if the stored value isn't a valid 6-digit token) and starts
    /// the server if enabled. Call once from main.cpp in place of an
    /// unconditional start() - the server now self-manages whether it
    /// should be running based on what the user last chose via
    /// setServerEnabled(), rather than always starting.
    void init();

    bool isRunning() const;
    QString serverIp() const;
    quint16 serverPort() const;
    QString securityToken() const;

    /// User-facing on/off intent (distinct from isRunning(): if start()
    /// ever fails to bind its port, isRunning() goes false but
    /// serverEnabled() still reflects that the user wants it on) -
    /// persisted to QSettings on every change so it survives a restart.
    bool serverEnabled() const;
    void setServerEnabled(bool enabled);

    /// Generates a fresh 6-digit PIN, persists it to QSettings, emits
    /// securityTokenChanged(), and - if currently running - cycles the
    /// server (stop() then start() on the same port) to disconnect every
    /// currently-connected client, including one that already knew the old
    /// PIN, rather than leaving them connected under a token nobody else
    /// can use to reconnect.
    Q_INVOKABLE void regenerateSecurityToken();

    /// Broadcasts {"action":"telemetry","button":button,"state":state} to
    /// every authenticated client (Fase 16) - lets a physical joystick's own
    /// vJoy button state changes (see VJoyDevice::setTelemetryCallback())
    /// light up an `indicator` control on every connected PWA in real time,
    /// independent of which client (if any) actually owns that binding.
    /// A no-op if no client is currently authenticated.
    void broadcastTelemetry(const QString &button, bool state);

signals:
    void isRunningChanged();
    void serverIpChanged();
    void serverPortChanged();
    void securityTokenChanged();
    void serverEnabledChanged();

    /// Emitted once an authenticated client's button message is parsed.
    /// deviceName identifies which web client sent it (see ClientState) -
    /// the user-chosen name (Fase 17) rather than the random deviceId, so it
    /// matches EventRouter::pwaSystemPath()'s routing key; buttonId/isPressed
    /// mirror DeviceManager::buttonPressed's own shape, so a subscriber can
    /// treat it identically to real hardware.
    void remoteInputReceived(const QString &deviceName, int buttonId, bool isPressed);

    /// Emitted right after a client's token is accepted (Fase 16.5, deviceName
    /// added Fase 17) - lets main.cpp register a synthetic DeviceInfo for
    /// deviceName in DeviceManager, so a paired tablet shows up in the
    /// Profiles screen exactly like a physical joystick, and
    /// RemoteControlPopup.qml close itself the instant pairing succeeds.
    void clientConnected(const QString &deviceId, const QString &deviceName);

    /// Emitted when a previously-authenticated client disconnects (Fase
    /// 16.5, deviceName added Fase 17) - the counterpart to clientConnected(),
    /// so main.cpp can drop that same synthetic DeviceInfo (keyed by
    /// deviceName - see EventRouter::pwaSystemPath()). Never emitted for a
    /// client that disconnects before authenticating - there is nothing
    /// registered for it to remove.
    void clientDisconnected(const QString &deviceId, const QString &deviceName);

private slots:
    void onNewConnection();
    void onTextMessageReceived(const QString &message);
    void onSocketDisconnected();

    void onNewHttpConnection();
    void onHttpReadyRead();

private:
    struct ClientState
    {
        QString deviceId;
        QString deviceName; ///< User-chosen name (Fase 17) - see handleAuthMessage().
        bool authenticated = false;
        QTimer *authTimeoutTimer = nullptr;
        /// Client's own viewport size in CSS pixels (Fase 16 Addendum), sent
        /// once at auth time - see handleAuthMessage()/handleListDevicesMessage().
        /// 0 until a client that actually reports them authenticates (older
        /// PWA builds that predate this field never will).
        int screenWidth = 0;
        int screenHeight = 0;
    };

    void handleAuthMessage(QWebSocket *socket, const QJsonObject &json);
    void handleButtonMessage(QWebSocket *socket, const QJsonObject &json);

    /// Fase 16: persists json's "layout" object into pwa_layouts.json under
    /// json's own "deviceName" key (a client saves *its own* layout - see
    /// index.html's saveLayout(), which always fills deviceName with its own
    /// identity, even the Desktop Editor session, which spoofs deviceName to
    /// "DesktopEditor"). Silently does nothing if either field is missing.
    void handleSetLayoutMessage(const QJsonObject &json);

    /// Fase 16: responds to an explicit {"action":"getLayout","targetDevice":
    /// "..."} request (the Desktop Editor asking for a specific paired
    /// device's layout, as opposed to handleAuthMessage()'s automatic
    /// self-lookup on connect) with {"action":"layoutData","layout":...} if
    /// targetDevice has a saved layout - silently does nothing otherwise.
    void handleGetLayoutMessage(QWebSocket *socket, const QJsonObject &json);

    /// Fase 16: responds to {"action":"listDevices"} with
    /// {"action":"deviceList","devices":[...]}, one entry per key already in
    /// pwa_layouts.json - lets the Desktop Editor offer a device picker
    /// without needing DeviceManager/EventRouter knowledge of what's paired.
    /// Each entry is {"name":..., "width":..., "height":...} (Fase 16
    /// Addendum) - width/height are only present for a device that is
    /// currently connected (see handleAuthMessage()'s screenWidth/
    /// screenHeight capture), so the Desktop Editor can size its canvas to
    /// that exact device.
    void handleListDevicesMessage(QWebSocket *socket);

    /// Sends json's "layout" (if any) for deviceName back to socket as
    /// {"action":"layoutData","layout":...} - shared by handleAuthMessage()
    /// (self-lookup) and handleGetLayoutMessage() (explicit targetDevice
    /// lookup) so both go through the exact same wire format.
    static void sendLayoutIfExists(QWebSocket *socket, const QString &deviceName);

    void disconnectClient(QWebSocket *socket);

    /// Where handleSetLayoutMessage()/sendLayoutIfExists()/
    /// handleListDevicesMessage() persist/read PWA panel layouts -
    /// "pwa_layouts.json" next to the running executable (same convention as
    /// AutoSwitchManager's autoswitch_rules.json), a single JSON object keyed
    /// by deviceName, one entry per paired device that has ever saved a
    /// layout.
    static QString layoutsFilePath();
    static QJsonObject loadLayouts();
    static bool saveLayouts(const QJsonObject &layouts);

    /// Parses request's GET line and writes back either the embedded PWA
    /// (for "/", "/index.html", or either with a "?..." query string - the
    /// pairing URL always has one, see RemoteControlPopup.qml) or a plain
    /// 404, then closes the connection - every response sends
    /// "Connection: close", so there is no keep-alive state to track between
    /// requests.
    void handleHttpRequest(QTcpSocket *socket, const QByteArray &request);

    static QByteArray loadPwaHtml();
    static QString generateSecurityToken();
    static QString detectLanIp();

    static constexpr int kAuthTimeoutMs = 10000;

    QWebSocketServer *m_server = nullptr;
    QTcpServer *m_httpServer = nullptr;
    QHash<QWebSocket *, ClientState> m_clients;

    /// Every HTTP socket currently accepted and not yet cleaned up (see
    /// onNewHttpConnection()) - QPointer so a socket that already went
    /// through its own disconnected->deleteLater() cycle (the normal path,
    /// once its one-shot "Connection: close" response finishes) is simply
    /// null here rather than dangling. stop() aborts and clears this
    /// directly instead of relying on m_httpServer's own deleteLater() (and
    /// the parent-child cascade that eventually brings down) to close out
    /// whatever's still mid-request.
    QList<QPointer<QTcpSocket>> m_httpSockets;
    QString m_securityToken;
    QString m_serverIp;

    /// Persisted user on/off intent - see serverEnabled()'s own docs. Set
    /// from QSettings in init(); false until init() runs.
    bool m_serverEnabled = false;
};
