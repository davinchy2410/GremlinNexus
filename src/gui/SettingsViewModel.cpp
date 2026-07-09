#include "SettingsViewModel.h"

#include <QCoreApplication>
#include <QDebug>
#include <QSettings>

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

void SettingsViewModel::resetVJoy()
{
    qInfo() << "Resetting vJoy...";
}
