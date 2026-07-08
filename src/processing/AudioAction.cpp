#include "AudioAction.h"

#include <utility>

#include <QAudioOutput>
#include <QJsonObject>
#include <QLatin1String>
#include <QMediaPlayer>
#include <QUrl>

AudioAction::AudioAction(QString filePath)
    : m_filePath(std::move(filePath))
{
    m_player = std::make_unique<QMediaPlayer>();
    m_audioOutput = std::make_unique<QAudioOutput>();
    m_player->setAudioOutput(m_audioOutput.get());
    if (!m_filePath.isEmpty()) {
        m_player->setSource(QUrl::fromLocalFile(m_filePath));
    }
}

// Out-of-line so QMediaPlayer/QAudioOutput are complete types here - required
// for std::unique_ptr's default deleter to compile with only a forward
// declaration in the header.
AudioAction::~AudioAction() = default;

void AudioAction::processAxis(const AxisEvent & /*evt*/)
{
    // Audio playback has no axis equivalent.
}

void AudioAction::processButton(const ButtonEvent &evt)
{
    if (!evt.pressed || m_filePath.isEmpty() || !m_player) {
        return;
    }
    // Restart from the top on every press, even if still playing.
    m_player->stop();
    m_player->play();
}

QJsonObject AudioAction::toJson() const
{
    QJsonObject binding;
    binding[QLatin1String("actionType")] = QStringLiteral("AudioAction");

    QJsonObject parameters;
    parameters[QLatin1String("filePath")] = m_filePath;
    binding[QLatin1String("parameters")] = parameters;
    return binding;
}
