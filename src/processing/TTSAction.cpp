#include "TTSAction.h"

#include <utility>

#include <QJsonObject>
#include <QLatin1String>
#include <QTextToSpeech>

TTSAction::TTSAction(QString text)
    : m_text(std::move(text))
{
    m_speech = std::make_unique<QTextToSpeech>();
}

// Out-of-line so QTextToSpeech is a complete type here - required for
// std::unique_ptr's default deleter to compile with only a forward
// declaration in the header.
TTSAction::~TTSAction() = default;

void TTSAction::processAxis(const AxisEvent & /*evt*/)
{
    // Text-to-speech has no axis equivalent.
}

void TTSAction::processButton(const ButtonEvent &evt)
{
    if (!evt.pressed || m_text.isEmpty() || !m_speech) {
        return;
    }
    // Restart from the top on every press, even if still speaking - mirrors
    // AudioAction's own "retrigger, don't queue" behavior for consistency.
    m_speech->stop();
    m_speech->say(m_text);
}

QJsonObject TTSAction::toJson() const
{
    QJsonObject binding;
    binding[QLatin1String("actionType")] = QStringLiteral("TTSAction");

    QJsonObject parameters;
    parameters[QLatin1String("text")] = m_text;
    binding[QLatin1String("parameters")] = parameters;
    return binding;
}
