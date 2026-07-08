#include "HatRemapHandler.h"

#include <QDebug>
#include <QLatin1String>

HatRemapHandler::HatRemapHandler(std::shared_ptr<IVirtualOutputDevice> target, int targetHat, int targetDirection)
    : m_target(std::move(target))
    , m_targetHat(targetHat)
    , m_targetDirection(targetDirection)
{
    if (targetHat < 0 || targetHat >= 4) {
        qWarning() << "HatRemapHandler: targetHat" << targetHat << "is outside the valid range [0, 4)";
    }
    if (targetDirection < 0 || targetDirection > 3) {
        qWarning() << "HatRemapHandler: targetDirection" << targetDirection << "is outside the valid range [0, 3]";
    }
}

void HatRemapHandler::processAxis(const AxisEvent & /*evt*/)
{
    // Hat remaps have no axis equivalent.
}

void HatRemapHandler::processButton(const ButtonEvent &evt)
{
    if (m_target) {
        m_target->setHatDirection(m_targetHat, m_targetDirection, evt.pressed);
    }
}

QJsonObject HatRemapHandler::toJson() const
{
    QJsonObject binding;
    binding[QLatin1String("actionType")] = QStringLiteral("HatRemapHandler");
    binding[QLatin1String("targetOutputId")] = m_target ? m_target->deviceId() : 0;
    if (m_target && m_target->isViGEmDevice()) {
        binding[QLatin1String("targetDeviceType")] = QStringLiteral("vigem");
    }
    binding[QLatin1String("targetHat")] = m_targetHat;
    binding[QLatin1String("targetDirection")] = m_targetDirection;
    return binding;
}
