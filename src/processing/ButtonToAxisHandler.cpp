#include "ButtonToAxisHandler.h"

#include <utility>

#include <QJsonObject>
#include <QLatin1String>

ButtonToAxisHandler::ButtonToAxisHandler(std::shared_ptr<IActionHandler> wrapped)
    : m_wrapped(std::move(wrapped))
{
}

void ButtonToAxisHandler::processAxis(const AxisEvent & /*evt*/)
{
    // This handler only ever produces axis events from a button; it does
    // not itself react to a real axis event.
}

void ButtonToAxisHandler::processButton(const ButtonEvent &evt)
{
    if (m_wrapped) {
        m_wrapped->processAxis(AxisEvent{evt.systemPath, 0, evt.pressed ? 65535 : 0});
    }
}

QJsonObject ButtonToAxisHandler::toJson() const
{
    QJsonObject binding;
    binding[QLatin1String("actionType")] = QStringLiteral("ButtonToAxisHandler");
    if (m_wrapped) {
        binding[QLatin1String("wrappedAction")] = m_wrapped->toJson();
    }
    return binding;
}
