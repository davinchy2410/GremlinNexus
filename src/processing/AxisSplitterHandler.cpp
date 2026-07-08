#include "AxisSplitterHandler.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonObject>
#include <QLatin1String>

AxisSplitterHandler::AxisSplitterHandler(std::shared_ptr<IVirtualOutputDevice> target, std::vector<Zone> zones,
                                          int inputMin, int inputMax)
    : m_target(std::move(target))
    , m_zones(std::move(zones))
    , m_inputMin(inputMin)
    , m_inputMax(inputMax)
    , m_zoneActive(m_zones.size(), false)
{
}

void AxisSplitterHandler::processAxis(const AxisEvent &evt)
{
    const double inputRange = static_cast<double>(m_inputMax - m_inputMin);
    if (inputRange <= 0.0 || !m_target) {
        return;
    }

    const double fraction =
        std::clamp((static_cast<double>(evt.value) - m_inputMin) / inputRange, 0.0, 1.0);

    for (std::size_t i = 0; i < m_zones.size(); ++i) {
        const Zone &zone = m_zones[i];
        const bool inside = fraction >= zone.minFraction && fraction <= zone.maxFraction;
        if (inside != m_zoneActive[i]) {
            m_zoneActive[i] = inside;
            m_target->setButton(zone.targetButton, inside);
        }
    }
}

void AxisSplitterHandler::processButton(const ButtonEvent & /*evt*/)
{
    // Axis-only handler: button events have no meaning for a splitter zone.
}

QJsonObject AxisSplitterHandler::toJson() const
{
    QJsonObject binding;
    binding[QLatin1String("actionType")] = QStringLiteral("AxisSplitterHandler");
    binding[QLatin1String("targetOutputId")] = m_target ? m_target->deviceId() : 0;
    if (m_target && m_target->isViGEmDevice()) {
        binding[QLatin1String("targetDeviceType")] = QStringLiteral("vigem");
    }

    QJsonArray zonesArray;
    for (const Zone &zone : m_zones) {
        QJsonObject zoneObject;
        zoneObject[QLatin1String("min")] = zone.minFraction;
        zoneObject[QLatin1String("max")] = zone.maxFraction;
        zoneObject[QLatin1String("targetButton")] = zone.targetButton;
        zonesArray.append(zoneObject);
    }

    QJsonObject parameters;
    parameters[QLatin1String("inputMin")] = m_inputMin;
    parameters[QLatin1String("inputMax")] = m_inputMax;
    parameters[QLatin1String("zones")] = zonesArray;
    binding[QLatin1String("parameters")] = parameters;
    return binding;
}
