#include "VoiceFeedbackManager.h"

#include <QLocale>

VoiceFeedbackManager &VoiceFeedbackManager::instance()
{
    static VoiceFeedbackManager s_instance;
    return s_instance;
}

VoiceFeedbackManager::VoiceFeedbackManager(QObject *parent)
    : QObject(parent), m_enabled(true)
{
    m_speech = new QTextToSpeech(this);
    // Forzamos inglés para que use voces como Zira/David que suenan mejor.
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
        emit enabledChanged();
    }
}

void VoiceFeedbackManager::onModeChanged(const QString &modeName)
{
    if (m_enabled && m_speech) {
        m_speech->say(QString("Activating %1 Mode").arg(modeName));
    }
}
