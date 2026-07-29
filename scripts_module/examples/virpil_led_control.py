"""Example Nexus Bridge script: controls Virpil hardware LEDs (Control
Panels, Alpha stick, CM2/CM3 throttle, ...) in response to Nexus button
aliases, across ONE OR MORE Virpil devices at once.

Ported from Painter's original Joystick Gremlin plugin, later extended by
oernster (https://github.com/oernster/joystickgremlin-vpcleds). Virpil has
no documented HID protocol for its LEDs - the original plugin (and this
port) instead shell out to Virpil's own official command-line tool,
VPC_LED_Control.exe, which ships with the VPC Software Suite. Each call
passes the target device's VID/PID, an LED ID, and an R/G/B color where
each channel is 0-3 (not 0-255): 0=off, 1=~30%, 2=~60%, 3=100%.

Validated on real hardware (2026-07-25): VPC_LED_Control.exe was found at
the default install path below, and calling it directly from a terminal
against a real VPC Control Panel 3 changed LED colors, confirmed visually.
LED IDs are per-device and not discoverable through any API or Device
Tester - not even sequential or otherwise predictable (LED 5 lit up
Button 13's LED while LED 6 lit up Button 12's, for instance) - the only
way to build this table is trial and error: set one LED ID to a color at a
time and note which physical button lights up.

Multiple devices (2026-07-25): every LED reference in this script is now a
(device name, LED id) pair, not just a bare LED id - LED numbering is only
unique WITHIN one physical device, so a second Virpil device (e.g. an
R-VPC CDT-AEROMAX alongside a VPC Control Panel 3) needs its own name and
its own VID/PID, declared in DEVICES below. Build/edit DEVICES and every
dict below with the companion GUI editor: tools/virpil_led_rules_editor.py
(run with your own system Python, NOT the embedded interpreter this script
runs under - see "Optional rules file" further down).

Copy this file, rename it, and point the Scripts panel at your copy - then
edit DEVICES/BUTTON_LEDS below for your own hardware (the LED <-> button
mapping is very likely different on a different physical panel, even the
same model).

Requires the VPC Software Suite installed (ships with any Virpil hardware)
- this script never talks to the device directly, only to that tool.

Firmware vs. script (tested 2026-07-25): if a button's LED color is also
assigned in Virpil's own Config Tool, the Config Tool's color reasserts
itself on that button's next physical press/release, fighting with
whatever this script just set - confirmed live (sent blue via this
script, pressed the physical button, the Config Tool's own red/green
color came back). Clear that button's Config Tool color assignment if
this script is going to control its LED, to avoid the two fighting.

Fifteen independent ways to react, mix and match as needed (a given button
alias should only appear in one of BUTTON_LEDS/ON_BUTTONS/BLINK_BUTTONS/
CHASE_BUTTONS/TOGGLE_BUTTONS/BREATHE_BUTTONS/SWITCH_LEDS/PULSE_BUTTONS/
DOOR_CHASE/ONOFF_LEDS/BLINK_ONOFF_LEDS/ALARM_LEDS/BLINK_PULSE_BUTTONS, not
several):
  BUTTON_LEDS    - fixed color while held, back to another color on release.
  ON_BUTTONS     - turns one or more LEDs on and leaves them lit - pressing
                    again or releasing does nothing further; something
                    else (e.g. ALL_OFF_BUTTON) has to turn it back off.
  BLINK_BUTTONS  - alternates two colors on a timer while held.
  CHASE_BUTTONS  - steps a color through a list of LEDs, looping, while held.
  TOGGLE_BUTTONS - press once to latch color A, press again for color B -
                    no need to hold, unlike the others. Its last state
                    survives a script restart (see TOGGLE_STATE_FILE).
  BREATHE_BUTTONS - brightness ramps smoothly up/down through all 4 levels
                    in a loop while held, instead of hard-cutting colors.
  AXIS_LEDS      - not a button at all; an axis alias's 0.0-1.0 value
                    drives an LED's brightness continuously (VU-meter
                    style) via dim().
  COMBO_LEDS     - a LED that only lights up while TWO specific button
                    aliases are both held at once (a modifier + a trigger).
  SWITCH_LEDS    - for a real 2-way switch wired as two separate SUSTAINED
                    raw buttons (flip one way and that alias reads held
                    until you flip back) - the LED shows whichever of the
                    two aliases is presently held. Unlike TOGGLE_BUTTONS
                    (one alias, latches per press) or BUTTON_LEDS (one
                    alias, on while held/off while released), this is
                    driven by two independent sustained aliases (2026-07-25:
                    added for landing gear / VTOL switches confirmed to
                    report as held, not momentary). If NEITHER alias is
                    held (a 3-position switch's neutral detent), the LED
                    goes off, matching the panel firmware's own old
                    behavior rather than leaving a stale color lit.
  PULSE_BUTTONS  - a brief flash of color on press, then back to whatever
                    the LED was already showing - confirmation feedback for
                    a genuinely momentary button where nothing should stay
                    lit afterward (2026-07-25: added for a lights on/off
                    switch pair that only pulses, never stays held).
  DOOR_CHASE     - like CHASE_BUTTONS, but for a MOMENTARY switch pair
                    instead of one held button: pressing the "open" alias
                    runs the chase forward through a list of LEDs in one
                    color, pressing the "close" alias runs it backward in
                    another color, each for a fixed number of full passes,
                    then stops on its own (2026-07-25: added for door
                    open/close switches that don't stay held).
  ONOFF_LEDS     - like SWITCH_LEDS, but for a MOMENTARY switch pair
                    instead of a sustained one: two separate press-only
                    aliases, one that SETS the LED to "on", one that SETS
                    it to "off" - since a momentary press has no held state
                    to read back, the last state is persisted to disk
                    (ONOFF_STATE_FILE), same reasoning as TOGGLE_BUTTONS
                    (2026-07-25: added for cruise control and master mode
                    NAV/SCM, both fired by two separate momentary buttons).
  BLINK_ONOFF_LEDS - like ONOFF_LEDS, but blinks for a few seconds right
                    after the press instead of switching color instantly -
                    a "something just changed" confirmation - then settles
                    into a steady color for whichever state it just
                    switched to. Persisted like ONOFF_LEDS (2026-07-25:
                    added for Visión Nocturna, so a glance a few seconds
                    later still tells you which way it was last switched,
                    without blinking forever while it's on).
  ALARM_LEDS     - while a single alias is held, EVERY LED in a given list
                    (e.g. the whole panel) switches to one warning color at
                    once, each restored to whatever it was individually
                    showing before the instant it's released - a
                    whole-panel warning rather than one LED (2026-07-25:
                    added for MASTER ARM, which floods every panel LED red
                    while held to arm the self-destruct button, then
                    "remembers" and restores each LED's own prior state).
  BLINK_PULSE_BUTTONS - like PULSE_BUTTONS's single flash, but blinks a
                    few times over a short duration instead, then reverts
                    to whatever the LED was showing right before - for a
                    momentary press worth catching the eye a bit more than
                    one flash, on an LED that already belongs to something
                    else the rest of the time (2026-07-25: added for an
                    ATC hangar request, sharing B3's LED with the lights
                    pulse; also used for the weapons/shields/engines power
                    allocation dials, sharing B8/B9/B10's LEDs with those
                    systems' own power-toggle color - a rotary encoder's
                    rapid clicks all cancel-and-restart cleanly rather than
                    getting debounced away, unlike the ATC button).
Plus INITIAL_LED_COLORS (set once at script start), SELF_TEST_ON_START (a
cosmetic sweep through every known LED at startup, real-cockpit style,
before INITIAL_LED_COLORS settles in), and ALL_OFF_BUTTON (a single button
that resets every LED this script knows about, on every device, to off and
stops any running blink/chase/breathe animation).

Wire up in the Scripts panel:
  Input aliases  - one per button/axis you want to react to (see each
                    dict above for its example alias names). An alias can
                    point at a button on ANY connected device, not just
                    an LED panel itself - confirmed live (2026-07-25):
                    pressing a VKBsim Gladiator EVO L trigger changed an
                    LED on a completely different device, the VPC Control
                    Panel 3, with zero code changes needed.
  Output aliases - none. This script only drives LEDs; it never writes
                    back to a virtual axis/button.

Optional rules file (2026-07-25): if "virpil_led_rules.json" sits next to
this file, DEVICES and every dict below (BUTTON_LEDS, BLINK_BUTTONS,
CHASE_BUTTONS, TOGGLE_BUTTONS, BREATHE_BUTTONS, AXIS_LEDS, COMBO_LEDS,
SWITCH_LEDS, PULSE_BUTTONS, DOOR_CHASE, ONOFF_LEDS, BLINK_ONOFF_LEDS,
ALARM_LEDS, BLINK_PULSE_BUTTONS, INITIAL_LED_COLORS, SELF_TEST_ON_START,
ALL_OFF_BUTTON) is built from that
file instead of the hardcoded values in this file, so you don't have to
hand-edit Python to change behavior. Build/edit that file with the
companion GUI editor: tools/virpil_led_rules_editor.py, run with your own
system Python (`python tools/virpil_led_rules_editor.py`), NOT the
embedded interpreter this script itself runs under - that editor uses
Tkinter for its window, which the embedded distribution doesn't include.
Without a rules file, this script behaves exactly as it did before this
feature existed - editing the dicts below by hand still works fine.
"""

