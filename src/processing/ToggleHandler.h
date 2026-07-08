#pragma once

#include <memory>

#include "IActionHandler.h"

/**
 * @brief Converts a momentary button into a latching on/off switch.
 *
 * Every physical press (evt.pressed == true) flips m_toggled and forwards a
 * spoofed ButtonEvent carrying that new toggled state to m_wrapped; the
 * physical release is swallowed rather than forwarded, since the wrapped
 * handler is meant to see press/release transitions of the *toggle*, not of
 * the physical button (a ButtonRemapHandler wrapped this way, for example,
 * should hold its virtual button down across two physical presses, not
 * just for the duration of the first one).
 */
class ToggleHandler : public IActionHandler
{
public:
    explicit ToggleHandler(std::shared_ptr<IActionHandler> wrapped);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

private:
    std::shared_ptr<IActionHandler> m_wrapped;
    bool m_toggled = false;
};
