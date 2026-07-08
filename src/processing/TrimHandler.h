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

private:
    std::shared_ptr<IVirtualOutputDevice> m_target;
    int m_targetAxis;
    int m_stepValue;
    int m_currentValue;
};
