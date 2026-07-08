#include "MouseButtonHandler.h"

#include <QJsonObject>
#include <QLatin1String>

#include "Win32MouseInjector.h"

namespace {
constexpr int kWheelDelta = 120; // Win32's own WHEEL_DELTA - one scroll notch.
} // namespace

MouseButtonHandler::MouseButtonHandler(const QString &targetAction)
    : m_targetAction(targetAction)
{
}

void MouseButtonHandler::processAxis(const AxisEvent & /*evt*/)
{
    // Button-only handler: axis events carry no meaning for a click/scroll transform.
}

void MouseButtonHandler::processButton(const ButtonEvent &evt)
{
    Win32MouseInjector injector;

    if (m_targetAction == QLatin1String("ScrollUp")) {
        if (evt.pressed) {
            injector.sendMouseScroll(kWheelDelta);
        }
        return;
    }
    if (m_targetAction == QLatin1String("ScrollDown")) {
        if (evt.pressed) {
            injector.sendMouseScroll(-kWheelDelta);
        }
        return;
    }

    // Left/Right/Middle: a real click, fires on both press and release -
    // see Win32MouseInjector::sendMouseButton()'s own docs for the mapping.
    injector.sendMouseButton(m_targetAction, evt.pressed);
}

QJsonObject MouseButtonHandler::toJson() const
{
    QJsonObject binding;
    binding[QLatin1String("actionType")] = QStringLiteral("MouseButton");

    QJsonObject parameters;
    parameters[QLatin1String("targetAction")] = m_targetAction;
    binding[QLatin1String("parameters")] = parameters;

    return binding;
}
