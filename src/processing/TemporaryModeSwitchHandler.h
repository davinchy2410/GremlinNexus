#pragma once

#include <QString>

#include "IActionHandler.h"

class EventRouter;

/**
 * @brief Shift-state mode switch: while its bound button is held, the
 *        router's mode is m_targetMode; on release, the mode that was
 *        active right before the press is restored automatically.
 *
 * Unlike ModeSwitchHandler (a *permanent* switch — still active after the
 * button is released), this is the "hold this button to access a temporary
 * layer" pattern (shift-states, in HOTAS terminology). m_previousMode is
 * captured on the press that actually shifts and consumed on the matching
 * release; a redundant press while already shifted (e.g. a spurious repeat
 * event) is ignored via m_active, so it can never overwrite m_previousMode
 * with m_targetMode itself and get stuck there after release.
 *
 * Same ownership shape as ModeSwitchHandler: a plain EventRouter& that
 * always outlives this handler, since the routing table holding it is one
 * of the router's own members (see ModeSwitchHandler's docs for the full
 * argument).
 */
class TemporaryModeSwitchHandler : public IActionHandler
{
public:
    TemporaryModeSwitchHandler(EventRouter &router, QString targetMode);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    /// Rebuilds this handler's "TemporaryModeSwitch" binding JSON (Fase 13),
    /// matching ProfileManager::instantiateTemporaryModeSwitchHandler()'s
    /// "parameters.targetMode" schema. m_previousMode/m_active are
    /// deliberately not serialized - they are live gesture state (which
    /// mode to restore on release), not configuration, and always start
    /// fresh when a profile is reloaded.
    QJsonObject toJson() const override;

private:
    EventRouter &m_router;
    QString m_targetMode;
    QString m_previousMode;
    bool m_active = false;
};