import json
import os
import subprocess
import threading
import time

import nexus_bridge as bridge

VPC_LED_TOOL = r"C:\Program Files (x86)\VPC Software Suite\tools\VPC_LED_Control.exe"

# See "Optional rules file" in the module docstring above.
RULES_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "virpil_led_rules.json")


def _load_rules_config():
    try:
        with open(RULES_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None  # No rules file yet, or it's not valid JSON - fall back to the hardcoded values below.


_rules_config = _load_rules_config()


def _rules_of_type(action_type):
    return [r for r in _rules_config.get("rules", []) if r.get("actionType") == action_type]


def _color_tuple(color_list):
    """A JSON [r, g, b] list back into the tuple this file's own colors use."""
    return tuple(color_list)


# Every physical Virpil device this script controls, by name - the name is
# your own label (e.g. "panel", "aeromax"), used everywhere below instead
# of a bare VID/PID pair, since LED ids are only unique WITHIN one device.
# Find a device's VID/PID in Device Tester, or in a saved profile's .json
# ("sourceDevice": "...HID#VID_xxxx&PID_yyyy...").
if _rules_config is not None:
    DEVICES = {d["name"]: (d["vid"], d["pid"]) for d in _rules_config.get("devices", [])}
    DEFAULT_DEVICE = _rules_config.get("defaultDevice") or next(iter(DEVICES), "panel")
else:
    DEVICES = {
        "panel": ("3344", "025c"),
        # "aeromax": ("3344", "c4b0"),
    }
    DEFAULT_DEVICE = "panel"

# Named colors, so BUTTON_LEDS below can read by name instead of raw
# numbers. Each channel is 0-3, not 0-255: 0=off, 1=~30%, 2=~60%, 3=100%.
# Pick a name from here rather than guessing your own (R, G, B) tuple -
# CYAN and YELLOW are the two confirmed by eye on real hardware so far.
OFF = (0, 0, 0)
RED = (3, 0, 0)
GREEN = (0, 3, 0)
BLUE = (0, 0, 3)
YELLOW = (3, 3, 0)
CYAN = (0, 3, 3)
MAGENTA = (3, 0, 3)
WHITE = (3, 3, 3)
ORANGE = (3, 1, 0)


def dim(color, factor):
    """Scale a color to a fraction (0.0-1.0) of its own brightness, e.g.
    dim(RED, 0.5) for a half-as-bright red. Rounds each channel back to
    the nearest whole level (0-3), since that's all the hardware accepts."""
    return tuple(round(max(0, min(3, channel * factor))) for channel in color)


DIM_RED = dim(RED, 1 / 3)
DIM_GREEN = dim(GREEN, 1 / 3)
DIM_BLUE = dim(BLUE, 1 / 3)

# One entry per button alias wired up in the Scripts panel:
#   alias name -> (device name, LED id, color while pressed, color while released)
# The 10 entries below are the confirmed LED <-> button mapping for a real
# VPC Control Panel 3 (see the file header) - rename the aliases to
# something meaningful once you know what each physical button does (e.g.
# "gearButton" instead of "button8"), and wire each one up in the Scripts
# panel to the matching physical button.
if _rules_config is not None:
    BUTTON_LEDS = {
        r["alias"]: (r["device"], r["ledId"], _color_tuple(r["colorOn"]), _color_tuple(r["colorOff"]))
        for r in _rules_of_type("color")
    }
else:
    BUTTON_LEDS = {
        "button8": (DEFAULT_DEVICE, 1, GREEN, YELLOW),
        "button9": (DEFAULT_DEVICE, 2, GREEN, YELLOW),
        "button10": (DEFAULT_DEVICE, 3, GREEN, YELLOW),
        "button11": (DEFAULT_DEVICE, 4, GREEN, YELLOW),
        "button13": (DEFAULT_DEVICE, 5, GREEN, YELLOW),
        "button12": (DEFAULT_DEVICE, 6, GREEN, YELLOW),
        "button3": (DEFAULT_DEVICE, 7, GREEN, YELLOW),
        "button1": (DEFAULT_DEVICE, 8, GREEN, YELLOW),
        "button2": (DEFAULT_DEVICE, 9, GREEN, YELLOW),
        "button4": (DEFAULT_DEVICE, 10, GREEN, YELLOW),
    }


# Tracks whatever color this script last actually sent to each (device,
# LED id) pair, so resting_color() below can restore "what it really
# looked like a moment ago" (e.g. off, if ALL_OFF_BUTTON was just pressed)
# instead of always falling back to the fixed INITIAL_LED_COLORS from when
# the script started.
_last_led_color = {}


def set_led(device, led_id, color):
    vid, pid = DEVICES[device]
    _last_led_color[(device, led_id)] = tuple(color)
    r, g, b = color
    # Fire-and-forget on purpose (2026-07-25: briefly made this wait for
    # the process to finish, to fix a rotary encoder spamming a stuck
    # color under rapid clicks - but that added real, felt latency to
    # EVERY animation in the whole file, chase/blink/breathe included, not
    # just the encoder rules. Reverted; the encoder rules' own debounce
    # (see BLINK_PULSE_BUTTONS) is the actual fix for that, without
    # slowing everything else down). CREATE_NO_WINDOW instead of
    # CREATE_NEW_CONSOLE just to avoid flashing a console window per call.
    subprocess.Popen(
        [VPC_LED_TOOL, vid, pid, str(led_id), str(r), str(g), str(b)],
        creationflags=subprocess.CREATE_NO_WINDOW,
    )


def make_handler(device, led_id, color_on, color_off):
    def handler(pressed):
        set_led(device, led_id, color_on if pressed else color_off)
    return handler


for _alias, (_device, _led_id, _color_on, _color_off) in BUTTON_LEDS.items():
    bridge.on_button(_alias)(make_handler(_device, _led_id, _color_on, _color_off))

# One entry per button alias that should just turn one or more LEDs on and
# leave them lit - pressing again does nothing (still on), releasing does
# nothing either (still on). Distinct from BUTTON_LEDS above (which
# reverts to a second color on release) and TOGGLE_BUTTONS below (which
# alternates between two colors on each press) - this one only ever turns
# on; turning it back off needs something else, e.g. ALL_OFF_BUTTON or
# another rule pointed at the same LED(s):
#   alias name -> (device name, list of LED ids, color)
if _rules_config is not None:
    ON_BUTTONS = {
        r["alias"]: (r["device"], r["ledIds"], _color_tuple(r["color"]))
        for r in _rules_of_type("on")
    }
else:
    ON_BUTTONS = {
        # "gearDownIndicator": (DEFAULT_DEVICE, [1], GREEN),
    }


def make_on_handler(device, led_ids, color):
    def handler(pressed):
        if pressed:
            for led_id in led_ids:
                set_led(device, led_id, color)
    return handler


for _alias, (_device, _led_ids, _color) in ON_BUTTONS.items():
    bridge.on_button(_alias)(make_on_handler(_device, _led_ids, _color))

# One entry per button alias that should BLINK while held, instead of just
# switching to a fixed color (see BUTTON_LEDS above for that simpler case -
# a button should only be in one of the two dicts, not both):
#   alias name -> (device name, LED id, color A, color B, seconds between
#                   toggles, color when released)
# Each call to VPC_LED_Control.exe is its own new process (see the file
# header) - fine for a slow blink like this, too slow/flashy on-screen for
# a smooth fade or fast animation.
if _rules_config is not None:
    BLINK_BUTTONS = {
        r["alias"]: (
            r["device"], r["ledId"], _color_tuple(r["colorA"]), _color_tuple(r["colorB"]),
            r["interval"], _color_tuple(r["colorReleased"]),
        )
        for r in _rules_of_type("blink")
    }
else:
    BLINK_BUTTONS = {
        # "warningButton": (DEFAULT_DEVICE, 11, RED, OFF, 0.5, OFF),
    }

_blink_stop_events = {}


def blink_loop(device, led_id, color_a, color_b, interval, stop_event):
    show_a = True
    while not stop_event.is_set():
        set_led(device, led_id, color_a if show_a else color_b)
        show_a = not show_a
        stop_event.wait(interval)


def make_blink_handler(alias, device, led_id, color_a, color_b, interval, color_released):
    def handler(pressed):
        old_event = _blink_stop_events.pop(alias, None)
        if old_event is not None:
            old_event.set()  # Stop any blink loop already running for this button.

        if pressed:
            stop_event = threading.Event()
            _blink_stop_events[alias] = stop_event
            threading.Thread(
                target=blink_loop,
                args=(device, led_id, color_a, color_b, interval, stop_event),
                daemon=True,
            ).start()
        else:
            set_led(device, led_id, color_released)
    return handler


for _alias, (_device, _led_id, _color_a, _color_b, _interval, _color_released) in BLINK_BUTTONS.items():
    bridge.on_button(_alias)(
        make_blink_handler(_alias, _device, _led_id, _color_a, _color_b, _interval, _color_released)
    )

# One entry per button alias that should run a CHASE animation (a color
# stepping through a list of LEDs, one at a time, looping) while held:
#   alias name -> (device name, list of LED ids in order, color, seconds
#                   between steps)
# Tested on real hardware at a 0.3s step - much faster than that starts
# looking choppy on-screen since each step is still its own new process.
if _rules_config is not None:
    CHASE_BUTTONS = {
        r["alias"]: (r["device"], r["ledIds"], _color_tuple(r["color"]), r["interval"])
        for r in _rules_of_type("chase")
    }
else:
    CHASE_BUTTONS = {
        # "readyButton": (DEFAULT_DEVICE, [1, 2, 3, 4], GREEN, 0.3),
    }

_chase_stop_events = {}


def resting_color(device, led_id):
    """What an LED should show when nothing's actively animating it - the
    last color this script actually set there (e.g. off, if ALL_OFF_BUTTON
    was just pressed), or its INITIAL_LED_COLORS entry if it's never been
    touched yet this run, or off if it has neither."""
    key = (device, led_id)
    if key in _last_led_color:
        return _last_led_color[key]
    return INITIAL_LED_COLORS.get(key, OFF)


def chase_loop(device, led_ids, color, interval, stop_event):
    # Snapshot what each LED looked like right before the chase took over,
    # so it can be restored exactly once the chase stops - taken up front
    # rather than read fresh at the end, since by then every LED's "last
    # color" would just be whatever the chase itself last set it to (off).
    original_colors = {led_id: resting_color(device, led_id) for led_id in led_ids}

    index = 0
    previous_led = None
    while not stop_event.is_set():
        if previous_led is not None:
            set_led(device, previous_led, OFF)  # Trailing LED goes dark - makes it look like it's traveling.
        set_led(device, led_ids[index], color)
        previous_led = led_ids[index]
        index = (index + 1) % len(led_ids)
        stop_event.wait(interval)

    for led_id, color_value in original_colors.items():
        set_led(device, led_id, color_value)


def make_chase_handler(alias, device, led_ids, color, interval):
    def handler(pressed):
        old_event = _chase_stop_events.pop(alias, None)
        if old_event is not None:
            old_event.set()  # Stop any chase loop already running for this button.

        if pressed:
            stop_event = threading.Event()
            _chase_stop_events[alias] = stop_event
            threading.Thread(
                target=chase_loop,
                args=(device, led_ids, color, interval, stop_event),
                daemon=True,
            ).start()
        # On release, the chase thread's own stop_event.set() above already
        # turns its last LED off - nothing else to do here.
    return handler


for _alias, (_device, _led_ids, _color, _interval) in CHASE_BUTTONS.items():
    bridge.on_button(_alias)(make_chase_handler(_alias, _device, _led_ids, _color, _interval))

# One entry per button alias that should LATCH between two colors: press
# once to switch to color A, press again to switch back to color B - no
# need to hold the button down, unlike BUTTON_LEDS above. Matches the
# original Virpil plugin's own "momentary button, stateful LED" feature
# (e.g. a single gear button where the LED should stay lit until pressed
# again, not just while held).
#   alias name -> (device name, LED id, color A, color B)
if _rules_config is not None:
    TOGGLE_BUTTONS = {
        r["alias"]: (r["device"], r["ledId"], _color_tuple(r["colorA"]), _color_tuple(r["colorB"]))
        for r in _rules_of_type("toggle")
    }
else:
    TOGGLE_BUTTONS = {
        # "gearToggle": (DEFAULT_DEVICE, 1, RED, GREEN),
    }

# Where each TOGGLE_BUTTONS alias's last state (True = showing color A,
# False = showing color B) is remembered across script restarts, so a
# restart doesn't silently disagree with what the LED is actually showing.
# Change this if you want the file somewhere else, or set to None to turn
# persistence off (state resets to "showing color B" every restart then).
TOGGLE_STATE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "virpil_led_toggle_state.json")


