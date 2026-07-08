#include "ButtonRemapHandler.h"

#include <QDebug>
#include <QLatin1String>

ButtonRemapHandler::ButtonRemapHandler(std::shared_ptr<IVirtualOutputDevice> target, int targetButton)
    : m_target(std::move(target))
    , m_targetButton(targetButton)
{
    if (targetButton < 0 || targetButton >= 128) {
        qWarning() << "ButtonRemapHandler: targetButton" << targetButton << "is outside the valid range [0, 128)";
    }
}

void ButtonRemapHandler::processAxis(const AxisEvent & /*evt*/)
{
    // Button remaps have no axis equivalent.
}

void ButtonRemapHandler::processButton(const ButtonEvent &evt)
{
    if (m_target) {
        m_target->setButton(m_targetButton, evt.pressed);
    }
}

QJsonObject ButtonRemapHandler::toJson() const
{
    QJsonObject binding;
    binding[QLatin1String("actionType")] = QStringLiteral("ButtonRemapHandler");
    binding[QLatin1String("targetOutputId")] = m_target ? m_target->deviceId() : 0;
    if (m_target && m_target->isViGEmDevice()) {
        binding[QLatin1String("targetDeviceType")] = QStringLiteral("vigem");
    }
    binding[QLatin1String("targetButton")] = m_targetButton;
    return binding;
}
