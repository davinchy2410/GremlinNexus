#pragma once

#include <QObject>

class AutoSwitchManager;

/**
 * @brief Backs the "Settings" screen (Fase 13, Auto-Switch wired to the real
 *        engine in Fase 12(B)) - vJoy reset and app-wide preferences (run on
 *        Windows startup, Auto-Switch enabled).
 *
 * HidHide integration was removed entirely (GremblingNexus no longer manages
 * HidHide whitelisting/cloaking itself - see main.cpp). autoSwitchEnabled is
 * NOT self-tracked - it just forwards to m_autoSwitch.isEnabled()/
 * setEnabled(), so this ViewModel and the actual polling engine can never
 * disagree about whether Auto-Switch is running. runOnStartup is the one
 * property with real persistent state outside this process: it reads/writes
 * the Windows "Run" registry key directly via QSettings::NativeFormat, so it
 * reflects whatever's actually registered there (including e.g. a previous
 * install's entry) rather than an in-memory guess.
 */
class SettingsViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool runOnStartup READ runOnStartup WRITE setRunOnStartup NOTIFY runOnStartupChanged)
    Q_PROPERTY(bool autoSwitchEnabled READ autoSwitchEnabled WRITE setAutoSwitchEnabled NOTIFY autoSwitchChanged)

public:
    explicit SettingsViewModel(AutoSwitchManager &autoSwitch, QObject *parent = nullptr);

    /// Reads back whatever is (or isn't) currently registered for
    /// "GremblingEx" under the Windows "Run" key - not a cached flag, so it
    /// stays honest even if the registry was edited outside this process.
    bool runOnStartup() const;
    void setRunOnStartup(bool enabled);

    bool autoSwitchEnabled() const;
    void setAutoSwitchEnabled(bool enabled);

    /// "Reset vJoy" button - no vJoy driver-reset manager exists yet, so
    /// this just logs for now rather than blocking the Settings screen on
    /// that unrelated piece of work.
    Q_INVOKABLE void resetVJoy();

signals:
    void runOnStartupChanged();
    void autoSwitchChanged();

private:
    /// Owned by main.cpp - outlives this ViewModel.
    AutoSwitchManager &m_autoSwitch;
};
