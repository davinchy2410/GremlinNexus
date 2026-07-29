#include "VoiceFeedbackManager.h"

#include <QLocale>
#include <QSettings>

namespace {
// Same QSettings() (registry-backed, HKCU\Software\Antigravity\GremblingEx -
// see main.cpp's setOrganizationName/setApplicationName) pattern already
// used by SettingsViewModel's debugLoggingEnabled/hidHideAutoCloakEnabled/
// scriptsEnabled - unlike this class before this fix, this key was never
// persisted at all, so "enabled" silently reset to its m_enabled(true)
// default on every relaunch.
constexpr auto kVoiceEnabledSettingsKey = "voice/enabled";
}

VoiceFeedbackManager &VoiceFeedbackManager::instance()
{
    static VoiceFeedbackManager s_instance;
    return s_instance;
}

VoiceFeedbackManager::VoiceFeedbackManager(QObject *parent)
    : QObject(parent)
    , m_enabled(QSettings().value(QLatin1String(kVoiceEnabledSettingsKey), true).toBool())
{
    m_speech = new QTextToSpeech(this);
    // Force English so it picks voices like Zira/David, which sound better.
    m_speech->setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
}

bool VoiceFeedbackManager::enabled() const
{
    return m_enabled;
}

void VoiceFeedbackManager::setEnabled(bool enabled)
{
    if (m_enabled != enabled) {
        m_enabled = enabled;
        QSettings().setValue(QLatin1String(kVoiceEnabledSettingsKey), enabled);
        emit enabledChanged();
    }
}

void VoiceFeedbackManager::onModeChanged(const QString &modeName)
{
    if (m_enabled && m_speech) {
        m_speech->say(QString("Activating %1 Mode").arg(modeName));
    }
}
