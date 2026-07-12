#include "TrimHandler.h"

#include <algorithm>
#include <utility>

#include <QJsonObject>
#include <QLatin1String>
#include <QString>

TrimHandler::TrimHandler(std::shared_ptr<IVirtualOutputDevice> target, int targetAxis, int stepValue,
                          int initialValue)
    : m_target(std::move(target))
    , m_targetAxis(targetAxis)
    , m_stepValue(stepValue)
    , m_currentValue(std::clamp(initialValue, 0, 32767))
{
}

void TrimHandler::processAxis(const AxisEvent & /*evt*/)
{
    // Trim is driven by discrete button presses, not axis motion.
}

void TrimHandler::processButton(const ButtonEvent &evt)
{
    if (!evt.pressed) {
        return;
    }

    m_currentValue = std::clamp(m_currentValue + m_stepValue, 0, 32767);
    if (m_target) {
        m_target->setAxis(m_targetAxis, m_currentValue);
    }
}

QJsonObject TrimHandler::toJson() const
{
    QJsonObject binding;
    binding[QLatin1String("actionType")] = QStringLiteral("TrimHandler");
    binding[QLatin1String("targetOutputId")] = m_target ? m_target->deviceId() : 0;
    if (m_target && m_target->isViGEmDevice()) {
        binding[QLatin1String("targetDeviceType")] = QStringLiteral("vigem");
    }
    binding[QLatin1String("targetAxis")] = m_targetAxis;

    QJsonObject parameters;
    parameters[QLatin1String("stepValue")] = m_stepValue;
    // The trim's *live* running position, not the value it started at this
    // session - see this method's own docs in TrimHandler.h.
    parameters[QLatin1String("initialValue")] = m_currentValue;
    binding[QLatin1String("parameters")] = parameters;
    return binding;
}