def _load_toggle_state():
    if not TOGGLE_STATE_FILE:
        return {}
    try:
        with open(TOGGLE_STATE_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}  # No saved state yet, or the file's corrupt - start fresh.


def _save_toggle_state():
    if not TOGGLE_STATE_FILE:
        return
    try:
        with open(TOGGLE_STATE_FILE, "w", encoding="utf-8") as f:
            json.dump(_toggle_showing_a, f)
    except OSError:
        pass  # Not worth crashing the script over a failed save.


_toggle_showing_a = _load_toggle_state()


def make_toggle_handler(alias, device, led_id, color_a, color_b):
    def handler(pressed):
        if not pressed:
            return  # Only the press flips the state, the release does nothing.
        showing_a = not _toggle_showing_a.get(alias, False)
        set_led(device, led_id, color_a if showing_a else color_b)
        _toggle_showing_a[alias] = showing_a
        _save_toggle_state()
    return handler


for _alias, (_device, _led_id, _color_a, _color_b) in TOGGLE_BUTTONS.items():
    bridge.on_button(_alias)(make_toggle_handler(_alias, _device, _led_id, _color_a, _color_b))
    # Restore this LED to whatever it was last showing, if anything was saved.
    if _alias in _toggle_showing_a:
        set_led(_device, _led_id, _color_a if _toggle_showing_a[_alias] else _color_b)

