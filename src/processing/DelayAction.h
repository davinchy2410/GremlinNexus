#pragma once

#include "IActionHandler.h"

/**
 * @brief Momentary "pause" action: on button press, schedules a non-blocking
 *        m_delayMs wait (QTimer::singleShot) and logs both ends of it.
 *
 * A standalone leaf action - the Action Picker only exposes a single
 * "Milliseconds" field (no nested action target), so it has nothing to fire
 * once the delay elapses; its only observable effect today is the timed
 * qInfo() pair in processButton(). Non-blocking for the same reason
 * MacroHandler's own Wait step is: this runs on the same thread as
 * EventRouter's tick, and sleeping it would stall every other route in the
 * profile for the duration of the pause.
 */
class DelayAction : public IActionHandler
{
public:
    explicit DelayAction(int delayMs);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    /// Rebuilds this handler's "DelayAction" binding JSON, matching
    /// ProfileManager::instantiateDelayAction()'s "parameters.delayMs" schema.
    QJsonObject toJson() const override;

    /// Configured pause length, in milliseconds - read by TempoHandler/
    /// SequenceHandler cascades to actually gate the next entry in their own
    /// action list instead of firing every entry in the same tick.
    int delayMs() const { return m_delayMs; }

private:
    int m_delayMs;
};
