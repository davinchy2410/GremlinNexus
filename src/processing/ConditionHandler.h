#pragma once

#include <memory>

#include <QString>

#include "IActionHandler.h"

class EventRouter;

/**
 * @brief Gates another handler behind a modifier button's physical state.
 *
 * Wraps m_wrapped: every processAxis/processButton call is forwarded to it
 * only if EventRouter::isButtonPressed(modSystemPath, modButtonIndex)
 * currently equals m_requirePressed; otherwise the event is silently
 * dropped. This checks the modifier's *physical* state (see EventRouter's
 * m_buttonStates), independent of the active mode and independent of
 * whatever the modifier button's own route (if any) is bound to - the same
 * button can simultaneously be a normal action trigger and a
 * ConditionHandler modifier elsewhere in the profile.
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
};
