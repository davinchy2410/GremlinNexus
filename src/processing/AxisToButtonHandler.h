#pragma once

#include <memory>

#include "IActionHandler.h"

/**
 * @brief Cross-domain bridge: treats an axis crossing a threshold as a
 *        button press/release, forwarding a synthesized ButtonEvent to
 *        m_wrapped (buttonIndex 0 - there is no real button behind this,
 *        just evt.systemPath carried through for whatever the wrapped
 *        handler needs it for).
 *
 * invert selects which side of threshold means "pressed":
 *  - invert == false: evt.value >= threshold -> pressed.
 *  - invert == true:  evt.value <  threshold -> pressed (fires below the threshold).
 *
 * Only forwards on a state *transition* (m_wasPressed changing) - holding
 * the axis past the threshold does not repeatedly re-fire press events,
 * mirroring how a real button only reports one press per press.
 */
class AxisToButtonHandler : public IActionHandler
{
public:
    AxisToButtonHandler(int threshold, bool invert, std::shared_ptr<IActionHandler> wrapped);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    /// Rebuilds this handler's "AxisToButtonHandler" binding JSON, matching
    /// ProfileManager::instantiateAxisToButtonHandler()'s "parameters.threshold"/
    /// "invert" schema plus a top-level "wrappedAction" sibling of
    /// "actionType" (same convention ToggleHandler/ConditionHandler's own
    /// toJson() use) - NOT nested under "parameters". m_wasPressed (live
    /// state, not configuration) is deliberately not serialized, same policy
    /// as every other handler's own transient state.
    QJsonObject toJson() const override;

private:
    int m_threshold;
    bool m_invert;
    std::shared_ptr<IActionHandler> m_wrapped;
    bool m_wasPressed = false;
};
