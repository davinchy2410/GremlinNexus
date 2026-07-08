#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <QObject>
#include <QString>

#include "IActionHandler.h"
#include "IVirtualOutputDevice.h"

class QTimer;
class IKeyboardBackend;

/**
 * @brief One step of a MacroHandler sequence.
 *
 * Three independent domains share this one step vocabulary: PressButton/
 * ReleaseButton drive a vJoy button via IVirtualOutputDevice (the original
 * Phase 7 macro concept - "press this virtual button, wait, release it"),
 * PressKey/ReleaseKey (Fase 10.9's Macro Editor) drive a real keyboard key
 * via IKeyboardBackend instead - "Key Down: G" / "Key Up: G" as recorded
 * live from the user's own keyboard - and PressMouseButton/
 * ReleaseMouseButton/MouseScroll drive the OS cursor's buttons/wheel via a
 * stateless Win32MouseInjector, the same primitive MouseButtonHandler
 * itself uses (see executeStep()'s own docs on why MacroHandler calls
 * Win32MouseInjector directly instead of holding a MouseButtonHandler
 * instance per step). A single MacroHandler/MacroStep sequence may freely
 * mix all three kinds of step; whichever target/backend a given step needs
 * is simply left unused if this instance wasn't constructed with one (see
 * MacroHandler's own class docs) - Win32MouseInjector needs no such
 * collaborator at all, since it is stateless and safe to construct ad hoc.
 */
struct MacroStep
{
    enum class Type
    {
        PressButton,
        ReleaseButton,
        Wait,
        PressKey,
        ReleaseKey,
        PressMouseButton,
        ReleaseMouseButton,
        MouseScroll,
    };

    Type type = Type::Wait;
    int buttonIndex = 0;      ///< Meaningful for PressButton/ReleaseButton.
    int waitMs = 0;           ///< Meaningful for Wait.
    uint16_t scanCode = 0;    ///< Meaningful for PressKey/ReleaseKey - see IKeyboardBackend::sendKey().

    /// Meaningful for PressMouseButton/ReleaseMouseButton ("Left"/"Right"/
    /// "Middle" - see Win32MouseInjector::sendMouseButton()) and MouseScroll
    /// ("ScrollUp"/"ScrollDown" - see Win32MouseInjector::sendMouseScroll()),
    /// matching MouseButtonHandler's own "targetAction" vocabulary exactly.
    QString mouseAction;
};

/**
 * @brief Plays back a timed sequence of button presses/releases on a
 *        virtual output device without ever blocking the thread it runs on.
 *
 * Triggered by a physical button press (processButton with pressed=true);
 * a trigger received while already running is ignored — never restarted or
 * queued — so overlapping playback of the same MacroHandler instance can
 * never happen. Release events and axis events are no-ops.
 *
 * Threading (read this before changing anything here): this is a QObject
 * specifically so it can own a QTimer to implement Wait steps instead of
 * std::this_thread::sleep_for, which would stall the entire 200 Hz
 * EventRouter tick — and therefore every other route, and this
 * ultra-low-latency project's entire reason for existing — for the
 * duration of the sleep. PressButton/ReleaseButton steps run immediately,
 * back to back, until a Wait step is hit; at that point executeStep()
 * returns, yielding control back to the Qt event loop, and
 * QTimer::timeout() resumes execution exactly where it left off once the
 * wait elapses — no separate thread is ever spun up. This matters beyond
 * just latency: m_target (like every IVirtualOutputDevice) is not
 * internally synchronized, by design (see VJoyDevice), because the engine
 * guarantees only one thread ever touches a given output device. A
 * background-thread macro implementation would violate that guarantee and
 * race with EventRouter's tick() and every other handler. So: construct
 * this, and let it receive processButton(), only from the same thread
 * EventRouter itself lives on.
 *
 * target and keyboardBackend are each independently optional (may be
 * nullptr/null shared_ptr) - a pure-keyboard macro (only PressKey/
 * ReleaseKey/Wait steps, e.g. anything built by the Macro Editor's Record
 * button) never touches target, and a pure-vJoy-button macro never touches
 * keyboardBackend. A step whose required collaborator is null is silently
 * skipped rather than crashing (see executeStep()) - the same "missing
 * driver/device just means this step does nothing" tolerance every other
 * handler in this codebase already has for an unacquired output device.
 */
class MacroHandler : public QObject, public IActionHandler
{
    Q_OBJECT

public:
    MacroHandler(std::shared_ptr<IVirtualOutputDevice> target, std::shared_ptr<IKeyboardBackend> keyboardBackend,
                 std::vector<MacroStep> steps, QObject *parent = nullptr);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    /// Rebuilds this handler's "MacroHandler" binding JSON (Fase 10.8),
    /// matching ProfileManager::instantiateMacroHandler()'s
    /// "parameters.steps" schema - a full, explicit step list, regardless
    /// of whether this instance was originally built from that same
    /// explicit form or from the Action Picker's "quick timed press"
    /// shorthand (both produce the same in-memory m_steps either way, so
    /// there is nothing shorthand-specific left to preserve here).
    QJsonObject toJson() const override;

private slots:
    void onTimerFired();

private:
    /// Runs steps synchronously (they're just in-memory setButton calls)
    /// until either the sequence ends or a Wait step arms m_timer and
    /// returns, yielding to the event loop.
    void executeStep();

    std::shared_ptr<IVirtualOutputDevice> m_target;
    std::shared_ptr<IKeyboardBackend> m_keyboardBackend;
    std::vector<MacroStep> m_steps;
    std::size_t m_currentStep = 0;
    bool m_running = false;
    QTimer *m_timer = nullptr;
};
