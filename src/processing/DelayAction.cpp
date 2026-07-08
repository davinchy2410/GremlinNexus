#include "DelayAction.h"

#include <QDebug>
#include <QJsonObject>
#include <QLatin1String>
#include <QTimer>

DelayAction::DelayAction(int delayMs)
    : m_delayMs(delayMs)
{
}

void DelayAction::processAxis(const AxisEvent & /*evt*/)
{
    // Delay has no axis equivalent.
}

void DelayAction::processButton(const ButtonEvent &evt)
{
    if (!evt.pressed) {
        return; // Fires once per press, like MacroHandler/KeyboardHandler's own momentary actions.
    }

    const int delayMs = m_delayMs;
    qInfo() << "DelayAction: pausing" << delayMs << "ms";
    // Contextless QTimer::singleShot overload - no QObject needed to own it,
    // and it never blocks the EventRouter tick this call originates from.
    QTimer::singleShot(delayMs, [delayMs]() {
        qInfo() << "DelayAction:" << delayMs << "ms elapsed";
    });
}

QJsonObject DelayAction::toJson() const
{
    QJsonObject binding;
    binding[QLatin1String("actionType")] = QStringLiteral("DelayAction");

    QJsonObject parameters;
    parameters[QLatin1String("delayMs")] = m_delayMs;
    binding[QLatin1String("parameters")] = parameters;
    return binding;
}
