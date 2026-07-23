#pragma once

#include <memory>
#include <vector>

#include <QObject>
#include <QProcess>
#include <QVariantList>

class ScriptBridgeServer;

/**
 * @brief Backs the "Scripts" screen (Fase 19, Script Bridge, step 5/6) -
 *        a small CRUD list of configured Python scripts, each independently
 *        start/stop-able as its own child process.
 *
 * Persists to "scripts_config.json" next to the executable, same pattern
 * AutoSwitchManager uses for autoswitch_rules.json. Each running script owns
 * a QProcess launching the Scripts module's embedded python.exe (see
 * ScriptsModuleLocator.h) with the script's own file as its argument;
 * NEXUS_BRIDGE_PORT/NEXUS_BRIDGE_TOKEN environment variables let its
 * `nexus_bridge` SDK connect back to ScriptBridgeServer without anything
 * hardcoded or typed by the user, and PYTHONPATH points at the module's own
 * nexus_bridge SDK so `import nexus_bridge` just works.
 *
 * Deliberately does NOT yet manage per-script input/output channel alias
 * assignments (which physical axis/button a script's "rudder" name means,
 * or which slot of the shared "Nexus Scripts" virtual device its own output
 * writes land on) - that configuration surface, and the EventRouter-side
 * plumbing to actually act on it, is its own separate follow-up once this
 * screen exists to hang it off of. Today a running script can authenticate
 * with ScriptBridgeServer and would receive/send messages if it did, but
 * nothing yet interprets what those messages mean for real input routing.
 */
class ScriptsViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList scripts READ scripts NOTIFY scriptsChanged)

public:
    explicit ScriptsViewModel(ScriptBridgeServer &bridgeServer, QObject *parent = nullptr);
    ~ScriptsViewModel() override;

    /// One QVariantMap per configured script: {name, scriptPath, status}
    /// where status is "Stopped"/"Running"/"Crashed" - a plain string
    /// rather than an enum, since QML only ever needs to display it, not
    /// branch on it as a typed value.
    QVariantList scripts() const;

    Q_INVOKABLE void addScript(const QString &name, const QString &scriptPath);
    Q_INVOKABLE void removeScript(int index);
    Q_INVOKABLE void startScript(int index);
    Q_INVOKABLE void stopScript(int index);

signals:
    void scriptsChanged();

private:
    enum class Status { Stopped, Running, Crashed };

    /// unique_ptr-held (not stored by value) so a pointer to one entry
    /// stays valid across the vector growing/shrinking as other scripts are
    /// added/removed - process signal handlers below capture the owning
    /// ScriptEntry* directly rather than an index, which could otherwise
    /// silently start pointing at the wrong script the moment an earlier
    /// entry is removed.
    struct ScriptEntry
    {
        QString name;
        QString scriptPath;
        Status status = Status::Stopped;
        QProcess *process = nullptr; ///< nullptr whenever status != Running.
        QString token;                ///< Valid only while process is non-null - see ScriptBridgeServer::registerScriptToken().
        /// Set by stopScript() before it terminates/kills the process - lets
        /// teardownProcess() tell a user-requested Stop's forced kill() (see
        /// its own docs on why Windows console-less child processes rarely
        /// exit gracefully from terminate() alone) apart from the process
        /// actually crashing on its own, so a deliberate Stop still lands on
        /// Status::Stopped instead of the misleading Status::Crashed.
        bool stopRequested = false;
    };

    static QString statusToString(Status status);
    static QString configFilePath();
    void loadScripts();
    void saveScripts() const;

    /// Tears down entry's process/token cleanly - called both by
    /// stopScript() (user-initiated) and the process' own finished()/
    /// errorOccurred() handlers (the script exited or crashed on its own).
    /// crashed selects which Status the entry is left in.
    void teardownProcess(ScriptEntry &entry, bool crashed);

    ScriptBridgeServer &m_bridgeServer;
    std::vector<std::unique_ptr<ScriptEntry>> m_scripts;
};
