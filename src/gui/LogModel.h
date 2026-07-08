#pragma once

#include <QStringListModel>
#include <QtLogging>

/**
 * @brief App-wide log console (Fase 14): installs itself as the sole Qt
 *        message handler and keeps the most recent lines as a
 *        QStringListModel, so a QML ListView/TextArea can show
 *        qDebug()/qInfo()/qWarning()/qCritical() output live instead of
 *        only being visible in whatever console window happens to be
 *        attached (see CMakeLists.txt's note on why the console subsystem
 *        is kept attached in the first place).
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
    /// slower for no benefit (nobody scrolls back that far).
    void appendLine(const QString &line);

private:
    explicit LogModel(QObject *parent = nullptr);

    static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message);

    static constexpr int kMaxLines = 2000;

    bool m_installed = false;
    QtMessageHandler m_previousHandler = nullptr;
};
