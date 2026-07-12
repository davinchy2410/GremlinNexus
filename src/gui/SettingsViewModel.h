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
    Q_PROPERTY(bool debugLoggingEnabled READ debugLoggingEnabled WRITE setDebugLoggingEnabled NOTIFY debugLoggingChanged)
    Q_PROPERTY(bool hidHideAutoCloakEnabled READ hidHideAutoCloakEnabled WRITE setHidHideAutoCloakEnabled NOTIFY hidHideAutoCloakChanged)

public:
    explicit SettingsViewModel(AutoSwitchManager &autoSwitch, QObject *parent = nullptr);

    /// Reads back whatever is (or isn't) currently registered for
    /// "GremblingEx" under the Windows "Run" key - not a cached flag, so it
    /// stays honest even if the registry was edited outside this process.
    bool runOnStartup() const;
    void setRunOnStartup(bool enabled);

    bool autoSwitchEnabled() const;
    void setAutoSwitchEnabled(bool enabled);

    /// Forwards to AsyncLogSink::instance().isEnabled()/setEnabled() - see
    /// that class's own docs for why this defaults to off and what turning
    /// it on/off actually does. Not self-tracked, same reasoning as
    /// autoSwitchEnabled above: this ViewModel and the real log sink must
    /// never be able to disagree about whether file logging is active.
    bool debugLoggingEnabled() const;
    void setDebugLoggingEnabled(bool enabled);

    /// Backed by QSettings ("HidHide/AutoCloakManagement" - kept in sync
    /// with the same key main.cpp and DeviceManager::initialize() read
    /// directly, since this ViewModel only exists once the app is already
    /// past the startup sequence that key actually controls). Unlike
    /// debugLoggingEnabled above, toggling this has no live effect on the
    /// current session - it only changes what the *next* launch does (skip
    /// or run HidHide's uncloak/recloak dance around startup device
    /// enumeration) - see HidHideController's own docs for why that
    /// sequencing exists and DeviceMonitorWorker::startMonitoring()'s docs
    /// for the extra ~2s of warmup rescans it also skips when off.
    bool hidHideAutoCloakEnabled() const;
    void setHidHideAutoCloakEnabled(bool enabled);

    /// "Reset vJoy" button - no vJoy driver-reset manager exists yet, so
    /// this just logs for now rather than blocking the Settings screen on
    /// that unrelated piece of work.
    Q_INVOKABLE void resetVJoy();

signals:
    void runOnStartupChanged();
    void autoSwitchChanged();
    void debugLoggingChanged();
    void hidHideAutoCloakChanged();

private:
    /// Owned by main.cpp - outlives this ViewModel.
    AutoSwitchManager &m_autoSwitch;
};
