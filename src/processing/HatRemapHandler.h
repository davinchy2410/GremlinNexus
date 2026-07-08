#pragma once

#include <memory>

#include "IActionHandler.h"
#include "IVirtualOutputDevice.h"

/**
 * @brief Physical-button -> virtual-POV-hat-direction passthrough (Fase 19).
 *
 * On processButton, forwards evt.pressed straight to
 * target->setHatDirection(targetHat, targetDirection, evt.pressed) with no
 * transformation - typically bound to one of a physical hat's own 4
 * synthetic Up/Right/Down/Left buttons (see DeviceManager/
 * ProfileEditorViewModel::buttonDisplayName()). Axis events carry no
 * meaning for a hat remap and are ignored.
 */
class HatRemapHandler : public IActionHandler
{
public:
    /**
     * @param target          Virtual output device that receives the remapped direction.
     * @param targetHat       POV hat index on target, [0, 4).
     * @param targetDirection Direction on targetHat: 0=Up, 1=Right, 2=Down, 3=Left.
     */
    HatRemapHandler(std::shared_ptr<IVirtualOutputDevice> target, int targetHat, int targetDirection);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    /// Rebuilds this handler's "HatRemapHandler" binding JSON (Fase 19),
    /// matching ProfileManager::instantiateHatRemapHandler()'s schema.
    QJsonObject toJson() const override;

private:
    std::shared_ptr<IVirtualOutputDevice> m_target;
    int m_targetHat;
    int m_targetDirection;
};
