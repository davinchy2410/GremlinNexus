#include "ModeSwitchHandler.h"

#include <unordered_map>
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
/// (whichever mode is now active) than the one that just fired. A single
/// process-wide timestamp closed that gap, but at too coarse a grain: two
/// DIFFERENT physical mode-switch buttons pressed within 250ms of each
/// other (a spring-loaded 2-way toggle wired as two buttons, or just quick
/// presses) would debounce-suppress each other's legitimate presses,
/// leaking the router stuck on whichever mode the first press set - see
/// TemporaryModeSwitchHandler.cpp's own docs for the sibling bug this was
/// found from.
///
/// Keyed by (systemPath, buttonIndex) instead: still resolves a bounce
/// correctly even if it lands on a different handler instance after the
/// mode flipped (the key describes the physical source, not the instance),
/// but two different physical buttons no longer share one clock.
using ButtonKey = std::pair<QString, int>;

struct ButtonKeyHash
{
    size_t operator()(const ButtonKey &key) const noexcept
    {
        return qHash(key.first) ^ (static_cast<size_t>(key.second) * 0x9E3779B97F4A7C15ULL);
    }
};

std::unordered_map<ButtonKey, qint64, ButtonKeyHash> s_lastModeSwitchEventTimeByButton;
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
    const ButtonKey key{evt.systemPath, evt.buttonIndex};
    qint64 &lastEventTime = s_lastModeSwitchEventTimeByButton[key];

    if (evt.pressed) {
        if (now - lastEventTime < 250) {
            return; // Debounce: a mechanical switch bounce re-firing the same physical press.
        }
        lastEventTime = now;

        const QString oldMode = m_router.currentMode();
        if (oldMode != m_targetMode) {
            m_router.setMode(m_targetMode);
            m_router.reevaluateHeldState(evt.systemPath, evt.buttonIndex);
        }
    } else {
        // Ignore the bounced release itself (this handler has nothing to do
        // on release anyway), but still bump this button's own timestamp so
        // a false press arriving right after this release is caught too.
        lastEventTime = now;
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
