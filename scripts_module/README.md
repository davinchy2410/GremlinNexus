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

- `bridge.set_axis(name, value)` / `bridge.set_button(name, pressed)` - write
  to a named output channel.
- `@bridge.on_axis(name)` / `@bridge.on_button(name)` - react to a named
  input channel.
- A script that only calls `set_axis`/`set_button` on its own timer doesn't
  need `bridge.run()` at all - that's only for reacting to incoming updates.

The `name` strings above are whatever alias you've assigned that script's
input/output in the Scripts panel - see `examples/` for a complete script.
