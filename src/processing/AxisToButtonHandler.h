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
    /// inputMin/inputMax: the source axis' raw HID logical range (defaults
    /// [0, 65535] to reproduce the previous unnormalized behavior exactly
    /// for an already-16-bit axis or a profile saved before this existed) -
    /// threshold is always authored/compared on the canonical [0, 65535]
    /// scale, same convention CurveHandler/SplitAxisHandler/etc. already
    /// use, so processAxis() normalizes the raw incoming evt.value into that
    /// scale before comparing against threshold. Without this, a device
    /// reporting a narrower native range (e.g. a 12-bit stick topping out at
    /// 4095) could never reach a threshold authored assuming the full
    /// 16-bit scale.
    AxisToButtonHandler(int threshold, bool invert, std::shared_ptr<IActionHandler> wrapped, int inputMin = 0,
                         int inputMax = 65535);

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
    int m_inputMin;
    int m_inputMax;
};
