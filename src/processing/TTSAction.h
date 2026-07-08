#pragma once

#include <memory>

#include <QString>

#include "IActionHandler.h"

class QTextToSpeech;

/**
 * @brief Reads a fixed, user-authored string out loud via Qt's native TTS
 *        engine (QTextToSpeech) on button press.
 *
 * Deliberately owns its own QTextToSpeech instance rather than reusing
 * VoiceFeedbackManager's shared one (see that class) - VoiceFeedbackManager's
 * "enabled" toggle is scoped to *mode-change* announcements specifically, and
 * silencing an explicit user-configured TTS binding whenever that unrelated
 * toggle is off would be surprising.
 */
class TTSAction : public IActionHandler
{
public:
    explicit TTSAction(QString text);
    ~TTSAction() override;

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    /// Rebuilds this handler's "TTSAction" binding JSON, matching
    /// ProfileManager::instantiateTTSAction()'s "parameters.text" schema.
    QJsonObject toJson() const override;

private:
    QString m_text;
    std::unique_ptr<QTextToSpeech> m_speech;
};
