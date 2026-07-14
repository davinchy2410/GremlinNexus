#pragma once

#include <memory>

#include <QString>

#include "IActionHandler.h"

class EventRouter;

/**
 * @brief Gates another handler behind a modifier button's physical state.
 *
 * Wraps m_wrapped: every processAxis call, and a button PRESS, is forwarded
 * to it only if EventRouter::isButtonPressed(modSystemPath, modButtonIndex)
 * currently equals m_requirePressed; otherwise the event is silently
 * dropped. This checks the modifier's *physical* state (see EventRouter's
 * m_buttonStates), independent of the active mode and independent of
 * whatever the modifier button's own route (if any) is bound to - the same
 * button can simultaneously be a normal action trigger and a
 * ConditionHandler modifier elsewhere in the profile.
 *
 * A button RELEASE is the one deliberate exception: it forwards whenever
 * m_wasPressed is true (i.e. whenever the matching press WAS let through),
 * regardless of whatever the condition evaluates to *now* - fixing a stuck-
 * output bug where releasing the modifier before releasing the gated button
 * itself (hold mod, press main, release mod, release main) left conditionMet()
 * false exactly when the release needed to reach m_wrapped, so a
 * ButtonRemapHandler's vJoy bit (or any other press/release-paired wrapped
 * handler) never saw its release and stayed stuck on indefinitely. The
 * condition is only ever consulted live for a PRESS - what matters for a
 * RELEASE is just "did I actually forward the press this release is closing
 * out", not the modifier's state at this later, unrelated moment.
 *
 * Same ownership shape as ModeSwitchHandler: a plain EventRouter& that
 * always outlives this handler, since the routing table holding it is one
 * of the router's own members.
 */
class ConditionHandler : public IActionHandler
{
public:
    ConditionHandler(EventRouter &router, QString modSystemPath, int modButtonIndex, bool requirePressed,
                      std::shared_ptr<IActionHandler> wrapped);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    /// Rebuilds this handler's "ConditionHandler" binding JSON, matching
    /// ProfileManager::instantiateConditionHandler()'s schema exactly:
    /// "parameters.modSystemPath"/"modButtonIndex"/"requirePressed" plus a
    /// top-level "wrappedAction" sibling of "actionType" (same convention
    /// ToggleHandler's own toJson() uses) - NOT nested under "parameters".
    QJsonObject toJson() const override;

private:
    bool conditionMet() const;

    EventRouter &m_router;
    QString m_modSystemPath;
    int m_modButtonIndex;
    bool m_requirePressed;
    std::shared_ptr<IActionHandler> m_wrapped;

    /// Whether the most recent button PRESS was actually forwarded to
    /// m_wrapped - see the class docs on why RELEASE keys off this instead
    /// of re-checking conditionMet().
    bool m_wasPressed = false;
};
