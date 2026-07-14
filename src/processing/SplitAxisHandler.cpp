#include "SplitAxisHandler.h"

#include <algorithm>
#include <cstdint>
#include <utility>

SplitAxisHandler::SplitAxisHandler(std::shared_ptr<IVirtualOutputDevice> lowerTarget, int lowerAxis, bool lowerInvert,
                                    std::shared_ptr<IVirtualOutputDevice> upperTarget, int upperAxis, bool upperInvert,
                                    SplitMode mode, int inputMin, int inputMax)
    : m_lowerTarget(std::move(lowerTarget))
    , m_lowerAxis(lowerAxis)
    , m_lowerInvert(lowerInvert)
    , m_upperTarget(std::move(upperTarget))
    , m_upperAxis(upperAxis)
    , m_upperInvert(upperInvert)
    , m_mode(mode)
    , m_inputMin(inputMin)
    , m_inputMax(inputMax)
{
}

void SplitAxisHandler::processAxis(const AxisEvent &evt)
{
    // BUG-002 fix: normalize the raw HID value from [m_inputMin, m_inputMax]
    // to the canonical [0, 65535] range before applying the split math, so
    // devices with a narrower logical range (e.g. a 12-bit 0-4095 stick) split
    // across their full travel instead of only the bottom sliver of it. Same
    // pattern CurveHandler/MergeAxisHandler already use. Clamping the value to
    // [0, 65535] before the multiply below also keeps ((value - 32768) * 65535)
    // within int range, which a raw >16-bit logical value could otherwise
    // overflow.
    const double inputRange = static_cast<double>(m_inputMax - m_inputMin);
    const double normalized = (inputRange > 0.0)
        ? std::clamp((static_cast<double>(evt.value) - m_inputMin) / inputRange, 0.0, 1.0)
        : 0.5;
    const int value = static_cast<int>(normalized * 65535.0);

    uint16_t outLower = 0;
    uint16_t outUpper = 0;

    // Fase 20.41/20.42: which rest position this split is centered around
    // is now a per-binding choice, not an assumption - CenterToEdges (most
    // joysticks/rudders, resting at 32767) and Sequential (most throttles/
    // sliders, resting at 0) need opposite math, so a single hardcoded
    // formula was always wrong for one of the two.
    if (m_mode == SplitMode::CenterToEdges) {
        if (value <= 32767) {
            // Lower half: center (32767) -> 0, all the way left (0) -> 65535.
            outLower = static_cast<uint16_t>(((32767 - value) * 65535) / 32767);
        } else {
            // Upper half: just past center (32768) -> 0, all the way right (65535) -> 65535.
            outUpper = static_cast<uint16_t>(((value - 32768) * 65535) / 32767);
        }
    } else { // Sequential
        if (value <= 32767) {
            // Map [0, 32767] to [0, 65535].
            outLower = static_cast<uint16_t>((value * 65535) / 32767);
        } else {
            outLower = 65535;
            // Map [32768, 65535] to [0, 65535].
            outUpper = static_cast<uint16_t>(((value - 32768) * 65535) / 32767);
        }
    }

    if (m_lowerInvert) {
        outLower = 65535 - outLower;
    }
    if (m_upperInvert) {
        outUpper = 65535 - outUpper;
    }

    if (m_lowerTarget) {
        m_lowerTarget->setAxis(m_lowerAxis, outLower);
    }
    if (m_upperTarget) {
        m_upperTarget->setAxis(m_upperAxis, outUpper);
    }
}

void SplitAxisHandler::processButton(const ButtonEvent & /*evt*/)
{
    // Splitting is an axis-only concept; button events have no meaning here.
}

QJsonObject SplitAxisHandler::toJson() const
{
    QJsonObject json;
    json[QStringLiteral("actionType")] = QStringLiteral("SplitAxisHandler");

    QJsonObject parameters;
    parameters[QStringLiteral("lowerTargetOutputId")] = m_lowerTarget ? m_lowerTarget->deviceId() : 0;
    parameters[QStringLiteral("lowerTargetAxis")] = m_lowerAxis;
    parameters[QStringLiteral("lowerInvert")] = m_lowerInvert;
    parameters[QStringLiteral("upperTargetOutputId")] = m_upperTarget ? m_upperTarget->deviceId() : 0;
    parameters[QStringLiteral("upperTargetAxis")] = m_upperAxis;
    parameters[QStringLiteral("upperInvert")] = m_upperInvert;
    parameters[QStringLiteral("splitMode")] = static_cast<int>(m_mode);
    parameters[QStringLiteral("inputMin")] = m_inputMin;
    parameters[QStringLiteral("inputMax")] = m_inputMax;
    json[QStringLiteral("parameters")] = parameters;
    return json;
}
