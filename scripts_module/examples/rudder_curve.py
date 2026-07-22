"""Example Nexus Bridge script: applies a custom exponential curve to a
rudder axis before writing it back out as a toe-brake output.

Copy this file, rename it, and point the Scripts panel at your copy - this
one is just a starting point, not something you're expected to run as-is.
"""

import nexus_bridge as bridge


@bridge.on_axis("rudder")
def handle_rudder(value):
    # value is 0.0-1.0. A simple squared curve for a softer center feel.
    curved = value * value
    bridge.set_axis("toebrakeOutput", curved)


bridge.run()
