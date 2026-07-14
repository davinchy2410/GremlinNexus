#include "AxisToButtonHandler.h"

#include <algorithm>
#include <utility>

#include <QJsonObject>
#include <QLatin1String>

AxisToButtonHandler::AxisToButtonHandler(int threshold, bool invert, std::shared_ptr<IActionHandler> wrapped,
                                          int inputMin, int inputMax)
    : m_threshold(threshold)
    , m_invert(invert)
    , m_wrapped(std::move(wrapped))
    , m_inputMin(inputMin)
    , m_inputMax(inputMax)
{
}

void AxisToButtonHandler::processAxis(const AxisEvent &evt)
{
    // Normalize the raw HID value from [m_inputMin, m_inputMax] to the
    // canonical [0, 65535] range threshold is authored/compared on - same
    // pattern SplitAxisHandler/CurveHandler/etc. already use - so a device
    // with a narrower native range (e.g. 12-bit, 0-4095) can still reach a
    // threshold authored assuming the full 16-bit scale.
    const double inputRange = static_cast<double>(m_inputMax - m_inputMin);
    const double normalized = (inputRange > 0.0)
        ? std::clamp((static_cast<double>(evt.value) - m_inputMin) / inputRange, 0.0, 1.0)
        : 0.5;
    const int value = static_cast<int>(normalized * 65535.0);

    const bool aboveThreshold = value >= m_threshold;
    const bool isPressed = m_invert ? !aboveThreshold : aboveThreshold;

    if (isPressed == m_wasPressed) {
        return;
    }
    m_wasPressed = isPressed;

    if (m_wrapped) {
        m_wrapped->processButton(ButtonEvent{evt.systemPath, 0, isPressed});
    }
}

void AxisToButtonHandler::processButton(const ButtonEvent & /*evt*/)
{
    // This handler only ever produces button events from an axis; it does
    // not itself react to a real button event.
}

QJsonObject AxisToButtonHandler::toJson() const
{
    QJsonObject binding;
    binding[QLatin1String("actionType")] = QStringLiteral("AxisToButtonHandler");

    QJsonObject parameters;
    parameters[QLatin1String("threshold")] = m_threshold;
    parameters[QLatin1String("invert")] = m_invert;
    parameters[QLatin1String("inputMin")] = m_inputMin;
    parameters[QLatin1String("inputMax")] = m_inputMax;
    binding[QLatin1String("parameters")] = parameters;

    if (m_wrapped) {
        binding[QLatin1String("wrappedAction")] = m_wrapped->toJson();
    }
    return binding;
}
