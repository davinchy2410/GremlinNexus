# Gremlin Nexus - Scripts Module

Optional, separately-downloaded add-on that lets Gremlin Nexus's **Scripts**
panel run your own Python scripts, each able to read/write named channels of
the "Nexus Scripts" virtual device alongside your real hardware.

## Installing

1. Download `ScriptsModule.zip` from the Gremlin Nexus [Releases](https://github.com/davinchy2410/GremlinNexus/releases) page.
2. Extract it so you end up with a `ScriptsModule` folder **next to `GremlinNexus.exe`**.
3. Open Gremlin Nexus -> Settings -> enable "Scripts (Beta)".
4. Open the new "Scripts" tab (Tools menu) and add your `.py` script.

You do **not** need Python installed on your PC - this module ships its own
embedded interpreter, used only for scripts launched from the Scripts panel.

## A word on trust

A script you add here runs as a normal Python process with the same access
to your PC as any other program you run - Nexus does not sandbox it in any
way (there's no reliable way to sandbox arbitrary Python code). Only add
scripts from people you trust, same as you would with a Joystick Gremlin
plugin, a browser extension, or any other third-party code. Use the "View
code" button in the Scripts panel to read a script's source before adding
or running it if you're not sure.

## Writing a script

```python
import nexus_bridge as bridge

@bridge.on_axis("rudder")
def handle_rudder(value):
    # value is 0.0-1.0
    bridge.set_axis("toebrakeOutput", value)

bridge.run()
```

`nexus_bridge` is pure Python standard library - nothing to `pip install`.
The `name` strings above are whatever alias you've assigned that script's
input/output in the Scripts panel.

**See [`SCRIPTING_GUIDE.md`](SCRIPTING_GUIDE.md)** for the full picture -
the value convention, keeping state across events, a walkthrough of both
example scripts' own math, and what to change if you're porting a Joystick
Gremlin plugin over.
