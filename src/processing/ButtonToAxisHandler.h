#pragma once

#include <memory>

#include "IActionHandler.h"

/**
 * @brief Cross-domain bridge, the reverse of AxisToButtonHandler: treats a
 *        button press/release as a full-deflection/rest axis position,
 *        forwarding a synthesized AxisEvent to m_wrapped (axisIndex 0 -
 *        there is no real axis behind this, just evt.systemPath carried
 *        through for whatever the wrapped handler needs it for).
 *
 * Exists for source inputs that report as a plain HID button rather than an
 * analog axis (seen on some Bluetooth-connected XInput-compatible pads,
 * whose triggers can enumerate as digital buttons instead of the analog
 * byte fields XInput itself always allocates for them) - there was
 * previously no way to route that kind of button straight to an analog
 * target like ViGEm's Left/Right Trigger axis, since every axis-target
 * handler (CurveHandler included) only ever reacts to processAxis().
 *
 * Always emits on the same canonical [0, 65535] scale every other axis
 * handler in this codebase authors thresholds/curves against - pressed ->
 * 65535 (full deflection), released -> 0 - so m_wrapped (typically a
 * CurveHandler, whose own inputMin/inputMax default to that exact same
 * [0, 65535] range when its binding has no sourceAxis to resolve a real HID
 * range from) needs no special configuration to consume it correctly.
 */
class ButtonToAxisHandler : public IActionHandler
{
public:
    explicit ButtonToAxisHandler(std::shared_ptr<IActionHandler> wrapped);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    /// Rebuilds this handler's "ButtonToAxisHandler" binding JSON - no
    /// parameters of its own (unlike AxisToButtonHandler's threshold/
    /// invert), just a top-level "wrappedAction" sibling of "actionType",
    /// same convention every other wrapping handler's own toJson() uses.
    QJsonObject toJson() const override;

private:
    std::shared_ptr<IActionHandler> m_wrapped;
};