# One entry per axis alias whose value (0.0-1.0) should drive an LED's
# brightness, VU-meter style, instead of reacting to a button at all:
#   alias name -> (device name, LED id, color at full brightness)
# Tested on real hardware moving through 0.2/0.5/0.8/1.0: only 3 distinct
# brightness steps were visible, not 4, because 0.5 and 0.8 both rounded to
# the same level - dim()'s 4 whole levels (0-3) is genuinely all the
# hardware has, several different axis values will always look identical.
if _rules_config is not None:
    AXIS_LEDS = {
        r["alias"]: (r["device"], r["ledId"], _color_tuple(r["color"]))
        for r in _rules_of_type("axis")
    }
else:
    AXIS_LEDS = {
        # "throttleMeter": (DEFAULT_DEVICE, 1, GREEN),
    }

# A jittery/noisy physical axis can fire many updates per second - without
# a limit, AXIS_LEDS would launch a new VPC_LED_Control.exe process for
# every single one of them (see the file header on why that's expensive).
# This drops any update that arrives less than this many seconds after the
# last one actually sent for that LED - it doesn't smooth or average
# anything, just skips updates that are too close together in time.
AXIS_LED_MIN_INTERVAL = 0.1

_axis_led_last_sent = {}


def make_axis_handler(device, led_id, base_color):
    def handler(value):
        now = time.monotonic()
        key = (device, led_id)
        if now - _axis_led_last_sent.get(key, 0.0) < AXIS_LED_MIN_INTERVAL:
            return
        _axis_led_last_sent[key] = now
        set_led(device, led_id, dim(base_color, value))
    return handler


for _alias, (_device, _led_id, _base_color) in AXIS_LEDS.items():
    bridge.on_axis(_alias)(make_axis_handler(_device, _led_id, _base_color))

# One entry per button alias that should BREATHE while held - brightness
# ramps smoothly up and down through all 4 levels in a loop, instead of
# hard-cutting between two colors like BLINK_BUTTONS does:
#   alias name -> (device name, LED id, color at full brightness, seconds
#                   per full cycle)
if _rules_config is not None:
    BREATHE_BUTTONS = {
        r["alias"]: (r["device"], r["ledId"], _color_tuple(r["color"]), r["cycleSeconds"])
        for r in _rules_of_type("breathe")
    }
else:
    BREATHE_BUTTONS = {
        # "standbyButton": (DEFAULT_DEVICE, 3, CYAN, 2.0),
    }

_breathe_stop_events = {}
_BREATHE_LEVELS = (0, 1, 2, 3, 2, 1)  # up to full brightness, back down, loop


def breathe_loop(device, led_id, color, cycle_seconds, stop_event):
    step_seconds = cycle_seconds / len(_BREATHE_LEVELS)
    i = 0
    while not stop_event.is_set():
        level = _BREATHE_LEVELS[i % len(_BREATHE_LEVELS)]
        set_led(device, led_id, dim(color, level / 3))
        i += 1
        stop_event.wait(step_seconds)


def make_breathe_handler(alias, device, led_id, color, cycle_seconds):
    def handler(pressed):
        old_event = _breathe_stop_events.pop(alias, None)
        if old_event is not None:
            old_event.set()  # Stop any breathe loop already running for this button.

        if pressed:
            stop_event = threading.Event()
            _breathe_stop_events[alias] = stop_event
            threading.Thread(
                target=breathe_loop,
                args=(device, led_id, color, cycle_seconds, stop_event),
                daemon=True,
            ).start()
        else:
            set_led(device, led_id, resting_color(device, led_id))
    return handler


for _alias, (_device, _led_id, _color, _cycle_seconds) in BREATHE_BUTTONS.items():
    bridge.on_button(_alias)(make_breathe_handler(_alias, _device, _led_id, _color, _cycle_seconds))

# LEDs that only light up while a "modifier" button AND a "trigger" button
# are BOTH held at the same time - two aliases combined into one condition,
# off as soon as either one releases:
#   (modifier alias, trigger alias) -> (device name, LED id, color)
if _rules_config is not None:
    COMBO_LEDS = {
        (r["modifierAlias"], r["triggerAlias"]): (r["device"], r["ledId"], _color_tuple(r["color"]))
        for r in _rules_of_type("combo")
    }
else:
    COMBO_LEDS = {
        # ("shiftButton", "fireButton"): (DEFAULT_DEVICE, 4, RED),
    }

_combo_pressed = {}  # alias -> bool, shared by every combo that alias appears in


def make_combo_handler(alias):
    def handler(pressed):
        _combo_pressed[alias] = pressed
        for (modifier_alias, trigger_alias), (device, led_id, color) in COMBO_LEDS.items():
            if alias not in (modifier_alias, trigger_alias):
                continue
            both_held = _combo_pressed.get(modifier_alias, False) and _combo_pressed.get(trigger_alias, False)
            set_led(device, led_id, color if both_held else OFF)
    return handler


_combo_aliases = set()
for _modifier_alias, _trigger_alias in COMBO_LEDS.keys():
    _combo_aliases.add(_modifier_alias)
    _combo_aliases.add(_trigger_alias)
for _alias in _combo_aliases:
    bridge.on_button(_alias)(make_combo_handler(_alias))

