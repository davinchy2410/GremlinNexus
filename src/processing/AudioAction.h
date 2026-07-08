#pragma once

#include <memory>

#include <QString>

#include "IActionHandler.h"

class QMediaPlayer;
class QAudioOutput;

/**
 * @brief Plays a .wav/.mp3 file (Qt Multimedia's platform media backend -
 *        Media Foundation on Windows - decodes both) on button press.
 *
 * QMediaPlayer/QAudioOutput are owned directly (not via IVirtualOutputDevice
 * or any shared engine) since playback is entirely local to this one
 * handler and has no bearing on any output device's staged axis/button
 * state. A retrigger while a previous play is still running restarts from
 * the beginning rather than queuing or being ignored - a rapid double-tap
 * should retrigger the sound, matching MacroHandler's own "ignore mid-flight
 * re-press" precedent would be the wrong call here since a sound effect (unlike
 * a timed macro) is short and meant to be interruptible.
 */
class AudioAction : public IActionHandler
{
public:
    explicit AudioAction(QString filePath);
    ~AudioAction() override;

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    /// Rebuilds this handler's "AudioAction" binding JSON, matching
    /// ProfileManager::instantiateAudioAction()'s "parameters.filePath" schema.
    QJsonObject toJson() const override;

private:
    QString m_filePath;
    std::unique_ptr<QMediaPlayer> m_player;
    std::unique_ptr<QAudioOutput> m_audioOutput;
};
