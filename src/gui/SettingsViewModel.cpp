#include "SettingsViewModel.h"

#include <QCoreApplication>
#include <QDebug>
#include <QSettings>

#include "AsyncLogSink.h"
#include "AutoSwitchManager.h"

namespace {
/// Windows' own per-user "Run" key - a value here (name -> executable path)
/// is launched once at every logon; removing the value stops it. Native
/// (not Ini) format so QSettings talks to the real registry instead of
/// writing an .ini file at this literal path.
QSettings runKeySettings()
{
    return QSettings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                      QSettings::NativeFormat);
}

constexpr auto kRunOnStartupValueName = "GremblingEx";

// Same key main.cpp and DeviceManager::initialize() read directly (see
// SettingsViewModel::hidHideAutoCloakEnabled()'s own docs) - keep in sync.
constexpr auto kHidHideAutoCloakSettingsKey = "HidHide/AutoCloakManagement";

/// Every driver registers itself as a Windows service under this same
/// registry branch regardless of its own installer's engine (Advanced
/// Installer, Inno Setup, or a raw INF) - the same check installer.iss
/// itself uses (see its NeedsVJoy()/NeedsHidHide()/NeedsViGEm() Pascal
/// functions) to decide whether a driver needs installing, confirmed
/// against a real system with all three already installed and running.
bool isDriverServiceInstalled(const QString &serviceName)
{
    QSettings settings(QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\") + serviceName,
                        QSettings::NativeFormat);
    return !settings.allKeys().isEmpty();
}
} // namespace

SettingsViewModel::SettingsViewModel(AutoSwitchManager &autoSwitch, QObject *parent)
    : QObject(parent)
    , m_autoSwitch(autoSwitch)
{
}

bool SettingsViewModel::runOnStartup() const
{
    QSettings settings = runKeySettings();
    return settings.contains(QLatin1String(kRunOnStartupValueName));
}

void SettingsViewModel::setRunOnStartup(bool enabled)
{
    if (enabled == runOnStartup()) {
        return;
    }

    QSettings settings = runKeySettings();
    if (enabled) {
        settings.setValue(QLatin1String(kRunOnStartupValueName),
                           QCoreApplication::applicationFilePath().replace('/', '\\'));
    } else {
        settings.remove(QLatin1String(kRunOnStartupValueName));
    }
    emit runOnStartupChanged();
}

bool SettingsViewModel::autoSwitchEnabled() const
{
    return m_autoSwitch.isEnabled();
}

void SettingsViewModel::setAutoSwitchEnabled(bool enabled)
{
    if (m_autoSwitch.isEnabled() == enabled) {
        return;
    }
    m_autoSwitch.setEnabled(enabled);
    emit autoSwitchChanged();
}

bool SettingsViewModel::debugLoggingEnabled() const
{
    return AsyncLogSink::instance().isEnabled();
}

void SettingsViewModel::setDebugLoggingEnabled(bool enabled)
{
    if (AsyncLogSink::instance().isEnabled() == enabled) {
        return;
    }
    AsyncLogSink::instance().setEnabled(enabled);
    emit debugLoggingChanged();
}

bool SettingsViewModel::hidHideAutoCloakEnabled() const
{
    return QSettings().value(QLatin1String(kHidHideAutoCloakSettingsKey), true).toBool();
}

void SettingsViewModel::setHidHideAutoCloakEnabled(bool enabled)
{
    if (enabled == hidHideAutoCloakEnabled()) {
        return;
    }
    QSettings().setValue(QLatin1String(kHidHideAutoCloakSettingsKey), enabled);
    emit hidHideAutoCloakChanged();
}

void SettingsViewModel::resetVJoy()
{
    qInfo() << "Resetting vJoy...";
}

bool SettingsViewModel::vjoyDetected() const
{
    return isDriverServiceInstalled(QStringLiteral("vjoy"));
}

bool SettingsViewModel::vigemBusDetected() const
{
    return isDriverServiceInstalled(QStringLiteral("ViGEmBus"));
}

void SettingsViewModel::refreshDiagnostics()
{
    emit diagnosticsRefreshed();
}
