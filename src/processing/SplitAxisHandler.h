#pragma once

#include <memory>

#include <QJsonObject>

#include "IActionHandler.h"
#include "IVirtualOutputDevice.h"

/**
 * @brief Splits one physical axis' travel in half across two independent
 *        vJoy axes (Fase 20.39): the lower 50% of the raw HID range
 *        ([0, 32767]) is remapped to span lowerTarget's full [0, 65535]
 *        output, and the upper 50% ([32768, 65535]) likewise spans
 *        upperTarget's full range - e.g. one throttle axis driving two
 *        separate vJoy sliders, only one of which is ever "live" at a time.
 *
 * Unlike CurveHandler (one axis in, one axis out, with a response curve),
 * this always writes to *both* targets on every event: whichever half the
 * raw value currently falls outside of is pinned to that target's own range
 * zero (lowerTarget reads 0 once the input crosses into the upper half, and
 * vice versa) rather than left at its last value, so neither target is ever
 * left showing stale motion from before the axis crossed the midpoint.
 */
class SplitAxisHandler : public IActionHandler
{
public:
    /// Fase 20.41/20.42: which physical rest position this axis' travel is
    /// split around. CenterToEdges (the default) assumes the axis rests at
    /// center (32767) - a joystick/rudder split into two independent
    /// pedals/triggers either side of rest, both reading 0 at center.
    /// Sequential assumes the axis rests at 0 (a throttle/slider) - the
    /// lower half ramps lowerTarget from 0 to 65535, then the upper half
    /// ramps upperTarget from 0 to 65535, one after the other.
    enum class SplitMode
    {
        CenterToEdges,
        Sequential,
    };

    SplitAxisHandler(std::shared_ptr<IVirtualOutputDevice> lowerTarget, int lowerAxis, bool lowerInvert,
                      std::shared_ptr<IVirtualOutputDevice> upperTarget, int upperAxis, bool upperInvert,
                      SplitMode mode = SplitMode::CenterToEdges, int inputMin = 0, int inputMax = 65535);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    /// Rebuilds this handler's "SplitAxisHandler" binding JSON, matching
    /// ProfileManager::instantiateSplitAxisHandler()'s "parameters" schema.
    QJsonObject toJson() const override;

private:
    std::shared_ptr<IVirtualOutputDevice> m_lowerTarget;
    int m_lowerAxis;
    bool m_lowerInvert;

    std::shared_ptr<IVirtualOutputDevice> m_upperTarget;
    int m_upperAxis;
    bool m_upperInvert;

    SplitMode m_mode;

    /// Raw HID logical range of the source axis (Fase - BUG-002 fix). Not a
    /// fixed [0, 65535]: a 12-bit device (e.g. VKBsim, 0-4095) reports a
    /// narrower range, so processAxis() normalizes to [0, 65535] against these
    /// before the split math - same pattern as CurveHandler/MergeAxisHandler.
    int m_inputMin;
    int m_inputMax;
};