# For a real 2-way switch wired as two separate SUSTAINED raw buttons (one
# alias reads held while flipped one way, the other while flipped the
# other way, never both/neither) - the LED shows whichever one is
# presently held. See the file header for why this differs from
# TOGGLE_BUTTONS/BUTTON_LEDS/COMBO_LEDS:
#   (on alias, off alias) -> (device name, LED id, color while "on" alias
#                              held, color while "off" alias held)
if _rules_config is not None:
    SWITCH_LEDS = {
        (r["onAlias"], r["offAlias"]): (r["device"], r["ledId"], _color_tuple(r["colorOn"]), _color_tuple(r["colorOff"]))
        for r in _rules_of_type("switch")
    }
else:
    SWITCH_LEDS = {
        # ("gearDownButton", "gearUpButton"): (DEFAULT_DEVICE, 5, GREEN, RED),
    }

_switch_pressed = {}  # alias -> bool, shared by every switch pair that alias appears in

# Where each SWITCH_LEDS pair's last known position ("on", "off", or
# "neutral") is remembered across script restarts - a script restart (or
# first connect) doesn't see a fresh HID report for a switch that hasn't
# moved since, so without this the LED would default to off/self-test
# until the switch is physically flipped again, silently disagreeing with
# where it's actually sitting (2026-07-25, same reasoning as
# TOGGLE_STATE_FILE/ONOFF_STATE_FILE above).
SWITCH_STATE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "virpil_led_switch_state.json")


def _load_switch_state():
    try:
        with open(SWITCH_STATE_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}  # No saved state yet, or the file's corrupt - start fresh.


def _save_switch_state():
    try:
        with open(SWITCH_STATE_FILE, "w", encoding="utf-8") as f:
            json.dump(_switch_last_position, f)
    except OSError:
        pass  # Not worth crashing the script over a failed save.


_switch_last_position = _load_switch_state()  # key -> "on" / "off" / "neutral"


def make_switch_handler(alias):
    def handler(pressed):
        _switch_pressed[alias] = pressed
        for (on_alias, off_alias), (device, led_id, color_on, color_off) in SWITCH_LEDS.items():
            if alias not in (on_alias, off_alias):
                continue
            key = f"{on_alias}|{off_alias}"
            if _switch_pressed.get(on_alias, False):
                set_led(device, led_id, color_on)
                _switch_last_position[key] = "on"
            elif _switch_pressed.get(off_alias, False):
                set_led(device, led_id, color_off)
                _switch_last_position[key] = "off"
            else:
                # A genuine 3-position switch with a neutral detent - the
                # panel's own firmware used to just leave the LED off here
                # (2026-07-25), so match that instead of leaving whatever
                # color_on/color_off last showed stuck on screen.
                set_led(device, led_id, OFF)
                _switch_last_position[key] = "neutral"
            _save_switch_state()
    return handler


_switch_aliases = set()
for _on_alias, _off_alias in SWITCH_LEDS.keys():
    _switch_aliases.add(_on_alias)
    _switch_aliases.add(_off_alias)
for _alias in _switch_aliases:
    bridge.on_button(_alias)(make_switch_handler(_alias))

# Restore each switch pair's LED to its last known position at script
# start, before any live button event has had a chance to arrive - see
# SWITCH_STATE_FILE's own comment above for why this matters.
for (_on_alias, _off_alias), (_device, _led_id, _color_on, _color_off) in SWITCH_LEDS.items():
    _last = _switch_last_position.get(f"{_on_alias}|{_off_alias}")
    if _last == "on":
        set_led(_device, _led_id, _color_on)
    elif _last == "off":
        set_led(_device, _led_id, _color_off)
    elif _last == "neutral":
        set_led(_device, _led_id, OFF)

# A brief flash of color on press, then back to whatever the LED was
# already showing (resting_color, snapshotted BEFORE the flash - see
# chase_loop's own comment on why "read fresh at the end" would just give
# back the flash color itself) - confirmation feedback for a genuinely
# momentary button where nothing should stay lit afterward:
#   alias name -> (device name, LED id, color, seconds to hold the flash)
if _rules_config is not None:
    PULSE_BUTTONS = {
        r["alias"]: (r["device"], r["ledId"], _color_tuple(r["color"]), r["duration"])
        for r in _rules_of_type("pulse")
    }
else:
    PULSE_BUTTONS = {
        # "lightsOnButton": (DEFAULT_DEVICE, 7, WHITE, 0.3),
    }


_pulse_timers = {}  # (device, LED id) -> Timer, shared by every alias that pulses that same LED


def make_pulse_handler(device, led_id, color, duration):
    def handler(pressed):
        if not pressed:
            return
        key = (device, led_id)
        old_timer = _pulse_timers.pop(key, None)
        if old_timer is not None:
            old_timer.cancel()  # A pulse for this same LED (e.g. the opposite direction) is mid-flight - let this one win.

        # Snapshot BEFORE touching the LED - if two aliases share one LED
        # (e.g. power increase/decrease pulsing the same power-toggle LED),
        # this is what makes each press correctly capture the LED's REAL
        # current resting color (its toggle state), not a stale one from
        # whenever the previous pulse started.
        previous_color = resting_color(device, led_id)
        set_led(device, led_id, color)

        def revert():
            _pulse_timers.pop(key, None)
            set_led(device, led_id, previous_color)

        timer = threading.Timer(duration, revert)
        timer.daemon = True
        _pulse_timers[key] = timer
        timer.start()
    return handler


for _alias, (_device, _led_id, _color, _duration) in PULSE_BUTTONS.items():
    bridge.on_button(_alias)(make_pulse_handler(_device, _led_id, _color, _duration))

# Like CHASE_BUTTONS, but for a MOMENTARY switch pair instead of one held
# button: pressing aliasA runs a chase through its own list of LEDs in
# colorA, pressing aliasB runs a (possibly completely different) list of
# LEDs in colorB - each for a fixed number of full passes (loops), then
# stops on its own and restores every touched LED, instead of waiting for
# a release that (on a momentary switch) already happened by the time
# anyone would see the animation. The two directions don't have to share
# any LEDs at all (2026-07-25: generalized from an earlier version that
# assumed both directions were the same list reversed - true for doors,
# not for landing gear, which chases a totally different button pair
# depending on whether it's retracting or extending):
#   alias A -> (alias B, device name, LED ids to chase for A, LED ids to
#                chase for B, color for A, color for B, seconds between
#                steps, number of full passes)
if _rules_config is not None:
    DOOR_CHASE = {
        r["aliasA"]: (
            r["aliasB"], r["device"], r["ledIdsA"], r["ledIdsB"],
            _color_tuple(r["colorA"]), _color_tuple(r["colorB"]),
            r["interval"], r["loops"],
        )
        for r in _rules_of_type("doorchase")
    }
else:
    DOOR_CHASE = {
        # "doorOpenButton": ("doorCloseButton", DEFAULT_DEVICE, [1, 2, 3, 4], [4, 3, 2, 1], GREEN, RED, 0.15, 2),
    }


_door_chase_stop_events = {}  # rule key -> Event, shared by BOTH aliases of one rule

# A real mechanical switch (like the landing gear / VTOL toggles this was
# built for - see the file's own gear/VTOL rules) can bounce for a few
# milliseconds while it physically makes/breaks contact mid-flip - each
# bounce is a genuine, real transition as far as Windows/DeviceManager is
# concerned (2026-07-25: confirmed edge-triggered, not the cause), so it
# can fire a burst of press events for BOTH aliases of one switch within
# milliseconds of each other. Without this guard, each one would cancel
# the last chase and restart a new one before it ever finished, leaving
# the LEDs stuck wherever the bounce happened to stop, instead of settling
# on the switch's actual final resting position. Any press for the same
# rule within this many seconds of the last one is ignored outright.
DOOR_CHASE_DEBOUNCE_SECONDS = 0.25

