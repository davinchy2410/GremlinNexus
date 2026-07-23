# Writing scripts for Gremlin Nexus

This is the deeper reference for the Scripts panel - if you just want the
5-minute quickstart, see `README.md` instead. This guide walks through how
the API actually works, using the two example scripts in `examples/` as
real, working code you can read alongside it.

## The mental model

A script doesn't "control" your devices directly. It only ever talks to
Nexus over a small local connection, using **named channels**:

- It can **listen** to a named input (`@bridge.on_axis("rudder")`) - Nexus
  calls your function every time that named channel changes.
- It can **write** to a named output (`bridge.set_axis("toebrakeOutput",
  0.7)`) - Nexus treats that exactly like a real device axis moving, so any
  binding in Profiles that targets "Nexus Scripts" fires normally.

The name itself (`"rudder"`, `"toebrakeOutput"`, ...) means nothing to
Nexus or to Python - it's just a label. **You** decide what a name means by
wiring it up in the Scripts panel: an input alias points a name at a real
device's axis/button, an output alias points a name at a channel of the
shared "Nexus Scripts" virtual device. The script only ever sees the name,
never a device path or a raw HID index.

This is why the name has to match **exactly** between your code and the
Scripts panel - `@bridge.on_axis("rudder")` and an input alias named
`"Rudder"` (capital R) are two different names as far as either side is
concerned, and neither will ever notice the other exists. The Scripts panel
warns you (orange name, hover for why) when an alias's name doesn't turn up
anywhere in the script's own code, and offers a "Suggested" dropdown
pre-filled with the exact names the script uses - use it instead of typing
the name from memory when you can.

## Value convention: everything is 0.0-1.0

Every axis value - in (`on_axis`'s argument) and out (`set_axis`'s second
argument) - is a plain float from `0.0` to `1.0`. There's no negative half;
`0.0` and `1.0` are the two physical extremes of the axis and `0.5` is
center. Button values are always Python bools (`True`/`False`).

If you're used to another axis system that's centered at 0 (Joystick
Gremlin's own plugin API, older SDKs, etc.), you'll need to re-center it
yourself: `centered = value * 2.0 - 1.0` turns 0.0-1.0 into -1.0-1.0, and
`value = (centered + 1.0) / 2.0` goes back. `examples/virtual_toebrake.py`
does exactly this for its rudder input - see "Walking through an example"
below.

## Minimal shape of a script

```python
import nexus_bridge as bridge

@bridge.on_axis("rudder")
def handle_rudder(value):
    bridge.set_axis("toebrakeOutput", value)

bridge.run()
```

- `nexus_bridge` is pure standard library - nothing to `pip install`, and
  the embedded Python interpreter Nexus ships has no other packages
  available either.
- `bridge.run()` blocks forever, dispatching every incoming `on_axis`/
  `on_button` call as it arrives. A script only needs it if it *reacts* to
  input - one that only calls `set_axis`/`set_button` on its own timer
  (e.g. a clock, a random value generator) doesn't need `bridge.run()` at
  all, and can just loop with `time.sleep()` instead.
- Nothing stops you from running several scripts at once - each gets its
  own Python process and its own independent connection, so one script's
  aliases never collide with another's even if they happen to reuse the
  same name.

## Remembering state between calls

`on_axis`/`on_button` callbacks are one-shot - each call only gets *this*
event's value, with no memory of the last one. If your logic needs more
than one input at a time (e.g. "brake amount" *and* "which way is the
rudder pointing" to compute an output), keep your own state in a plain
Python variable at module scope and have every handler update it:

```python
state = {"rudder": 0.5, "brake": 0.0}

@bridge.on_axis("rudder")
def handle_rudder(value):
    state["rudder"] = value
    recompute()

@bridge.on_axis("brake")
def handle_brake(value):
    state["brake"] = value
    recompute()

def recompute():
    # both state["rudder"] and state["brake"] are always the latest
    # value here, regardless of which one just changed
    ...
```

This is the same pattern `examples/virtual_toebrake.py` uses - see below.

## Walking through an example: `rudder_curve.py`

The simplest possible real script - one input, one output, no state:

```python
@bridge.on_axis("rudder")
def handle_rudder(value):
    curved = value * value          # value is 0.0-1.0, so is curved
    bridge.set_axis("toebrakeOutput", curved)
```

Every time the "rudder" input alias's real axis moves, this runs once with
that axis's current 0.0-1.0 value, squares it (a softer response near
center, full deflection still reaches 1.0), and writes the result straight
back out as "toebrakeOutput". No state needed because the output only ever
depends on this one input's current value.

## Walking through an example: `virtual_toebrake.py`

A more involved one - two inputs combined into two outputs, ported from a
Joystick Gremlin community plugin (see the file's own header comment for
the original). The shape:

1. Two handlers (`handle_rudder`, `handle_brake_axis`) each just update
   `state["rudder"]`/`state["brake_axis"]` and call `update_virtual_brakes()`
   - neither one knows or cares about the other's value directly, they just
   trust `state` to hold the latest of both.
2. `update_virtual_brakes()` re-centers the rudder to -1.0..1.0 (see "Value
   convention" above - the original math assumes that range), then computes
   how much of the total brake amount goes to each side based on which way
   the rudder leans, and writes both `"toebrakeLeft"`/`"toebrakeRight"`
   outputs every time either input changes.

This is the general shape for "combine N inputs into M outputs": one shared
`state` dict, every input handler just updates its own slot and calls one
shared recompute function.

## Coming from Joystick Gremlin plugins?

Joystick Gremlin's own plugin system (`gremlin.user_plugin`,
`PhysicalInputVariable`/`VirtualInputVariable`, decorators bound to a
`ModeVariable`, writing straight to a `VJoyProxy`) is a **different, older
API** - a script written against it will not run here as-is. It'll fail
immediately with something like `ModuleNotFoundError: No module named
'gremlin'`, since that module doesn't exist in `nexus_bridge`.

Porting one over usually means:

- Replace `PhysicalInputVariable`/`VirtualInputVariable`/`ModeVariable`
  declarations with input/output aliases in the Scripts panel - there's no
  in-script config UI here, the panel *is* the config.
- Replace `@dec_something.axis(...)`/`.button(...)` decorators with
  `@bridge.on_axis("name")`/`@bridge.on_button("name")`.
- Replace `vj[...].axis(...).value = ...` with `bridge.set_axis("name",
  value)` (or `set_button` for buttons).
- Re-center any math that assumed a -1.0..1.0 axis range - see "Value
  convention" above.

`examples/virtual_toebrake.py` is a complete, real example of exactly this
port, if you want a template to follow.

## A word on trust

A script here runs as a normal Python process with the same access to your
PC as any other program - Nexus does not and cannot sandbox arbitrary
Python code. Only add scripts from people you trust, same as you would a
Joystick Gremlin plugin, a browser extension, or any other third-party
code. Use "View code" in the Scripts panel to read a script before adding
or running it if you're not sure.
