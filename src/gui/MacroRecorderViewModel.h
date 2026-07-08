#pragma once

#include <QElapsedTimer>
#include <QObject>

/**
 * @brief Live keyboard recorder backing the Macro Editor's "Record" button
 *        (Fase 10.9).
 *
 * Installs itself as an application-wide QObject::eventFilter while
 * recording, so it sees every QKeyEvent regardless of which QML item
 * happens to have focus - a plain QML Keys.onPressed handler (as used for
 * the ActionPicker's single-key "Press a Key..." capture, Fase 10.8) only
 * ever gets key*presses*, not matching releases with real elapsed time
 * between them, which the Macro Editor needs to record real "Key Down: G" /
 * "Delay: Nms" / "Key Up: G" step sequences.
 *
 * Reports scan codes via QKeyEvent::nativeScanCode() - the actual OS
 * hardware scan code IKeyboardBackend::sendKey() expects - rather than a
 * hand-maintained Qt::Key -> scan-code lookup table, so recording captures
 * exactly the key that was actually pressed.
 */
class MacroRecorderViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)

public:
    explicit MacroRecorderViewModel(QObject *parent = nullptr);
    ~MacroRecorderViewModel() override;

    bool recording() const;

    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();

signals:
    void recordingChanged();

    /// One recorded event, in playback order. kind is "PressKey"/
    /// "ReleaseKey" (scanCode meaningful, label a short human-readable key
    /// name) or "Wait" (waitMs meaningful, everything else 0/empty) - a
    /// "Wait" step is emitted just before a Press/ReleaseKey whenever real
    /// time elapsed since the previous recorded event, so playback timing
    /// matches how the macro was actually performed.
    void stepRecorded(const QString &kind, int scanCode, int waitMs, const QString &label);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool m_recording = false;
    QElapsedTimer m_lastEventTimer;
    bool m_hasLastEvent = false;
};
