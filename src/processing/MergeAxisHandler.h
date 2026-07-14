#pragma once

#include <memory>

#include <QJsonObject>
#include <QString>

#include "IActionHandler.h"
#include "IVirtualOutputDevice.h"

class EventRouter;

/**
 * @brief Combines two physical axes into one virtual axis (e.g. separate
 *        left/right brake pedals merged into a single rudder axis).
 *
 * Writes straight to target - there is no wrapped handler, this is a leaf.
 * In practice the same *configuration* is authored as two separate
 * bindings, one per physical axis (see ProfileManager's schema docs): each
 * MergeAxisHandler instance only ever hears about *its own* axis directly,
 * via processAxis(evt), and reads the other one live via
 * EventRouter::getAxisValue() - so moving *either* physical axis
 * re-evaluates the merge formula and re-stages target's output.
 *
 * isSubtraction selects the combining formula:
 *  - true  (differential, e.g. pedals -> rudder): centers the *difference*
 *    of the two axes around vJoy's own axis center (32767):
 *    ((evt.value - other) / 2) + 32767.
 *  - false (additive, e.g. averaging two throttle levers into one): the
 *    plain average, (evt.value + other) / 2 - no re-centering needed since
 *    the average of two values already in [0, 65535] stays in range.
 * Either way the result is clamped to [0, 65535] before being staged, as a
 * defensive backstop against inputs outside the assumed range.
 *
 * Both formulas assume their inputs already share vJoy's own [0, 65535]
 * logical range - not every physical axis actually reports in that range
 * (DeviceManager.cpp has documented seeing both a 16-bit-range vJoy device
 * and a 12-bit-range VKBsim stick on this same project), so evt.value and
 * the other axis' raw value are each independently rescaled from their own
 * device's true HID logical range (selfInputMin/Max, otherInputMin/Max) into
 * [0, 65535] before the formula above ever sees them - otherwise a
 * lower-resolution axis silently pulls the combined result toward its own
 * narrower native range instead of contributing its full physical travel.
 * Defaults (0/65535) reproduce the previous unnormalized behavior exactly
 * for a pair of already-16-bit axes, so existing profiles/devices are
 * unaffected.
 */
class MergeAxisHandler : public IActionHandler
{
public:
    MergeAxisHandler(EventRouter &router, std::shared_ptr<IVirtualOutputDevice> target, int targetAxis,
                      QString otherSystemPath, int otherAxisIndex, bool isSubtraction, int selfInputMin = 0,
                      int selfInputMax = 65535, int otherInputMin = 0, int otherInputMax = 65535);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    /// Rebuilds this handler's "MergeAxisHandler" binding JSON, matching
    /// ProfileManager::instantiateMergeAxisHandler()'s
    /// "targetOutputId"/"targetAxis"/"parameters" schema.
    QJsonObject toJson() const override;

private:
    EventRouter &m_router;
    std::shared_ptr<IVirtualOutputDevice> m_target;
    int m_targetAxis;
    QString m_otherSystemPath;
    int m_otherAxisIndex;
    bool m_isSubtraction;
    int m_selfInputMin;
    int m_selfInputMax;
    int m_otherInputMin;
    int m_otherInputMax;
};