_door_chase_last_trigger = {}  # rule key -> time.monotonic() of the last press that was actually acted on


def door_chase_run(device, led_ids, color, interval, loops, stop_event):
    # Snapshot before animating, same reasoning as chase_loop() above.
    original_colors = {led_id: resting_color(device, led_id) for led_id in led_ids}

    previous_led = None
    for _lap in range(loops):
        for led_id in led_ids:
            if stop_event.is_set():
                return  # A newer press for this same rule pre-empted us - let IT restore the LEDs.
            if previous_led is not None:
                set_led(device, previous_led, OFF)
            set_led(device, led_id, color)
            previous_led = led_id
            stop_event.wait(interval)
    if previous_led is not None:
        set_led(device, previous_led, OFF)

    for led_id, color_value in original_colors.items():
        set_led(device, led_id, color_value)


def make_door_chase_handler(key, device, led_ids, color, interval, loops):
    def handler(pressed):
        if not pressed:
            return
        now = time.monotonic()
        if now - _door_chase_last_trigger.get(key, 0.0) < DOOR_CHASE_DEBOUNCE_SECONDS:
            return  # Almost certainly switch bounce, not a real second flip - ignore it.
        _door_chase_last_trigger[key] = now

        old_event = _door_chase_stop_events.pop(key, None)
        if old_event is not None:
            old_event.set()  # A chase for this rule (either direction) is still running - cancel it first.

        stop_event = threading.Event()
        _door_chase_stop_events[key] = stop_event
        threading.Thread(
            target=door_chase_run, args=(device, led_ids, color, interval, loops, stop_event), daemon=True
        ).start()
    return handler


for _alias_a, (_alias_b, _device, _led_ids_a, _led_ids_b, _color_a, _color_b, _interval, _loops) in DOOR_CHASE.items():
    _key = f"{_alias_a}|{_alias_b}"
    bridge.on_button(_alias_a)(make_door_chase_handler(_key, _device, _led_ids_a, _color_a, _interval, _loops))
    bridge.on_button(_alias_b)(make_door_chase_handler(_key, _device, _led_ids_b, _color_b, _interval, _loops))

# While a single alias is held, EVERY LED in a given list floods to one
# warning color at once; each is individually restored to whatever it was
# showing right before (snapshotted the instant the alias goes down, same
# reasoning as chase_loop's own snapshot) the moment it's released:
#   alias name -> (device name, list of LED ids, color while held)
if _rules_config is not None:
    ALARM_LEDS = {
        r["alias"]: (r["device"], r["ledIds"], _color_tuple(r["color"]))
        for r in _rules_of_type("alarm")
    }
else:
    ALARM_LEDS = {
        # "masterArmButton": (DEFAULT_DEVICE, [1, 2, 3, 4, 5, 6, 7, 8, 9, 10], RED),
    }

_alarm_snapshots = {}


def make_alarm_handler(alias, device, led_ids, color):
    def handler(pressed):
        if pressed:
            _alarm_snapshots[alias] = {led_id: resting_color(device, led_id) for led_id in led_ids}
            for led_id in led_ids:
                set_led(device, led_id, color)
        else:
            snapshot = _alarm_snapshots.pop(alias, {})
            for led_id, color_value in snapshot.items():
                set_led(device, led_id, color_value)
    return handler


for _alias, (_device, _led_ids, _color) in ALARM_LEDS.items():
    bridge.on_button(_alias)(make_alarm_handler(_alias, _device, _led_ids, _color))

# Like SWITCH_LEDS, but for a MOMENTARY switch pair instead of a sustained
# one: two separate press-only aliases, one that SETS the LED to "on", one
# that SETS it to "off". A momentary press carries no held state to read
# back (unlike SWITCH_LEDS), so the last state is persisted to disk
# (ONOFF_STATE_FILE) instead, same reasoning as TOGGLE_BUTTONS:
#   (on alias, off alias) -> (device name, LED id, color when "on", color
#                              when "off")
if _rules_config is not None:
    ONOFF_LEDS = {
        (r["onAlias"], r["offAlias"]): (r["device"], r["ledId"], _color_tuple(r["colorOn"]), _color_tuple(r["colorOff"]))
        for r in _rules_of_type("onoff")
    }
else:
    ONOFF_LEDS = {
        # ("cruiseOnButton", "cruiseOffButton"): (DEFAULT_DEVICE, 5, GREEN, OFF),
    }

ONOFF_STATE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "virpil_led_onoff_state.json")


def _load_onoff_state():
    try:
        with open(ONOFF_STATE_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}  # No saved state yet, or the file's corrupt - start fresh.


def _save_onoff_state():
    try:
        with open(ONOFF_STATE_FILE, "w", encoding="utf-8") as f:
            json.dump(_onoff_is_on, f)
    except OSError:
        pass  # Not worth crashing the script over a failed save.


_onoff_is_on = _load_onoff_state()


def make_onoff_handler(key, device, led_id, color_on, color_off, is_on):
    def handler(pressed):
        if not pressed:
            return
        set_led(device, led_id, color_on if is_on else color_off)
        _onoff_is_on[key] = is_on
        _save_onoff_state()
    return handler


for (_on_alias, _off_alias), (_device, _led_id, _color_on, _color_off) in ONOFF_LEDS.items():
    _key = f"{_on_alias}|{_off_alias}"
    bridge.on_button(_on_alias)(make_onoff_handler(_key, _device, _led_id, _color_on, _color_off, True))
    bridge.on_button(_off_alias)(make_onoff_handler(_key, _device, _led_id, _color_on, _color_off, False))
    if _key in _onoff_is_on:
        set_led(_device, _led_id, _color_on if _onoff_is_on[_key] else _color_off)

# Like ONOFF_LEDS, but blinks for a few seconds right after the press
# instead of switching color instantly, then settles into a steady color
# for whichever state it just switched to - a "something just changed"
# confirmation that still leaves a correct steady readout behind, unlike
# PULSE_BUTTONS (which reverts to whatever was showing BEFORE the press)
# or BLINK_BUTTONS (which blinks only while held, forever, not a fixed
# duration on a momentary press). Each direction gets its OWN blink color
# (alternating with off) - not just a different STEADY color once the
# blink ends - so e.g. landing gear can blink green going down and red
# going up on the very same LED (2026-07-25). colorOn/colorOff are each
# optional (null in the rules file) - when omitted, that direction settles
# back to whatever the LED was showing right BEFORE the blink started
# (snapshotted like PULSE_BUTTONS/chase_loop do) instead of a fixed color,
# and nothing is persisted for it - for a control with no real "state" of
# its own worth remembering (2026-07-25: landing gear/VTOL, whose actual
# position is already visible on the physical lever - the blink is purely
# a "something changed" confirmation, not a status readout):
#   (on alias, off alias) -> (device name, LED id, blink color while "on"
#                              alias fires, blink color while "off" alias
#                              fires, seconds between blink steps, seconds
#                              to blink for, steady color once settled
#                              "on" (or None), steady color once settled
#                              "off" (or None))
if _rules_config is not None:
    BLINK_ONOFF_LEDS = {
        (r["onAlias"], r["offAlias"]): (
            r["device"], r["ledId"], _color_tuple(r["blinkColorOn"]), _color_tuple(r["blinkColorOff"]),
            r["blinkInterval"], r["blinkDuration"],
            _color_tuple(r["colorOn"]) if r.get("colorOn") is not None else None,
            _color_tuple(r["colorOff"]) if r.get("colorOff") is not None else None,
        )
        for r in _rules_of_type("blinkonoff")
    }
