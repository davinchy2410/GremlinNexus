#pragma once

#include <QObject>

class AutoSwitchManager;

/**
 * @brief Backs the "Settings" screen (Fase 13, Auto-Switch wired to the real
 *        engine in Fase 12(B)) - HidHide status/global cloaking, vJoy reset,
 *        and app-wide preferences (run on Windows startup, Auto-Switch
 *        enabled).
 *
 * globalCloakEnabled tracks its own state internally (HidHideManager has no
 * getter for "is global cloaking currently on" - see its
 * setGlobalCloak()'s own docs). autoSwitchEnabled is NOT similarly
 * self-tracked (that was Fase 13's placeholder) - it now just forwards to
 * m_autoSwitch.isEnabled()/setEnabled(), so this ViewModel and the actual
 * polling engine can never disagree about whether Auto-Switch is running.
 * runOnStartup is the one property with real persistent state outside this
 * process: it reads/writes the Windows "Run" registry key directly via
 * QSettings::NativeFormat, so it reflects whatever's actually registered
 * there (including e.g. a previous install's entry) rather than an
 * in-memory guess.
 */
class SettingsViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isHidHideInstalled READ isHidHideInstalled NOTIFY hidHideStateChanged)
    Q_PROPERTY(bool globalCloakEnabled READ globalCloakEnabled WRITE setGlobalCloakEnabled NOTIFY hidHideStateChanged)
    Q_PROPERTY(bool runOnStartup READ runOnStartup WRITE setRunOnStartup NOTIFY runOnStartupChanged)
    Q_PROPERTY(bool autoSwitchEnabled READ autoSwitchEnabled WRITE setAutoSwitchEnabled NOTIFY autoSwitchChanged)

public:
    explicit SettingsViewModel(AutoSwitchManager &autoSwitch, QObject *parent = nullptr);

    bool isHidHideInstalled() const;

    bool globalCloakEnabled() const;
    void setGlobalCloakEnabled(bool enabled);

    /// Reads back whatever is (or isn't) currently registered for
    /// "GremblingEx" under the Windows "Run" key - not a cached flag, so it
    /// stays honest even if the registry was edited outside this process.
    bool runOnStartup() const;
    void setRunOnStartup(bool enabled);

    bool autoSwitchEnabled() const;
    void setAutoSwitchEnabled(bool enabled);

    /// "Re-Whitelist GremblingEx" button - re-registers this executable on
    /// HidHide's application whitelist (normally only needed once, at
    /// startup - see main.cpp - but exposed here as a manual retry).
    Q_INVOKABLE void whitelistApplication();

    /// "Reset vJoy" button - no vJoy driver-reset manager exists yet, so
    /// this just logs for now rather than blocking the Settings screen on
    /// that unrelated piece of work.
    Q_INVOKABLE void resetVJoy();

signals:
    void hidHideStateChanged();
    void runOnStartupChanged();
    void autoSwitchChanged();

private:
    /// Owned by main.cpp - outlives this ViewModel.
    AutoSwitchManager &m_autoSwitch;

    bool m_globalCloakEnabled = false;
};
