#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

class EventRouter;

/**
 * @brief Live keyboard + joystick-button recorder backing the Macro
 *        Editor's "Record" button (Fase 10.9; joystick support added
 *        later).
 *
 * Keyboard: installs itself as an application-wide QObject::eventFilter
 * while recording, so it sees every QKeyEvent regardless of which QML item
 * happens to have focus - a plain QML Keys.onPressed handler (as used for
 * the ActionPicker's single-key "Press a Key..." capture, Fase 10.8) only
 * ever gets key*presses*, not matching releases with real elapsed time
 * between them, which the Macro Editor needs to record real "Key Down: G" /
 * "Delay: Nms" / "Key Up: G" step sequences. Reports scan codes via
 * QKeyEvent::nativeScanCode() - the actual OS hardware scan code
 * IKeyboardBackend::sendKey() expects - rather than a hand-maintained
 * Qt::Key -> scan-code lookup table, so recording captures exactly the key
 * that was actually pressed.
 *
 * Joystick buttons: connects to DeviceManager::instance()'s buttonPressed
 * signal while recording. A physical press/release only becomes a step if
 * EventRouter resolves it (in whatever mode is active right now) to a
 * *direct* ButtonRemapHandler - deliberately narrow scope, since a button
 * could otherwise be unbound, or wrapped in a Tempo/Toggle/keyboard action/
 * nested macro/etc. with no single unambiguous "vJoy button" to record (see
 * buttonRecordSkipped()). Both capture paths share one QElapsedTimer so a
 * macro mixing keyboard and joystick input gets one consistent timeline
 * instead of two independently-clocked ones.
 */
class MacroRecorderViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)

public:
    explicit MacroRecorderViewModel(EventRouter &router, QObject *parent = nullptr);
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

    /// One recorded PressButton/ReleaseButton step: a physical button
    /// pressed/released while recording that resolved to a direct
    /// ButtonRemapHandler. kind is "PressButton"/"ReleaseButton",
    /// targetOutputId/isVigemTarget/buttonIndex identify the vJoy (or
    /// ViGEm) device+button that handler targets, label a short
    /// human-readable name for the step list. Like stepRecorded()'s own
    /// "Wait" step, a "Wait" is emitted via stepRecorded() just before this
    /// whenever real time elapsed since the previous recorded event.
    void buttonStepRecorded(const QString &kind, int targetOutputId, bool isVigemTarget, int buttonIndex,
                             const QString &label);

    /// Emitted instead of buttonStepRecorded() when a physical button
    /// press/release arrives while recording but can't be captured as a
    /// step - unbound, or bound to something other than a direct vJoy
    /// button remap. reason is a short human-readable explanation for the
    /// popup to surface to the user.
    void buttonRecordSkipped(const QString &reason);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onButtonPressed(const QString &systemPath, int buttonIndex, bool pressed);

private:
    /// Emits a "Wait" step via stepRecorded() if real time elapsed since
    /// the previous recorded event (key or button) - shared by both
    /// capture paths, see class docs above.
    void recordElapsedWaitIfAny();

    EventRouter &m_router;
    bool m_recording = false;
    QElapsedTimer m_lastEventTimer;
    bool m_hasLastEvent = false;
};
