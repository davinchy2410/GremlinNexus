#pragma once

#include <memory>

#include "IActionHandler.h"
#include "IVirtualOutputDevice.h"

/**
 * @brief Direct physical-button -> virtual-button passthrough.
 *
 * On processButton, forwards evt.pressed straight to
 * target->setButton(targetButton, evt.pressed) with no transformation.
 * Axis events carry no meaning for a button remap and are ignored.
 */
class ButtonRemapHandler : public IActionHandler
{
public:
    /**
     * @param target       Virtual output device that receives the remapped press/release.
     * @param targetButton Button index on target, [0, 128).
     */
    ButtonRemapHandler(std::shared_ptr<IVirtualOutputDevice> target, int targetButton);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    /// Rebuilds this handler's "ButtonRemapHandler" binding JSON (Fase 10.8),
    /// matching ProfileManager::instantiateButtonRemapHandler()'s schema.
    QJsonObject toJson() const override;

    /// Read-only introspection (Macro Editor "record joystick buttons"):
    /// lets MacroRecorderViewModel discover what vJoy device/button a
    /// physical button currently drives, without adding a second entry
    /// point for invoking this handler. Never used by the hot dispatch path
    /// itself - processButton() already holds these privately for that.
    std::shared_ptr<IVirtualOutputDevice> target() const { return m_target; }
    int targetButton() const { return m_targetButton; }

private:
    std::shared_ptr<IVirtualOutputDevice> m_target;
    int m_targetButton;
};
