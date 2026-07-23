#pragma once

#include <memory>
#include <vector>

#include <QObject>
#include <QProcess>
#include <QVariantList>

class QJsonArray;
class QJsonObject;
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
 * Alias routing (Fase 19.6): each script carries its own input alias list
 * (which physical device axis/button a name like "rudder" means) and output
 * alias list (which channel of the shared "Nexus Scripts" virtual device a
 * name like "toebrakeOutput" writes to), persisted alongside it in
 * scripts_config.json, editable from the Scripts panel (step 2).
 *
 * Step 3/6 wired the OUTGOING half: onScriptMessageReceived() listens to
 * every authenticated script's ScriptBridgeServer::messageReceived(),
 * resolves a "setAxis"/"setButton" message's own "name" against the sending
 * script's output aliases, and injects it into DeviceManager exactly like a
 * real device would (injectAxisValue()/injectButtonPress()) - so a binding
 * placed on "Nexus Scripts" in Profiles fires for real.
 *
 * Step 4/6 (this step) wires the INCOMING half: onDeviceAxisMoved()/
 * onDeviceButtonPressed() listen to every DeviceManager axis/button change
 * (real hardware only - the "Nexus Scripts" virtual device itself is never
 * a legitimate input source, see availableInputDevices()), resolve it
 * against every running script's own input aliases, and push an
 * "axisState"/"buttonState" message to whichever script(s) claimed that
 * exact device+channel - matching MasterPlan.md's own Fase 19 protocol
 * naming (nexus_bridge's on_axis()/on_button() dispatch on these exact
 * type strings). An axis value is normalized to [0.0, 1.0] using the
 * source device's own real HID logical range when known, falling back to
 * the same [0, 65535] project-wide default otherwise - same convention
 * step 3 already uses for the opposite direction, so a script author works
 * in one consistent unit regardless of which way data is flowing.
 *
 * Step 5/6 (safety): teardownProcess() resets every one of the stopping/
 * crashing script's output aliases to neutral (axis 0, button released)
 * before it clears the entry's status - MasterPlan.md's own Fase 19 design
 * calls this out explicitly, so a script dying never leaves e.g. a virtual
 * brake stuck at full deflection.
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

    /// Input alias: "name" is what the script's own @bridge.on_axis(name)/
    /// @bridge.on_button(name) refers to; devicePath+channelIndex+isAxis
    /// identify the physical device channel that feeds it. Rejects a
    /// duplicate name within the same script's own input list (same
    /// reasoning as addScript()'s own name uniqueness - "name" is the only
    /// handle a script has on it, two aliases sharing one would be
    /// ambiguous to route).
    Q_INVOKABLE void addInputAlias(int scriptIndex, const QString &name, const QString &devicePath, int channelIndex, bool isAxis);
    Q_INVOKABLE void removeInputAlias(int scriptIndex, int aliasIndex);

    /// Output alias: "name" is what the script's own bridge.set_axis(name,
    /// value)/bridge.set_button(name, pressed) refers to; channelIndex+
    /// isAxis identify which channel of the shared "Nexus Scripts" virtual
    /// device (see EventRouter::scriptsSystemPath()) it writes to - there's
    /// no devicePath here, unlike an input alias, since the target device is
    /// always that one fixed virtual device.
    Q_INVOKABLE void addOutputAlias(int scriptIndex, const QString &name, int channelIndex, bool isAxis);
    Q_INVOKABLE void removeOutputAlias(int scriptIndex, int aliasIndex);

    /// Physical devices an input alias can point at: one QVariantMap per
    /// connected device - {deviceName, systemPath, numAxes, numButtons} -
    /// for the Scripts panel's own device/channel picker. Excludes the
    /// shared "Nexus Scripts" virtual device itself (see
    /// EventRouter::scriptsSystemPath()) - it's a script's *output* target,
    /// never a sensible input source for another (or its own) input alias.
    Q_INVOKABLE QVariantList availableInputDevices() const;

signals:
    void scriptsChanged();

private slots:
    /// Resolves an authenticated script's "setAxis"/"setButton" message
    /// against its own output aliases and injects the result into
    /// DeviceManager - see this class' own docs on step 3/6.
    void onScriptMessageReceived(const QString &token, const QJsonObject &message);

    /// Forwards a real device's axis/button change to every running
    /// script that claimed it as an input alias - see this class' own
    /// docs on step 4/6.
    void onDeviceAxisMoved(const QString &systemPath, int axisIndex, int value);
    void onDeviceButtonPressed(const QString &systemPath, int buttonIndex, bool pressed);

private:
    enum class Status { Stopped, Running, Crashed };

    struct AliasEntry
    {
        QString name;
        /// Physical device systemPath for an input alias; unused (always
        /// empty) for an output alias, since its target is always the one
        /// shared "Nexus Scripts" virtual device.
        QString devicePath;
        int channelIndex = 0;
        bool isAxis = true;
    };

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

        std::vector<AliasEntry> inputAliases;
        std::vector<AliasEntry> outputAliases;
    };

    static QString statusToString(Status status);
    static QVariantList aliasesToVariantList(const std::vector<AliasEntry> &aliases);
    static QJsonArray aliasesToJson(const std::vector<AliasEntry> &aliases);
    static std::vector<AliasEntry> aliasesFromJson(const QJsonArray &array);
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
