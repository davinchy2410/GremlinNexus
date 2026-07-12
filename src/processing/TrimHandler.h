#pragma once

#include <memory>

#include "IActionHandler.h"
#include "IVirtualOutputDevice.h"

/**
 * @brief Relative axis trim: each press nudges a virtual axis by a fixed
 *        step instead of setting it to an absolute value (flight-sim
 *        elevator/aileron/rudder trim wheels).
 *
 * Holds the running trimmed value itself (m_currentValue) since there is no
 * physical axis backing it — the "input" is just a momentary button. Only a
 * press advances the trim; a release is a no-op (no auto-repeat-on-hold in
 * this iteration — each physical press is one discrete step). Axis events
 * carry no meaning here and are ignored, mirroring ButtonRemapHandler/
 * KeyboardHandler's own irrelevant-event-type convention.
 */
class TrimHandler : public IActionHandler
{
public:
    /**
     * @param target       Virtual output device that receives the trimmed value.
     * @param targetAxis   Axis index on target (see VJoyDevice's axis ordering).
     * @param stepValue    Amount added to the running value per press (negative to trim down).
     * @param initialValue Starting value before any press, clamped to
     *                     [0, 32767]; 16383 is vJoy's own axis center.
     */
    TrimHandler(std::shared_ptr<IVirtualOutputDevice> target, int targetAxis, int stepValue,
                int initialValue = 16383);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    /// Fase (bugfix - profile save data loss): this override was missing
    /// entirely - every other IActionHandler subclass has one, but this one
    /// fell through to IActionHandler's own default (an empty object, no
    /// "actionType"), which makes ProfileManager::serializeProfile() skip
    /// the binding outright (with only a qWarning - easy to miss) instead
    /// of writing it to the saved profile. A Trim binding worked perfectly
    /// all session, then silently vanished from the JSON on save/reload.
    /// "parameters.initialValue" is written as m_currentValue (the trim's
    /// *live* running position), not whatever initialValue the binding was
    /// originally constructed with - a trim wheel is meant to hold wherever
    /// the pilot left it across a save/reload, not snap back to its
    /// starting point every time (same reasoning a real flight-sim's trim
    /// state survives a restart).
    QJsonObject toJson() const override;

private:
    std::shared_ptr<IVirtualOutputDevice> m_target;
    int m_targetAxis;
    int m_stepValue;
    int m_currentValue;
};
