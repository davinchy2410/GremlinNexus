#include "TemporaryModeSwitchHandler.h"

#include <unordered_map>
#include <utility>

#include <QDateTime>
#include <QJsonObject>
#include <QLatin1String>
#include <QTimer>

#include "EventRouter.h"

namespace {
/// Fase 20.38 kept the reasoning from ModeSwitchHandler.cpp's own
/// s_lastModeSwitchEventTime (shared across instances, not per-instance -
/// see that file's docs for why a per-instance timestamp doesn't catch a
/// bounce that resolves against a *different* handler instance once the
/// mode has already flipped) but got the sharing granularity wrong: a
/// single flat timestamp shared across *every* physical shift button meant
/// two different buttons (e.g. shift->left on one button, shift->right on
/// another) could debounce-suppress each other's legitimate presses if
/// pressed within 250ms of one another - exactly what happens flicking a
/// spring-loaded 2-way toggle wired as two buttons, or just pressing two
/// shift buttons in quick succession. The router's mode would then get
/// stuck reverted to whatever the first shift's release last set it to,
/// since the second shift's press was silently dropped.
///
/// Keyed by (systemPath, buttonIndex) instead - the same physical source
/// button's bounce is still debounced (and still resolves correctly even
/// if a bounce lands on a different handler instance after a mode change,
/// since the key describes the physical source, not which instance handled
/// it), but two different physical buttons no longer share one clock.
using ButtonKey = std::pair<QString, int>;

struct ButtonKeyHash
{
    size_t operator()(const ButtonKey &key) const noexcept
    {
        return qHash(key.first) ^ (static_cast<size_t>(key.second) * 0x9E3779B97F4A7C15ULL);
    }
};

std::unordered_map<ButtonKey, qint64, ButtonKeyHash> s_lastTempSwitchEventTimeByButton;
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
    const ButtonKey key{evt.systemPath, evt.buttonIndex};
    qint64 &lastEventTime = s_lastTempSwitchEventTimeByButton[key];

    if (evt.pressed) {
        // A pending release (see the else branch below) means the physical
        // button is coming back up within its grace window - the exact
        // "successful report that transiently drops one already-held
        // button, corrected on the very next report" hardware glitch
        // documented in DeviceManager.cpp's parseHidReport(). This press IS
        // that correction; the shift was never really released, so just
        // cancel the deferred revert and do nothing else - m_active is
        // still true and the router's mode was never actually changed.
        if (m_releaseGraceTimer && m_releaseGraceTimer->isActive()) {
            m_releaseGraceTimer->stop();
            lastEventTime = now;
            return;
        }

        if (now - lastEventTime < 250) {
            return; // Debounce: a mechanical switch bounce re-firing the same physical press.
        }
        lastEventTime = now;

        if (!m_active) {
            if (m_router.currentMode() != m_targetMode) {
                m_router.pushTemporaryMode(this, m_targetMode);
                m_active = true;
                m_router.reevaluateHeldState(evt.systemPath, evt.buttonIndex);
            }
        }
    } else {
        // Ignore a bounced release itself if it lands right after the last
        // recorded edge - but still bump this button's own timestamp so a
        // false press arriving right after this release is caught too.
        lastEventTime = now;
        if (m_active) {
            // Defer the actual revert by kReleaseGraceMs instead of applying
            // it immediately - a genuine release still reverts the mode,
            // just a few ms later; a spurious single-report dropout gets
            // cancelled by the corrective press above before this ever
            // fires, and the shift never visibly leaves its target mode.
            if (!m_releaseGraceTimer) {
                m_releaseGraceTimer = new QTimer(this);
                m_releaseGraceTimer->setSingleShot(true);
            }
            const QString systemPath = evt.systemPath;
            const int buttonIndex = evt.buttonIndex;
            disconnect(m_releaseGraceTimer, &QTimer::timeout, this, nullptr);
            connect(m_releaseGraceTimer, &QTimer::timeout, this, [this, systemPath, buttonIndex]() {
                m_router.popTemporaryMode(this);
                m_active = false;
                m_router.reevaluateHeldState(systemPath, buttonIndex);
            });
            m_releaseGraceTimer->start(kReleaseGraceMs);
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
