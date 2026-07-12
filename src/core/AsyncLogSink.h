#pragma once

#include <QObject>
#include <QString>

class QThread;
class QFile;
class QTextStream;

/**
 * @brief Off-main-thread sink for every qDebug()/qInfo()/qWarning()/
 *        qCritical() line LogModel's message handler captures.
 *
 * Investigation this session found that EventRouter's button routing
 * (onButtonPressed()) and its 200 Hz vJoy output tick (tick()) both run on
 * the main/GUI thread - the same thread that used to do the message
 * handler's own console I/O synchronously. A slow or blocked console write
 * (or just enough log volume) on that thread delayed real button/vJoy
 * processing along with the UI, not just the UI. Every formatted line is
 * now handed to this sink via a Qt::QueuedConnection call (O(1), no I/O,
 * never blocks the calling thread) and drained here on a dedicated QThread
 * that owns the actual console/file writes.
 *
 * Also writes every line to a per-session log file under "Logs/" next to
 * the executable (Nexus.Log.<yy.MM.dd>-<NNN>.txt, NNN incrementing per
 * launch that day) so a durable capture of the whole session exists without
 * anyone needing to copy/paste the in-app console - the whole point being
 * the vJoy "stops responding after ~1h" bug this was built to help diagnose
 * takes too long to reproduce for someone to babysit a console the whole
 * time.
 *
 * Same Meyer's-singleton-with-explicit-shutdown() shape as DeviceManager
 * (see its own docs / Memory.md's "shutdown Heisenbug" entry): the worker
 * thread and its open file must be torn down deterministically from main()
 * while qApp is still alive, not via a static destructor that could run via
 * atexit() after qApp has already unwound.
 */
class AsyncLogSink : public QObject
{
    Q_OBJECT

public:
    static AsyncLogSink &instance();

    /// Starts the dedicated worker thread and, if debug logging is enabled
    /// (see isEnabled()), opens this session's log file. Safe to call more
    /// than once; later calls are ignored.
    ///
    /// Must be called after QGuiApplication/QCoreApplication has been
    /// constructed, AND after QCoreApplication::setOrganizationName()/
    /// setApplicationName() - both the file-open path (applicationDirPath())
    /// and the enabled flag (QSettings(), org/app-name-scoped) resolve to
    /// empty/wrong locations otherwise. LogModel::install() itself runs
    /// earlier than that (main() needs qInstallMessageHandler() active
    /// before DeviceManager::initialize() starts logging), so this is
    /// called separately from main(), right after `QGuiApplication app` and
    /// the org/app name calls - see main.cpp.
    void start();

    /// Stops the worker thread and closes the file, synchronously. Call
    /// explicitly from main() before qApp unwinds.
    void shutdown();

    /// Whether this session is currently writing to a Logs/*.txt file -
    /// backed by QSettings ("DebugLogging/Enabled"), so a user's choice
    /// persists across restarts. Defaults to OFF (Fase QoL - Settings
    /// toggle): file logging used to be always-on for every install, which
    /// meant an ordinary user's machine silently accumulated session logs
    /// forever (one friend's debugging session alone produced a 5.7GB file
    /// - see Memory.md) even though only a handful of people actively
    /// diagnosing a bug ever look at them. Making it an explicit opt-in from
    /// the Settings screen means nobody pays that disk cost unless they
    /// deliberately turn it on. writeLine()'s own off-GUI-thread dispatch is
    /// unaffected either way - that's what keeps qDebug()/qInfo() volume
    /// from blocking EventRouter's button routing/vJoy tick regardless of
    /// whether anything is actually being written to disk (see this class's
    /// own docs) - only the *file* write is gated by this flag.
    bool isEnabled() const { return m_enabled; }

    /// Toggles isEnabled() live (no restart needed) and persists the choice
    /// to QSettings - opens a fresh session file immediately when turned on,
    /// closes the current one immediately when turned off.
    void setEnabled(bool enabled);

signals:
    void enabledChanged(bool enabled);

public slots:
    /// Writes line to both the attached console and this session's log
    /// file. Only ever called on m_thread via the queued invokeMethod()
    /// call in LogModel::messageHandler() - never call directly from
    /// another thread.
    void writeLine(const QString &line);

private slots:
    void openSessionFile();
    void closeSessionFile();

private:
    explicit AsyncLogSink(QObject *parent = nullptr);
    ~AsyncLogSink() override;

    QThread *m_thread = nullptr;
    QFile *m_file = nullptr;
    QTextStream *m_stream = nullptr;
    bool m_started = false;
    bool m_enabled = false;
};
