#include "KeyboardHandler.h"

#include <QLatin1String>

#include "IKeyboardBackend.h"

KeyboardHandler::KeyboardHandler(std::shared_ptr<IKeyboardBackend> backend, uint16_t scanCode)
    : m_backend(std::move(backend))
    , m_scanCode(scanCode)
{
}

void KeyboardHandler::processAxis(const AxisEvent & /*evt*/)
{
    // Keyboard remaps have no axis equivalent.
}

void KeyboardHandler::processButton(const ButtonEvent &evt)
{
    if (m_backend) {
        m_backend->sendKey(m_scanCode, evt.pressed);
    }
}

QJsonObject KeyboardHandler::toJson() const
{
    QJsonObject binding;
    binding[QLatin1String("actionType")] = QStringLiteral("KeyboardHandler");

    QJsonObject parameters;
    parameters[QLatin1String("scanCode")] = static_cast<int>(m_scanCode);
    binding[QLatin1String("parameters")] = parameters;
    return binding;
}
