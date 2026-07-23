"""Example Nexus Bridge script: splits a single brake input (axis and/or
button) into independent left/right toe brakes, reducing brake force on
whichever side the rudder is deflected toward - a common workaround for
rudder pedals with no toe brakes of their own.

Ported from Kbu's original Joystick Gremlin plugin
(https://forum.dcs.world/topic/380937-kbus-joystick-gremlin-plugins/) -
the original used Joystick Gremlin's own PhysicalInputVariable/
VirtualInputVariable config UI and wrote straight to vJoy; nexus_bridge
has neither, so this copy wires the same math up to plain on_axis()/
on_button() aliases and set_axis() calls instead. Axis values here are
0.0-1.0 (nexus_bridge's own convention), not the original's -1.0-1.0, so
the rudder is re-centered below before the same left/right math runs.

Copy this file, rename it, and point the Scripts panel at your copy -
see rudder_curve.py's own docs on why.

Wire up in the Scripts panel:
  Input aliases  - "rudder" (your rudder/pedal X axis), and "brake"
                    (a brake axis) and/or "brakeButton" (a full-brake
                    button) - wiring both brake inputs is fine, whichever
                    reports the higher value wins.
  Output aliases - "toebrakeLeft", "toebrakeRight" (both Axis).

If your rudder/brake feel backwards, flip INVERT_RUDDER/INVERT_BRAKE
below and re-run - there's no in-app toggle for this (yet), just edit
the copy.
"""

import nexus_bridge as bridge

INVERT_RUDDER = False
INVERT_BRAKE = False

state = {"rudder": 0.5, "brake_axis": 0.0, "brake_button": 0.0}


def apply_deadzone(value, threshold=0.02):
    """Avoid noise right at center/zero."""
    return 0.0 if abs(value) < threshold else value


def update_virtual_brakes():
    # Re-center the 0.0-1.0 rudder axis to -1.0 (left) .. 1.0 (right), same
    # range the original plugin's math assumed.
    r = apply_deadzone(state["rudder"] * 2.0 - 1.0)
    if INVERT_RUDDER:
        r = -r

    b = max(state["brake_axis"], state["brake_button"])
    if INVERT_BRAKE:
        b = 1.0 - b

    # Rudder right (+) reduces LEFT brake; rudder left (-) reduces RIGHT.
    left = b * (1.0 - max(0.0, r))
    right = b * (1.0 - max(0.0, -r))

    bridge.set_axis("toebrakeLeft", max(0.0, min(1.0, left)))
    bridge.set_axis("toebrakeRight", max(0.0, min(1.0, right)))


@bridge.on_axis("rudder")
def handle_rudder(value):
    state["rudder"] = value
    update_virtual_brakes()


@bridge.on_axis("brake")
def handle_brake_axis(value):
    state["brake_axis"] = apply_deadzone(value)
    update_virtual_brakes()


@bridge.on_button("brakeButton")
def handle_brake_button(pressed):
    state["brake_button"] = 1.0 if pressed else 0.0
    update_virtual_brakes()


bridge.run()
