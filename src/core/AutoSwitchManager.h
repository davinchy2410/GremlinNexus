#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

/**
 * @brief Polls the Windows foreground window (Fase 12) and requests a
 *        profile switch when the active application changes.
 *
 * Deliberately polls GetForegroundWindow() on a 1s QTimer instead of
 * installing a global WH_CBT/WH_SHELL hook - a hook runs injected into
 * every process on the desktop, and a misbehaving one can take down other
 * applications or the whole session; a once-a-second poll can only ever
 * affect this process, at the cost of up to ~1s of latency noticing a
 * foreground-app switch, which is an easy trade for a background input
 * remapper like this one.
 *
 * Deliberately decoupled from ProfileManager/ProfileEditorViewModel: this
 * class only ever emits profileSwitchRequested(path) with no idea what a
 * "profile" even is - main.cpp wires that signal straight to
 * ProfileEditorViewModel::loadProfileFromPath(), so this class could be
 * reused (or tested) with zero knowledge of bindings/EventRouter/etc.
 *
 * Not a Meyer's singleton like DeviceManager - main.cpp owns
 * the single instance directly (see its own docs), matching how EventRouter
 * is already owned/passed around by reference rather than looked up via
 * instance().
 */
class AutoSwitchManager : public QObject
{
    Q_OBJECT

public:
    explicit AutoSwitchManager(QObject *parent = nullptr);

    /// Registers (or overwrites) the profile to switch to whenever exeName
    /// (matched case-insensitively against the foreground window's process,
    /// basename only - e.g. "dcs.exe") becomes the foreground application.
    /// Persisted immediately to rulesFilePath().
    void addRule(const QString &exeName, const QString &profilePath);

    /// Removes exeName's rule, if any. A no-op for an unknown exeName.
    /// Persisted immediately.
    void removeRule(const QString &exeName);

    /// Profile to switch to when the foreground application matches no rule
    /// in m_rules - empty means "do nothing" (stay on whatever profile is
    /// already loaded). Persisted immediately.
    void setDefaultProfile(const QString &profilePath);

    QHash<QString, QString> rules() const { return m_rules; }
    QString defaultProfilePath() const { return m_defaultProfilePath; }

    /// Whether m_pollTimer is (meant to be) running - the Settings screen's
    /// "Auto-Switch Profiles" toggle reads this back, so it always reflects
    /// the actual engine state rather than a separate UI-only flag.
    bool isEnabled() const { return m_enabled; }

    /// Starts/stops m_pollTimer to match enabled and persists the new state
    /// (see saveRules()) - a no-op if enabled already matches isEnabled().
    void setEnabled(bool enabled);

signals:
    /// Emitted from poll() when the foreground application changes to one
    /// that resolves to a profile (either a matching rule or the default) -
    /// profilePath is an absolute path to a profile .json, exactly what
    /// ProfileEditorViewModel::loadProfileFromPath() expects (see main.cpp).
    void profileSwitchRequested(const QString &profilePath);

private slots:
    /// m_pollTimer's timeout handler - reads the current foreground
    /// window's owning process, and if its executable basename differs
    /// from m_currentExe (the common case: the tick doesn't matter because
    /// the user is still in the same app), looks it up in m_rules and emits
    /// profileSwitchRequested() accordingly. Silently does nothing if
    /// GetForegroundWindow()/GetWindowThreadProcessId()/OpenProcess()/
    /// QueryFullProcessImageNameW() fail at any step (e.g. no window has
    /// focus at all - during shutdown, on the lock screen - or the
    /// foreground process is a protected one PROCESS_QUERY_LIMITED_INFORMATION
    /// can't be opened for) rather than treating that as a "switch to
    /// nothing" rule match.
    void poll();

private:
    /// Where addRule()/removeRule()/setDefaultProfile() persist m_rules/
    /// m_defaultProfilePath to, and where the constructor loads them back
    /// from - "autoswitch_rules.json" next to the running executable, so it
    /// travels with a portable install same as the app itself.
    static QString rulesFilePath();

    void loadRules();
    void saveRules() const;

    QTimer m_pollTimer;

    /// Lowercased executable basename (e.g. "dcs.exe") -> absolute path to
    /// the profile .json to switch to when that executable is the
    /// foreground application.
    QHash<QString, QString> m_rules;

    /// Profile to fall back to when the foreground executable matches no
    /// m_rules entry. Empty means no default is configured.
    QString m_defaultProfilePath;

    /// Lowercased executable basename of the last-seen foreground window -
    /// lets poll() skip re-matching m_rules (and re-emitting
    /// profileSwitchRequested) on every single tick while the user just
    /// stays in the same application.
    QString m_currentExe;

    /// Whether polling is active - persisted (see saveRules()/loadRules())
    /// so the Settings screen's toggle survives a restart. Defaults true so
    /// an existing rules file with no "enabled" key (pre-Fase-12(B)) keeps
    /// its prior always-on behavior instead of silently going quiet.
    bool m_enabled = true;
};
