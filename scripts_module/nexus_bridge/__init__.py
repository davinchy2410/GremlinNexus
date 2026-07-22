"""nexus_bridge - Python SDK for Gremlin Nexus's Script Bridge (Fase 19).

Talks to Nexus over a localhost TCP socket, one JSON object per line.
Connection details (host/port/token) come from environment variables Nexus
sets before launching this script - never hardcode or prompt for them, and
never run this file directly outside of Nexus's own Scripts panel.

Minimal example:

    import nexus_bridge as bridge

    @bridge.on_axis("rudder")
    def handle_rudder(value):
        bridge.set_axis("toebrakeOutput", value)

    bridge.run()

Wire protocol (matches ScriptBridgeServer on the Nexus side):
    -> {"type": "auth", "token": "..."}
    <- {"type": "authResult", "success": true}
    -> {"type": "setAxis", "name": "<alias>", "value": 0.0-1.0}
    -> {"type": "setButton", "name": "<alias>", "pressed": true|false}
    <- {"type": "axisUpdate", "name": "<alias>", "value": 0.0-1.0}
    <- {"type": "buttonUpdate", "name": "<alias>", "pressed": true|false}

"<alias>" is a free-form name chosen by whoever wires up the Scripts panel's
input/output mapping for this script - nexus_bridge itself has no idea what
a name resolves to on the Nexus side, it only ever carries the string.
"""

import json
import os
import socket

__all__ = [
    "connect",
    "close",
    "run",
    "on_axis",
    "on_button",
    "set_axis",
    "set_button",
    "NexusBridgeError",
]


class NexusBridgeError(RuntimeError):
    """Raised when this script can't reach or authenticate with Nexus."""


class _Bridge:
    def __init__(self):
        self._socket = None
        self._file = None
        self._axis_handlers = {}
        self._button_handlers = {}

    @staticmethod
    def _required_env(name):
        value = os.environ.get(name)
        if not value:
            raise NexusBridgeError(
                f"{name} is not set - this script must be launched from Gremlin "
                "Nexus's Scripts panel, not run standalone."
            )
        return value

    def connect(self):
        if self._socket is not None:
            return  # Already connected - safe to call from set_axis()/set_button()/run() alike.

        host = self._required_env("NEXUS_BRIDGE_HOST")
        port = int(self._required_env("NEXUS_BRIDGE_PORT"))
        token = self._required_env("NEXUS_BRIDGE_TOKEN")

        sock = socket.create_connection((host, port), timeout=5)
        sock.settimeout(None)
        self._socket = sock
        self._file = sock.makefile("r", encoding="utf-8", newline="\n")

        self._send({"type": "auth", "token": token})
        reply = self._read_message()
        if reply is None or not reply.get("success"):
            self.close()
            raise NexusBridgeError("Nexus rejected this script's connection token.")

    def close(self):
        if self._file is not None:
            self._file.close()
            self._file = None
        if self._socket is not None:
            self._socket.close()
            self._socket = None

    def _send(self, message):
        line = json.dumps(message, separators=(",", ":")) + "\n"
        self._socket.sendall(line.encode("utf-8"))

    def _read_message(self):
        line = self._file.readline()
        if not line:
            return None  # Nexus closed the connection.
        try:
            return json.loads(line)
        except ValueError:
            return None  # Malformed line - ignored, matches ScriptBridgeServer's own tolerance.

    def on_axis(self, name):
        def decorator(func):
            self._axis_handlers.setdefault(name, []).append(func)
            return func
        return decorator

    def on_button(self, name):
        def decorator(func):
            self._button_handlers.setdefault(name, []).append(func)
            return func
        return decorator

    def set_axis(self, name, value):
        self.connect()
        # Nexus's own convention (see ButtonToAxisHandler) is 0.0-1.0 for a
        # 0-100% axis - clamped here so a script bug (e.g. a stray negative
        # or >1 value) can't send Nexus something out of range.
        clamped = max(0.0, min(1.0, float(value)))
        self._send({"type": "setAxis", "name": name, "value": clamped})

    def set_button(self, name, pressed):
        self.connect()
        self._send({"type": "setButton", "name": name, "pressed": bool(pressed)})

    def run(self):
        """Blocks forever, dispatching incoming axis/button updates to the
        matching @on_axis/@on_button handlers. Only needed by scripts that
        react to Nexus-side input - a script that only calls set_axis()/
        set_button() on its own timer doesn't need to call this at all."""
        self.connect()
        try:
            while True:
                message = self._read_message()
                if message is None:
                    break  # Nexus closed the connection (Scripts panel Stop, or app shutdown).
                msg_type = message.get("type")
                name = message.get("name")
                if msg_type == "axisUpdate":
                    for handler in self._axis_handlers.get(name, []):
                        handler(message.get("value"))
                elif msg_type == "buttonUpdate":
                    for handler in self._button_handlers.get(name, []):
                        handler(bool(message.get("pressed")))
        except OSError:
            pass  # Socket dropped - Nexus's own Scripts panel decides whether to relaunch.
        finally:
            self.close()


_bridge = _Bridge()

connect = _bridge.connect
close = _bridge.close
run = _bridge.run
on_axis = _bridge.on_axis
on_button = _bridge.on_button
set_axis = _bridge.set_axis
set_button = _bridge.set_button
