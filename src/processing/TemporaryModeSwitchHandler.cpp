#include "TemporaryModeSwitchHandler.h"

#include <utility>

#include <QDateTime>
#include <QJsonObject>
#include <QLatin1String>

#include "EventRouter.h"

namespace {
/// Fase 20.38: shared across every TemporaryModeSwitchHandler instance, not
/// a per-instance member - see ModeSwitchHandler.cpp's own s_lastModeSwitchEventTime
/// for the full reasoning (a mechanical bounce's second edge can resolve
/// against a different handler instance than the one that fired first, once
/// the mode itself has already flipped in between).
qint64 s_lastTempSwitchEventTime = 0;
}

TemporaryModeSwitchHandler::TemporaryModeSwitchHandler(EventRouter &router, QString targetMode)
    : m_router(router)
    , m_targetMode(std::move(targetMode))
{
}

void TemporaryModeSwitchHandler::processAxis(const AxisEvent & /*evt*/)
{
    // Shift-states are triggered by buttons, not axes.
}

void TemporaryModeSwitchHandler::processButton(const ButtonEvent &evt)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (evt.pressed) {
        if (now - s_lastTempSwitchEventTime < 250) {
            return; // Debounce: a mechanical switch bounce re-firing the same physical press.
        }
        s_lastTempSwitchEventTime = now;

        if (!m_active) {
            m_previousMode = m_router.currentMode();
            if (m_previousMode != m_targetMode) {
                m_router.setMode(m_targetMode);
                m_active = true;
                m_router.reevaluateHeldState(evt.systemPath, evt.buttonIndex);
            }
        }
    } else {
        // Ignore a bounced release itself if it lands right after the last
        // recorded edge - but still bump the shared timestamp so a false
        // press arriving right after this release is caught too.
        s_lastTempSwitchEventTime = now;
        if (m_active) {
            m_router.setMode(m_previousMode);
            m_active = false;
            m_router.reevaluateHeldState(evt.systemPath, evt.buttonIndex);
        }
    }
}

QJsonObject TemporaryModeSwitchHandler::toJson() const
{
    QJsonObject binding;
    binding[QLatin1String("actionType")] = QStringLiteral("TemporaryModeSwitch");

    QJsonObject parameters;
    parameters[QLatin1String("targetMode")] = m_targetMode;
    binding[QLatin1String("parameters")] = parameters;
    return binding;
}
