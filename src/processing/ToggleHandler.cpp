#include "ToggleHandler.h"

#include <utility>

#include <QJsonObject>
#include <QLatin1String>

ToggleHandler::ToggleHandler(std::shared_ptr<IActionHandler> wrapped)
    : m_wrapped(std::move(wrapped))
{
}

void ToggleHandler::processAxis(const AxisEvent & /*evt*/)
{
    // Toggling is driven by discrete button presses, not axis motion.
}

void ToggleHandler::processButton(const ButtonEvent &evt)
{
    if (!evt.pressed) {
        return; // Physical release is swallowed; see class docs.
    }

    m_toggled = !m_toggled;

    if (m_wrapped) {
        ButtonEvent spoofed = evt;
        spoofed.pressed = m_toggled;
        m_wrapped->processButton(spoofed);
    }
}

QJsonObject ToggleHandler::toJson() const
{
    QJsonObject binding;
    binding[QLatin1String("actionType")] = QStringLiteral("ToggleHandler");
    if (m_wrapped) {
        binding[QLatin1String("wrappedAction")] = m_wrapped->toJson();
    }
    return binding;
}
