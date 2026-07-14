#pragma once

#include <QJsonObject>
#include <QString>

#include "IActionHandler.h"

class EventRouter;

/**
 * @brief Switches EventRouter's active mode when its bound button is pressed.
 *
 * Only a press (evt.pressed == true) triggers the switch; a release is a
 * no-op, and so is any axis event — this is a button-only action.
 *
 * Holds a plain reference to the EventRouter it switches, the same
 * ownership shape MainWindow uses (see Phase 6): the router outlives every
 * handler in its own routing table by construction, since that table is
 * one of the router's own members and gets torn down as part of the
 * router's own destruction — so a ModeSwitchHandler never outlives the
 * EventRouter it references.
 */
class ModeSwitchHandler : public IActionHandler
{
public:
    ModeSwitchHandler(EventRouter &router, QString targetMode);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;
    bool isModeSwitch() const override { return true; }

    /// Rebuilds this handler's "ModeSwitch" binding JSON,
    /// matching ProfileManager::instantiateModeSwitchHandler()'s schema.
    QJsonObject toJson() const override;

private:
    EventRouter &m_router;
    QString m_targetMode;
};