else:
    BLINK_ONOFF_LEDS = {
        # ("gearDownButton", "gearUpButton"): (DEFAULT_DEVICE, 8, GREEN, RED, 0.3, 2.0, None, None),
    }

_blinkonoff_stop_events = {}
BLINKONOFF_STATE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "virpil_led_blinkonoff_state.json")

# Same bounce concern as DOOR_CHASE above (this type was reused for
# landing gear / VTOL for exactly that reason - a plain blink survives a
# bounced restart far less jarringly than a chase does, but still deserves
# the same debounce guard rather than relying on that alone).
BLINKONOFF_DEBOUNCE_SECONDS = 0.25

_blinkonoff_last_trigger = {}


def _load_blinkonoff_state():
    try:
        with open(BLINKONOFF_STATE_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}  # No saved state yet, or the file's corrupt - start fresh.


def _save_blinkonoff_state():
    try:
        with open(BLINKONOFF_STATE_FILE, "w", encoding="utf-8") as f:
            json.dump(_blinkonoff_is_on, f)
    except OSError:
        pass  # Not worth crashing the script over a failed save.


_blinkonoff_is_on = _load_blinkonoff_state()


def run_timed_blink(device, led_id, blink_color, interval, duration, final_color, stop_event):
    end_time = time.monotonic() + duration
    show_color = True
    while time.monotonic() < end_time and not stop_event.is_set():
        set_led(device, led_id, blink_color if show_color else OFF)
        show_color = not show_color
        stop_event.wait(interval)
    if not stop_event.is_set():
        set_led(device, led_id, final_color)


def make_blinkonoff_handler(key, device, led_id, blink_color, interval, duration, final_color, turning_on):
    def handler(pressed):
        if not pressed:
            return
        now = time.monotonic()
        if now - _blinkonoff_last_trigger.get(key, 0.0) < BLINKONOFF_DEBOUNCE_SECONDS:
            return  # Almost certainly switch bounce, not a real second flip - ignore it.
        _blinkonoff_last_trigger[key] = now

        old_event = _blinkonoff_stop_events.pop(key, None)
        if old_event is not None:
            old_event.set()  # Cancel any blink already running for this LED - the newer press wins.

        # None means "no real state to remember" - snapshot what the LED
        # looked like right THIS INSTANT, before the blink below touches
        # it, same reasoning as chase_loop's own snapshot (has to happen
        # here, not inside the thread, or by the time it ran resting_color
        # would just see the blink's own most recent color).
        settle_color = final_color if final_color is not None else resting_color(device, led_id)

        stop_event = threading.Event()
        _blinkonoff_stop_events[key] = stop_event
        threading.Thread(
            target=run_timed_blink,
            args=(device, led_id, blink_color, interval, duration, settle_color, stop_event),
            daemon=True,
        ).start()

        if final_color is not None:
            _blinkonoff_is_on[key] = turning_on
            _save_blinkonoff_state()
    return handler


for (_on_alias, _off_alias), (_device, _led_id, _blink_on, _blink_off, _interval, _duration, _color_on, _color_off) in BLINK_ONOFF_LEDS.items():
    _key = f"{_on_alias}|{_off_alias}"
    bridge.on_button(_on_alias)(
        make_blinkonoff_handler(_key, _device, _led_id, _blink_on, _interval, _duration, _color_on, True)
    )
    bridge.on_button(_off_alias)(
        make_blinkonoff_handler(_key, _device, _led_id, _blink_off, _interval, _duration, _color_off, False)
    )
    # Restore the steady color for whatever it last settled to - no need
    # to replay the blink itself on script startup. Skipped for a rule with
    # no real state to remember (colorOn/colorOff both None) - and a STALE
    # saved entry from before a rule switched to that mode (e.g. landing
    # gear/VTOL, which used to persist a state) must be ignored too, not
    # just a fresh empty file, or set_led() below would crash trying to
    # unpack None as a color.
    if _color_on is not None and _color_off is not None and _key in _blinkonoff_is_on:
        set_led(_device, _led_id, _color_on if _blinkonoff_is_on[_key] else _color_off)

# One entry per button alias that should blink briefly (alternating with
# off) a few times, then revert to whatever the LED was showing right
# before - like PULSE_BUTTONS's single flash, but blinking for a moment
# instead, for something worth catching the eye a bit more (2026-07-25:
# added for an ATC hangar request, sharing an LED with something else
# entirely rather than needing its own dedicated one). debounce is
# per-rule and optional (0 if omitted) - a real mechanical switch/button
# (like the ATC request) benefits from one to swallow contact bounce, but
# a rotary encoder's rapid clicks (2026-07-25: power allocation dials
# reusing this same type) are all genuine distinct presses that shouldn't
# get dropped, just cancelled-and-restarted cleanly on each one. Multiple
# aliases sharing one LED (e.g. an increase/decrease pair) correctly
# cancel EACH OTHER's in-flight blink, not just their own, since both are
# tracked by (device, LED id) rather than by alias:
#   alias name -> (device name, LED id, color, seconds between blink
#                   steps, seconds to blink for, debounce seconds)
if _rules_config is not None:
    BLINK_PULSE_BUTTONS = {
        r["alias"]: (
            r["device"], r["ledId"], _color_tuple(r["color"]), r["interval"], r["duration"],
            r.get("debounce", 0.0),
        )
        for r in _rules_of_type("blinkpulse")
    }
else:
    BLINK_PULSE_BUTTONS = {
        # "atcHangarRequest": (DEFAULT_DEVICE, 7, CYAN, 0.2, 1.5, 0.25),
    }

_blinkpulse_stop_events = {}  # (device, LED id) -> Event, shared by every alias that shares that LED
_blinkpulse_last_trigger = {}


def make_blinkpulse_handler(device, led_id, color, interval, duration, debounce):
    def handler(pressed):
        if not pressed:
            return
        key = (device, led_id)
        now = time.monotonic()
        if debounce > 0 and now - _blinkpulse_last_trigger.get(key, 0.0) < debounce:
            return  # Debounce, same reasoning as DOOR_CHASE/BLINK_ONOFF_LEDS above.
        _blinkpulse_last_trigger[key] = now

        old_event = _blinkpulse_stop_events.pop(key, None)
        if old_event is not None:
            old_event.set()  # Cancel any blink already running for this LED - the newer press wins.

        # Snapshot BEFORE the blink touches the LED, same reasoning as
        # chase_loop/PULSE_BUTTONS above.
        previous_color = resting_color(device, led_id)
        stop_event = threading.Event()
        _blinkpulse_stop_events[key] = stop_event
        threading.Thread(
            target=run_timed_blink,
            args=(device, led_id, color, interval, duration, previous_color, stop_event),
            daemon=True,
        ).start()
    return handler


