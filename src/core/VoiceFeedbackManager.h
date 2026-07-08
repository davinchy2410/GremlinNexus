#pragma once

#include <QObject>
#include <QTextToSpeech>

/// Announces mode changes out loud via Qt's native TTS engine (Fase: Voice
/// Feedback). Unlike most managers in this codebase, this is NOT wired to
/// EventRouter::modeChanged internally - EventRouter is a plain QObject
/// instance owned by main() (not a Meyer's singleton), so there is no
/// EventRouter::instance() to connect to from in here. main.cpp connects
/// the live `router` instance's modeChanged signal to onModeChanged()
/// after both objects exist.
class VoiceFeedbackManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
    static VoiceFeedbackManager &instance();

    bool enabled() const;
    void setEnabled(bool enabled);

signals:
    void enabledChanged();

public slots:
    void onModeChanged(const QString &modeName);

private:
    explicit VoiceFeedbackManager(QObject *parent = nullptr);
    ~VoiceFeedbackManager() override = default;

    QTextToSpeech *m_speech = nullptr;
    bool m_enabled = true;
};
