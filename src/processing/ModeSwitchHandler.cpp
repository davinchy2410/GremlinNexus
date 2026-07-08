#include "ModeSwitchHandler.h"

#include <utility>

#include <QDateTime>
#include <QJsonObject>

#include "EventRouter.h"

namespace {
/// Fase 20.38: shared across every ModeSwitchHandler instance, not a
/// per-instance member (see Fase 20.36's failed per-instance attempt) - a
/// mechanical bounce on the physical toggle button fires its *release*
/// after the mode has already flipped, so the bounce's own press/release
/// pair gets resolved against a *different* ModeSwitchHandler instance
/// (whichever mode is now active) than the one that just fired. That new
/// instance's own timer would start at 0 and never see the bounce coming;
/// a single process-wide timestamp closes that gap.
qint64 s_lastModeSwitchEventTime = 0;
}

ModeSwitchHandler::ModeSwitchHandler(EventRouter &router, QString targetMode)
    : m_router(router)
    , m_targetMode(std::move(targetMode))
{
}

void ModeSwitchHandler::processAxis(const AxisEvent & /*evt*/)
{
    // Mode switches are triggered by buttons, not axes.
}

void ModeSwitchHandler::processButton(const ButtonEvent &evt)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (evt.pressed) {
        if (now - s_lastModeSwitchEventTime < 250) {
            return; // Debounce: a mechanical switch bounce re-firing the same physical press.
        }
        s_lastModeSwitchEventTime = now;

        const QString oldMode = m_router.currentMode();
        if (oldMode != m_targetMode) {
            m_router.setMode(m_targetMode);
            m_router.reevaluateHeldState(evt.systemPath, evt.buttonIndex);
        }
    } else {
        // Ignore the bounced release itself (this handler has nothing to do
        // on release anyway), but still bump the shared timestamp so a false
        // press arriving right after this release is caught too.
        s_lastModeSwitchEventTime = now;
    }
}

QJsonObject ModeSwitchHandler::toJson() const
{
    QJsonObject json;
    json[QStringLiteral("actionType")] = QStringLiteral("ModeSwitch");
    QJsonObject parameters;
    parameters[QStringLiteral("targetMode")] = m_targetMode;
    json[QStringLiteral("parameters")] = parameters;
    return json;
}
