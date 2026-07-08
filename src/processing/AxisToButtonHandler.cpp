#include "AxisToButtonHandler.h"

#include <utility>

#include <QJsonObject>
#include <QLatin1String>

AxisToButtonHandler::AxisToButtonHandler(int threshold, bool invert, std::shared_ptr<IActionHandler> wrapped)
    : m_threshold(threshold)
    , m_invert(invert)
    , m_wrapped(std::move(wrapped))
{
}

void AxisToButtonHandler::processAxis(const AxisEvent &evt)
{
    const bool aboveThreshold = evt.value >= m_threshold;
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
    binding[QLatin1String("parameters")] = parameters;

    if (m_wrapped) {
        binding[QLatin1String("wrappedAction")] = m_wrapped->toJson();
    }
    return binding;
}
