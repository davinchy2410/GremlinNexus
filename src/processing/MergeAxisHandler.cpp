#include "MergeAxisHandler.h"

#include <algorithm>
#include <utility>

#include <QJsonObject>

#include "EventRouter.h"

MergeAxisHandler::MergeAxisHandler(EventRouter &router, std::shared_ptr<IVirtualOutputDevice> target, int targetAxis,
                                    QString otherSystemPath, int otherAxisIndex, bool isSubtraction)
    : m_router(router)
    , m_target(std::move(target))
    , m_targetAxis(targetAxis)
    , m_otherSystemPath(std::move(otherSystemPath))
    , m_otherAxisIndex(otherAxisIndex)
    , m_isSubtraction(isSubtraction)
{
}

void MergeAxisHandler::processAxis(const AxisEvent &evt)
{
    if (!m_target) {
        return;
    }

    const int otherValue = m_router.getAxisValue(m_otherSystemPath, m_otherAxisIndex);

    const int combined = m_isSubtraction ? ((evt.value - otherValue) / 2) + 32767 : (evt.value + otherValue) / 2;

    m_target->setAxis(m_targetAxis, std::clamp(combined, 0, 65535));
}

void MergeAxisHandler::processButton(const ButtonEvent & /*evt*/)
{
    // Merging is an axis-only concept; button events have no meaning here.
}

QJsonObject MergeAxisHandler::toJson() const
{
    QJsonObject json;
    json[QStringLiteral("actionType")] = QStringLiteral("MergeAxisHandler");
    json[QStringLiteral("targetOutputId")] = m_target ? m_target->deviceId() : 0;
    if (m_target && m_target->isViGEmDevice()) {
        json[QStringLiteral("targetDeviceType")] = QStringLiteral("vigem");
    }
    json[QStringLiteral("targetAxis")] = m_targetAxis;

    QJsonObject parameters;
    parameters[QStringLiteral("otherSystemPath")] = m_otherSystemPath;
    parameters[QStringLiteral("otherAxisIndex")] = m_otherAxisIndex;
    parameters[QStringLiteral("isSubtraction")] = m_isSubtraction;
    json[QStringLiteral("parameters")] = parameters;
    return json;
}