for _alias, (_device, _led_id, _color, _interval, _duration, _debounce) in BLINK_PULSE_BUTTONS.items():
    bridge.on_button(_alias)(make_blinkpulse_handler(_device, _led_id, _color, _interval, _duration, _debounce))

# A single button alias that turns every LED this script knows about, on
# EVERY device, off - and stops any running blink/chase animation - a
# quick reset if something got left in a weird state. Set to None to
# disable this feature.
#   Nothing device-specific here or anywhere else in this script, by the
#   way - an input alias can point at a button on ANY device, not just a
#   panel with LEDs, so this "all off" button (or any BUTTON_LEDS/
#   TOGGLE_BUTTONS entry) works exactly the same wired to a button on a
#   different joystick entirely. Wire it up like any other alias in the
#   Scripts panel; nothing here needs to change.
if _rules_config is not None:
    ALL_OFF_BUTTON = _rules_config.get("allOffButtonAlias") or None
else:
    ALL_OFF_BUTTON = None  # e.g. "allOffButton"


def _collect_all_device_led_pairs():
    pairs = set(INITIAL_LED_COLORS.keys())
    pairs.update((device, led_id) for device, led_id, _, _ in BUTTON_LEDS.values())
    for _device, _on_led_ids, _ in ON_BUTTONS.values():
        pairs.update((_device, led_id) for led_id in _on_led_ids)
    pairs.update((device, led_id) for device, led_id, _, _, _, _ in BLINK_BUTTONS.values())
    pairs.update((device, led_id) for device, led_id, _, _ in TOGGLE_BUTTONS.values())
    pairs.update((device, led_id) for device, led_id, _ in AXIS_LEDS.values())
    pairs.update((device, led_id) for device, led_id, _, _ in BREATHE_BUTTONS.values())
    pairs.update((device, led_id) for device, led_id, _ in COMBO_LEDS.values())
    for device, led_ids, _, _ in CHASE_BUTTONS.values():
        pairs.update((device, led_id) for led_id in led_ids)
    pairs.update((device, led_id) for device, led_id, _, _ in SWITCH_LEDS.values())
    pairs.update((device, led_id) for device, led_id, _, _ in PULSE_BUTTONS.values())
    for _alias_b, device, led_ids_a, led_ids_b, _, _, _, _ in DOOR_CHASE.values():
        pairs.update((device, led_id) for led_id in led_ids_a)
        pairs.update((device, led_id) for led_id in led_ids_b)
    pairs.update((device, led_id) for device, led_id, _, _ in ONOFF_LEDS.values())
    pairs.update((device, led_id) for device, led_id, _, _, _, _, _, _ in BLINK_ONOFF_LEDS.values())
    for device, led_ids, _ in ALARM_LEDS.values():
        pairs.update((device, led_id) for led_id in led_ids)
    pairs.update((device, led_id) for device, led_id, _, _, _, _ in BLINK_PULSE_BUTTONS.values())
    return pairs


def turn_everything_off():
    for stop_event in list(_blink_stop_events.values()):
        stop_event.set()
    _blink_stop_events.clear()
    for stop_event in list(_chase_stop_events.values()):
        stop_event.set()
    _chase_stop_events.clear()
    for stop_event in list(_breathe_stop_events.values()):
        stop_event.set()
    _breathe_stop_events.clear()
    for stop_event in list(_blinkonoff_stop_events.values()):
        stop_event.set()
    _blinkonoff_stop_events.clear()
    for stop_event in list(_door_chase_stop_events.values()):
        stop_event.set()
    _door_chase_stop_events.clear()
    for stop_event in list(_blinkpulse_stop_events.values()):
        stop_event.set()
    _blinkpulse_stop_events.clear()
    for timer in list(_pulse_timers.values()):
        timer.cancel()
    _pulse_timers.clear()

    for device, led_id in _collect_all_device_led_pairs():
        set_led(device, led_id, OFF)


if ALL_OFF_BUTTON:
    def _handle_all_off(pressed):
        if pressed:
            turn_everything_off()
    bridge.on_button(ALL_OFF_BUTTON)(_handle_all_off)

# LEDs to set to a fixed color as soon as this script starts - this is the
# script's own replacement for whatever fixed "power-on" color the VPC
# Config Tool used to assign, for any LED whose Config Tool color entry you
# cleared (see the file header - the Config Tool's own color reasserts
# itself on the button's next physical press/release otherwise, fighting
# with whatever this script sets). This only takes effect once THIS
# SCRIPT starts, not when the panel itself powers on - there's still a
# brief window between plugging the panel in and Nexus starting this
# script where it shows the Config Tool's un-set default (normally off).
#   (device name, LED id) -> color
if _rules_config is not None:
    INITIAL_LED_COLORS = {
        (entry["device"], entry["ledId"]): _color_tuple(entry["color"])
        for entry in _rules_config.get("initialColors", [])
    }
else:
    INITIAL_LED_COLORS = {
        # (DEFAULT_DEVICE, 1): RED,
        # (DEFAULT_DEVICE, 2): GREEN,
        # (DEFAULT_DEVICE, 5): CYAN,
    }

# If True, briefly lights every LED this script knows about, one at a
# time, before settling into INITIAL_LED_COLORS - a "self-test" sweep like
# real cockpit panels do on power-up. Purely cosmetic; set to False to
# skip straight to INITIAL_LED_COLORS.
SELF_TEST_ON_START = _rules_config["selfTestOnStart"] if _rules_config is not None else True
SELF_TEST_COLOR = WHITE
SELF_TEST_STEP_SECONDS = 0.15

# LEDs owned by a rule type that already restored its own remembered state
# earlier in this file (SWITCH_LEDS/ONOFF_LEDS/BLINK_ONOFF_LEDS) must NOT
# be touched by either the self-test sweep below OR the INITIAL_LED_COLORS
# loop further down - both run AFTER those restores, so blindly sweeping
# or re-applying a fixed color to one of their LEDs would silently
# overwrite the state it just recalled, on EVERY script start, making it
# look like "memory" never worked at all (2026-07-25 - this was exactly
# that bug for landing gear/VTOL/cruise/night vision: the restore ran
# correctly, then self-test's own white-flash-then-off immediately undid
# it before anyone ever saw it).
_stateful_led_pairs = set()
_stateful_led_pairs.update((device, led_id) for device, led_id, _, _ in SWITCH_LEDS.values())
_stateful_led_pairs.update((device, led_id) for device, led_id, _, _ in ONOFF_LEDS.values())
_stateful_led_pairs.update(
    (device, led_id)
    for device, led_id, _, _, _, _, color_on, color_off in BLINK_ONOFF_LEDS.values()
    if color_on is not None and color_off is not None
)


def run_self_test():
    for device, led_id in sorted(_collect_all_device_led_pairs() - _stateful_led_pairs):
        set_led(device, led_id, SELF_TEST_COLOR)
        time.sleep(SELF_TEST_STEP_SECONDS)
        set_led(device, led_id, OFF)


if SELF_TEST_ON_START:
    run_self_test()

for (_device, _led_id), _color in INITIAL_LED_COLORS.items():
    if (_device, _led_id) in _stateful_led_pairs:
        continue
    set_led(_device, _led_id, _color)

bridge.run()
