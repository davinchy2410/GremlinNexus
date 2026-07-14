#include "MergeAxisHandler.h"

#include <algorithm>
#include <utility>

#include <QJsonObject>

#include "EventRouter.h"

namespace {
/// Rescales value from [inMin, inMax] into vJoy's own [0, 65535] convention,
/// so two axes with different HID logical resolutions combine on equal
/// footing instead of the lower-resolution one silently pulling the
/// combined result toward its own narrower native range. inMin == inMax (a
/// device that hasn't reported a real range yet) falls back to a flat
/// vJoy-center value rather than dividing by zero.
int normalizeTo16Bit(int value, int inMin, int inMax)
{
    const double range = static_cast<double>(inMax - inMin);
    if (range <= 0.0) {
        return 32767;
    }
    return static_cast<int>(std::clamp((static_cast<double>(value - inMin) / range) * 65535.0, 0.0, 65535.0));
}
} // namespace

MergeAxisHandler::MergeAxisHandler(EventRouter &router, std::shared_ptr<IVirtualOutputDevice> target, int targetAxis,
                                    QString otherSystemPath, int otherAxisIndex, bool isSubtraction,
                                    int selfInputMin, int selfInputMax, int otherInputMin, int otherInputMax)
    : m_router(router)
    , m_target(std::move(target))
    , m_targetAxis(targetAxis)
    , m_otherSystemPath(std::move(otherSystemPath))
    , m_otherAxisIndex(otherAxisIndex)
    , m_isSubtraction(isSubtraction)
    , m_selfInputMin(selfInputMin)
    , m_selfInputMax(selfInputMax)
    , m_otherInputMin(otherInputMin)
    , m_otherInputMax(otherInputMax)
{
}

void MergeAxisHandler::processAxis(const AxisEvent &evt)
{
    if (!m_target) {
        return;
    }

    const int selfValue = normalizeTo16Bit(evt.value, m_selfInputMin, m_selfInputMax);
    const int otherRaw = m_router.getAxisValue(m_otherSystemPath, m_otherAxisIndex);
    const int otherValue = normalizeTo16Bit(otherRaw, m_otherInputMin, m_otherInputMax);

    const int combined = m_isSubtraction ? ((selfValue - otherValue) / 2) + 32767 : (selfValue + otherValue) / 2;

    m_target->setAxis(m_targetAxis, std::clamp(combined, 0, 65535));
}

void MergeAxisHandler::processButton(const ButtonEvent & /*evt*/)
{
    // Merging is an axis-only concept; button events have no meaning here.
}

QJsonObject MergeAxisHandler::toJson() const
{
    QJsonObject json;
    json[QStringLiteral("actionType")] = QStringLiteral("MergeAxisHandler");
    json[QStringLiteral("targetOutputId")] = m_target ? m_target->deviceId() : 0;
    if (m_target && m_target->isViGEmDevice()) {
        json[QStringLiteral("targetDeviceType")] = QStringLiteral("vigem");
    }
    json[QStringLiteral("targetAxis")] = m_targetAxis;

    QJsonObject parameters;
    parameters[QStringLiteral("otherSystemPath")] = m_otherSystemPath;
    parameters[QStringLiteral("otherAxisIndex")] = m_otherAxisIndex;
    parameters[QStringLiteral("isSubtraction")] = m_isSubtraction;
    // Fallback for when the device isn't connected at load time - same
    // convention as CurveHandler's own "inputMin"/"inputMax", re-derived
    // from the live device's HID logical range whenever it IS connected
    // (see ProfileManager::instantiateMergeAxisHandler()'s own
    // applyAxisLogicalRange() calls, one per axis).
    parameters[QStringLiteral("inputMin")] = m_selfInputMin;
    parameters[QStringLiteral("inputMax")] = m_selfInputMax;
    parameters[QStringLiteral("otherInputMin")] = m_otherInputMin;
    parameters[QStringLiteral("otherInputMax")] = m_otherInputMax;
    json[QStringLiteral("parameters")] = parameters;
    return json;
}
