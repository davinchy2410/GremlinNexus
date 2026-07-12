#pragma once

#include <QStringListModel>
#include <QtLogging>

class QTimer;

/**
 * @brief App-wide log console (Fase 14): installs itself as the sole Qt
 *        message handler and keeps the most recent lines as a
 *        QStringListModel, so a QML ListView/TextArea can show
 *        qDebug()/qInfo()/qWarning()/qCritical() output live. Actual
 *        console/file I/O is delegated to AsyncLogSink on its own thread
 *        (see that class's docs) - this model's own job is just tracking
 *        the in-app console's text.
 *
 * A singleton (like VirtualOutputManager/DeviceManager) rather than an
 * object owned by main.cpp: qInstallMessageHandler() takes a bare function
 * pointer with no user-data parameter, so the handler itself has no way to
 * reach a particular instance other than through a process-wide one.
 *
 * Thread safety: qInstallMessageHandler()'s handler can be invoked from any
 * thread that logs (e.g. DeviceManager's RawInput thread) - appendLine()
 * itself must only ever run on the model's own thread (the main/GUI thread,
 * since that's where QML's ListView reads it from), so messageHandler()
 * never touches the model directly; it goes through
 * QMetaObject::invokeMethod(..., Qt::AutoConnection), which queues the call
 * across threads automatically and calls straight through when already on
 * the right one.
 */
class LogModel : public QStringListModel
{
    Q_OBJECT

public:
    static LogModel &instance();

    Q_PROPERTY(QString allText READ getAllText NOTIFY logUpdated)
    Q_PROPERTY(QString htmlText READ getHtmlText NOTIFY logUpdated)

    /// Installs this instance as the process' Qt message handler. Safe to
    /// call more than once; later calls are ignored (same idempotent-call
    /// convention as EventRouter::start()).
    void install();

    /// Every currently-held log line joined with '\n' (Fase 20.38).
    /// Used by LogConsoleView for multi-line selection.
    Q_INVOKABLE QString getAllText() const;

    /// Formatted as HTML for multi-line colored selection in QML.
    Q_INVOKABLE QString getHtmlText() const;

    Q_INVOKABLE void clearLogs();

signals:
    void logUpdated();

private slots:
    /// Appends line as a new row, then trims from the front once past
    /// kMaxLines - a plain ring buffer, since a live console some hours
    /// into a session showing tens of thousands of rows would only get
    /// slower for no benefit (nobody scrolls back that far). Does NOT emit
    /// logUpdated() itself anymore - see m_flushTimer's docs.
    void appendLine(const QString &line);

    /// m_flushTimer's own slot - emits logUpdated() at most once per tick,
    /// only if a line actually arrived since the last one.
    void flushPendingUpdate();

private:
    explicit LogModel(QObject *parent = nullptr);

    static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message);

    static constexpr int kMaxLines = 2000;

    bool m_installed = false;

    /// Coalesces appendLine() into at most one logUpdated() emission per
    /// tick (150 ms) instead of one per log line. LogConsoleView.qml binds
    /// its TextArea directly to htmlText, an O(current line count) rebuild
    /// (escapes + wraps every line in a <font> tag, up to kMaxLines) that
    /// used to re-run on literally every single qDebug()/qInfo() call, on
    /// the main/GUI thread, for the whole app session - not just while the
    /// Log Console tab is visible (it lives inside main.qml's StackLayout,
    /// which keeps every tab's Item instantiated). Under log-heavy activity
    /// (e.g. mashing buttons on the Device Tester screen, each transition
    /// logged by EventRouter) this was almost certainly the dominant real
    /// cost behind the UI/joystick-response lag investigated this session -
    /// same shape as DeviceTesterViewModel's own m_uiThrottleTimer.
    QTimer *m_flushTimer = nullptr;
    bool m_dirty = false;
};
