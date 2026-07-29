"""Standalone GUI editor for virpil_led_control.py's rules file.

Personal companion tool, NOT part of Nexus itself and NOT shipped in
ScriptsModule.zip - it edits scripts_module/examples/virpil_led_rules.json,
which virpil_led_control.py reads at startup if present (see that file's
own docstring, "Optional rules file" section). Nexus's own code is not
touched by any of this; the LED script would work identically without
this editor ever existing, just with hand-edited Python dicts instead.

Run with your own system Python, NOT the embedded interpreter Nexus bundles
for scripts (that one has no Tkinter):

    python tools/virpil_led_rules_editor.py

Reads alias names from dist/GremlinNexus/scripts_config.json (the same
file Nexus's own Scripts panel already writes) so you don't have to
retype alias names by hand - wire your buttons in Nexus first, then use
this editor to decide what each one does.

Multiple devices (2026-07-25): every rule/initial color belongs to a named
device (e.g. "panel", "aeromax"), each with its own VID/PID - LED numbers
are only unique WITHIN one physical Virpil device, so a second device
needs its own name to avoid colliding with the first one's LED 1, LED 2...
"""

import json
import os
import re
import subprocess
import threading
import time
import tkinter as tk
from tkinter import colorchooser, messagebox, ttk

# --- paths ------------------------------------------------------------
# Two different folder layouts this file can be run from:
#   Dev (this git repo):      <repo>/tools/virpil_led_rules_editor.py
#                              examples at <repo>/scripts_module/examples
#                              scripts_config.json at <repo>/dist/GremlinNexus/
#   Deployed (ScriptsModule.zip extracted next to GremlinNexus.exe):
#                              <install>/ScriptsModule/tools/virpil_led_rules_editor.py
#                              examples at <install>/ScriptsModule/examples (a sibling
#                              of tools/, no "scripts_module" wrapper folder)
#                              scripts_config.json at <install>/ (next to the exe,
#                              one level above the ScriptsModule folder itself)
# Detected by whether an "examples" folder sits directly next to tools/ -
# only true in the deployed layout.
TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
PARENT_DIR = os.path.dirname(TOOLS_DIR)
_deployed_examples = os.path.join(PARENT_DIR, "examples")
if os.path.isdir(_deployed_examples):
    EXAMPLES_DIR = _deployed_examples
    SCRIPTS_CONFIG_FILE = os.path.join(os.path.dirname(PARENT_DIR), "scripts_config.json")
    DEPLOYED_RULES_FILE = None
else:
    REPO_ROOT = PARENT_DIR
    EXAMPLES_DIR = os.path.join(REPO_ROOT, "scripts_module", "examples")
    SCRIPTS_CONFIG_FILE = os.path.join(REPO_ROOT, "dist", "GremlinNexus", "scripts_config.json")
    # Dev layout only: Nexus actually runs the script from this LOCAL
    # deployed copy, never from scripts_module/examples/ directly (see
    # sync_virpil_led_dev.ps1's own docs / Memory.md's "Editar un script
    # de ejemplo... no alcanza"). Mirroring every save here means Stop+Start
    # in Nexus's Scripts panel is enough - no manual sync step needed.
    DEPLOYED_RULES_FILE = os.path.join(REPO_ROOT, "dist", "ScriptsModule", "examples", "virpil_led_rules.json")
RULES_FILE = os.path.join(EXAMPLES_DIR, "virpil_led_rules.json")
VPC_LED_TOOL = r"C:\Program Files (x86)\VPC Software Suite\tools\VPC_LED_Control.exe"

# --- theme (matches Nexus's own Theme.qml palette) ---------------------
BASE = "#0b1021"
SURFACE0 = "#101830"
SURFACE1 = "#17203f"
SURFACE2 = "#202b52"
ACCENT = "#00f3ff"
ACCENT_HOVER = "#7dfbff"
TEXT = "#d8e6f3"
SUBTEXT = "#7891ab"
SUCCESS = "#2ee6a8"
WARNING = "#ff9500"
DANGER = "#ff3b30"

ACTION_TYPES = [
    "color", "on", "blink", "chase", "toggle", "breathe", "axis", "combo",
    "pulse", "switch", "onoff", "blinkonoff", "doorchase", "alarm", "blinkpulse",
]
ACTION_LABELS = {
    "color": "Color fijo (al apretar / soltar)",
    "on": "Encendido simple (queda prendido)",
    "blink": "Blink (parpadeo)",
    "chase": "Chase (recorrido por varios LEDs)",
    "toggle": "Toggle / Latch",
    "breathe": "Breathe (respiracion de brillo)",
    "axis": "Eje -> brillo",
    "combo": "Combo (2 botones a la vez)",
    "pulse": "Flash momentaneo (vuelve a lo anterior)",
    "switch": "Switch sostenido (2 alias, sin memoria en disco)",
    "onoff": "On/Off momentaneo (2 alias, con memoria en disco)",
    "blinkonoff": "Blink On/Off (2 alias, parpadea y despues fija o vuelve a lo anterior)",
    "doorchase": "Chase de 2 direcciones (2 alias, cada uno su propio recorrido)",
    "alarm": "Alarma de panel completo (1 alias, varios LEDs a la vez)",
    "blinkpulse": "Blink momentaneo (1 alias, parpadea y vuelve a lo anterior)",
}
DEFAULT_COLORS = {
    "colorOn": [0, 3, 0], "colorOff": [3, 3, 0],
    "colorA": [3, 0, 0], "colorB": [0, 3, 0], "colorReleased": [0, 0, 0],
    "color": [0, 3, 0],
    "blinkColorOn": [0, 3, 0], "blinkColorOff": [3, 0, 0],
}


def rgb3_to_hex(color3):
    return "#%02x%02x%02x" % tuple(round(c / 3 * 255) for c in color3)


def hex_to_rgb3(hex_color):
    hex_color = hex_color.lstrip("#")
    r, g, b = (int(hex_color[i:i + 2], 16) for i in (0, 2, 4))
    return [round(r / 255 * 3), round(g / 255 * 3), round(b / 255 * 3)]


def parse_led_ids(text):
    """"1" / "1,2,3" / "1-10" / a mix like "1,3-5,8" into a list of ints."""
    ids = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            start_str, end_str = part.split("-", 1)
            ids.extend(range(int(start_str), int(end_str) + 1))
        else:
            ids.append(int(part))
    return ids


def default_rule(action_type, device):
    return {
        "actionType": action_type, "device": device,
        "alias": "", "modifierAlias": "", "triggerAlias": "",
        "onAlias": "", "offAlias": "", "aliasA": "", "aliasB": "",
        "ledId": 1, "ledIds": [1, 2, 3, 4], "ledIdsA": [1, 2], "ledIdsB": [3, 4],
        "colorOn": [0, 3, 0], "colorOff": [3, 3, 0],
        "colorA": [3, 0, 0], "colorB": [0, 3, 0], "colorReleased": [0, 0, 0],
        "color": [0, 3, 0],
        "blinkColorOn": [0, 3, 0], "blinkColorOff": [3, 0, 0],
        "interval": 0.3, "cycleSeconds": 2.0,
        "duration": 0.3, "blinkInterval": 0.3, "blinkDuration": 2.0,
        "loops": 2, "debounce": 0.0,
    }


def default_data():
    return {
        "devices": [{"name": "panel", "vid": "3344", "pid": "025c"}],
        "defaultDevice": "panel",
        "selfTestOnStart": True, "allOffButtonAlias": "",
        "initialColors": [], "rules": [],
    }


def rule_summary(rule):
    action_type = rule["actionType"]
    if action_type == "combo":
        alias_text = f'{rule["modifierAlias"]} + {rule["triggerAlias"]}'
    elif action_type in ("switch", "onoff", "blinkonoff"):
        alias_text = f'{rule.get("onAlias", "")} / {rule.get("offAlias", "")}'
    elif action_type == "doorchase":
        alias_text = f'{rule.get("aliasA", "")} / {rule.get("aliasB", "")}'
    else:
        alias_text = rule["alias"]
    if action_type in ("chase", "on", "alarm"):
        led_text = ", ".join(str(x) for x in rule["ledIds"])
    elif action_type == "doorchase":
        led_a = ", ".join(str(x) for x in rule.get("ledIdsA", []))
        led_b = ", ".join(str(x) for x in rule.get("ledIdsB", []))
        led_text = f"{led_a} / {led_b}"
    else:
        led_text = str(rule["ledId"])
    return alias_text, rule.get("device", ""), ACTION_LABELS.get(action_type, action_type), led_text


def load_known_aliases():
    """Alias names already wired in Nexus's own Scripts panel for the
    virpil_led_control.py script, read straight from scripts_config.json -
    the same file Nexus itself writes, so this never gets out of sync by
    hand-typing. Returns ([alias names], [device paths]) - empty lists if
    the file/script entry isn't found yet (nothing wired in Nexus so far)."""
    try:
        with open(SCRIPTS_CONFIG_FILE, "r", encoding="utf-8") as f:
            scripts = json.load(f)
    except (OSError, ValueError):
        return [], []

    for entry in scripts:
        if "virpil_led_control.py" in entry.get("scriptPath", ""):
            aliases = [a["name"] for a in entry.get("inputAliases", [])]
            device_paths = [a["devicePath"] for a in entry.get("inputAliases", []) if a.get("devicePath")]
            return aliases, device_paths
    return [], []


def detect_vid_pid_candidates(device_paths):
    """Unique (vid, pid) pairs found in a list of HID device path strings."""
    seen = []
    for path in device_paths:
        match = re.search(r"VID_([0-9A-Fa-f]{4})&PID_([0-9A-Fa-f]{4})", path)
        if match:
            pair = (match.group(1).lower(), match.group(2).lower())
            if pair not in seen:
                seen.append(pair)
    return seen


def _normalize_to_full_brightness(color3):
    """Scales a color up so its brightest channel hits 3 - i.e. "the same
    hue, but at full brightness" - used so the brightness slider always has
    full 0-3 range to work with regardless of how dim a picked color was."""
    peak = max(color3)
    if peak == 0:
        return [3, 3, 3]  # A picked pure black has no hue to preserve - default to white.
    factor = 3 / peak
    return [round(c * factor) for c in color3]


class ColorSwatchButton(tk.Frame):
    """A small clickable color swatch + hex label, opening askcolor() for
    hue and a brightness slider (0-3, matching the script's own dim()) for
    how bright that hue should be."""

    def __init__(self, parent, color3, on_change, on_preview=None):
        super().__init__(parent, bg=SURFACE1)
        self.on_change = on_change
        self.on_preview = on_preview
        self.base_color3 = _normalize_to_full_brightness(color3)
        self.color3 = list(color3)

        self.swatch = tk.Label(self, width=4, bg=rgb3_to_hex(self.color3), relief="flat",
                                highlightthickness=1, highlightbackground=ACCENT)
        self.swatch.pack(side="left", padx=(0, 6))
        self.swatch.bind("<Button-1>", self._pick)

        self.hex_label = tk.Label(self, text=rgb3_to_hex(self.color3), bg=SURFACE1, fg=SUBTEXT,
                                   font=("Consolas", 9), width=8)
        self.hex_label.pack(side="left")

        tk.Label(self, text="Brillo", bg=SURFACE1, fg=SUBTEXT, font=("Segoe UI", 8)).pack(
            side="left", padx=(8, 2))
        self.brightness_var = tk.IntVar(value=max(color3))
        tk.Scale(self, from_=0, to=3, orient="horizontal", variable=self.brightness_var,
                 command=self._on_brightness_change, length=70, bg=SURFACE1, fg=TEXT,
                 troughcolor=SURFACE2, highlightthickness=0, showvalue=True,
                 font=("Segoe UI", 7)).pack(side="left")

        if on_preview:
            preview_btn = tk.Button(self, text="Probar", command=self._preview, bg=SURFACE2, fg=TEXT,
                                     activebackground=ACCENT, activeforeground=BASE, relief="flat",
                                     font=("Segoe UI", 8), padx=6)
            preview_btn.pack(side="left", padx=(6, 0))

    def _apply_color(self):
        self.swatch.configure(bg=rgb3_to_hex(self.color3))
        self.hex_label.configure(text=rgb3_to_hex(self.color3))
        self.on_change(self.color3)

    def _pick(self, _event=None):
        _rgb, hex_color = colorchooser.askcolor(color=rgb3_to_hex(self.color3), title="Elegir color")
        if hex_color is None:
            return
        self.base_color3 = _normalize_to_full_brightness(hex_to_rgb3(hex_color))
        self.brightness_var.set(3)  # A freshly-picked hue starts at full brightness.
        self.color3 = list(self.base_color3)
        self._apply_color()

    def _on_brightness_change(self, _value):
        level = self.brightness_var.get()
        self.color3 = [round(c * level / 3) for c in self.base_color3]
        self._apply_color()

    def _preview(self):
        if self.on_preview:
            self.on_preview(self.color3)

    def get(self):
        return self.color3


class RuleDialog(tk.Toplevel):
    """Add/edit dialog for a single rule. Result left in self.result
    (a rule dict) if the user confirms, None if they cancel."""

    def __init__(self, parent, aliases, device_names, preview_cb, rule=None):
        super().__init__(parent)
        self.title("Editar regla" if rule else "Nueva regla")
        self.configure(bg=BASE)
        self.resizable(False, False)
        self.transient(parent)
        self.grab_set()

        self.aliases = aliases
        self.device_names = device_names or ["panel"]
        self.preview_cb = preview_cb
        self.rule = dict(rule) if rule else default_rule("color", self.device_names[0])
        self.result = None
        self.swatches = {}

        self.action_var = tk.StringVar(value=self.rule["actionType"])
        self.device_var = tk.StringVar(value=self.rule.get("device", self.device_names[0]))
        self.alias_var = tk.StringVar(value=self.rule.get("alias", ""))
        self.modifier_var = tk.StringVar(value=self.rule.get("modifierAlias", ""))
        self.trigger_var = tk.StringVar(value=self.rule.get("triggerAlias", ""))
        self.on_alias_var = tk.StringVar(value=self.rule.get("onAlias", ""))
        self.off_alias_var = tk.StringVar(value=self.rule.get("offAlias", ""))
        self.alias_a_var = tk.StringVar(value=self.rule.get("aliasA", ""))
        self.alias_b_var = tk.StringVar(value=self.rule.get("aliasB", ""))
        self.led_id_var = tk.StringVar(value=str(self.rule.get("ledId", 1)))
        self.led_ids_var = tk.StringVar(value=",".join(str(x) for x in self.rule.get("ledIds", [1, 2, 3, 4])))
        self.led_ids_a_var = tk.StringVar(value=",".join(str(x) for x in self.rule.get("ledIdsA", [1, 2])))
        self.led_ids_b_var = tk.StringVar(value=",".join(str(x) for x in self.rule.get("ledIdsB", [3, 4])))
        self.interval_var = tk.StringVar(value=str(self.rule.get("interval", 0.3)))
        self.cycle_var = tk.StringVar(value=str(self.rule.get("cycleSeconds", 2.0)))
        self.duration_var = tk.StringVar(value=str(self.rule.get("duration", 0.3)))
        self.blink_interval_var = tk.StringVar(value=str(self.rule.get("blinkInterval", 0.3)))
        self.blink_duration_var = tk.StringVar(value=str(self.rule.get("blinkDuration", 2.0)))
        self.loops_var = tk.StringVar(value=str(self.rule.get("loops", 2)))
        self.debounce_var = tk.StringVar(value=str(self.rule.get("debounce", 0.0)))
        # blinkonoff's colorOn/colorOff are each optional (None = "revert to
        # whatever the LED showed before the blink" instead of a fixed
        # color) - a checkbox per side controls whether the color swatch
        # below is even used, defaulting to UNCHECKED (fixed color) when
        # editing an existing rule that already HAS a color there, and to
        # whatever the loaded rule's null-ness says otherwise.
        self.keep_color_on_var = tk.BooleanVar(value=self.rule.get("colorOn") is not None)
        self.keep_color_off_var = tk.BooleanVar(value=self.rule.get("colorOff") is not None)

        self._build()
        self._on_action_change()

    def _labeled_row(self, parent, label_text):
        row = tk.Frame(parent, bg=BASE)
        row.pack(fill="x", pady=3)
        tk.Label(row, text=label_text, bg=BASE, fg=SUBTEXT, width=16, anchor="w").pack(side="left")
        return row

    def _build(self):
        pad = tk.Frame(self, bg=BASE, padx=16, pady=16)
        pad.pack(fill="both", expand=True)

        row = self._labeled_row(pad, "Dispositivo")
        ttk.Combobox(row, textvariable=self.device_var, state="readonly",
                     values=self.device_names, width=20).pack(side="left")

        row = self._labeled_row(pad, "Tipo de accion")
        # No textvariable bound to self.action_var here on purpose - combo.set()
        # below writes its DISPLAY LABEL (e.g. "Encendido simple..."), and a
        # textvariable binding would let that overwrite self.action_var with
        # that label text instead of the real key ("on") it's supposed to
        # hold. _action_selected() is the only thing that updates self.action_var.
        combo = ttk.Combobox(row, state="readonly",
                              values=[ACTION_LABELS[t] for t in ACTION_TYPES], width=32)
        combo.set(ACTION_LABELS[self.action_var.get()])
        combo.pack(side="left")
        combo.bind("<<ComboboxSelected>>", lambda _e: self._action_selected(combo.get()))

        self.fields_frame = tk.Frame(pad, bg=BASE)
        self.fields_frame.pack(fill="x", pady=(8, 8))

        footer = tk.Frame(pad, bg=BASE)
        footer.pack(fill="x", pady=(12, 0))
        tk.Button(footer, text="Cancelar", command=self.destroy, bg=SURFACE1, fg=TEXT,
                  relief="flat", padx=12, pady=4).pack(side="right", padx=(6, 0))
        tk.Button(footer, text="Guardar regla", command=self._confirm, bg=ACCENT, fg=BASE,
                  relief="flat", padx=12, pady=4, font=("Segoe UI", 9, "bold")).pack(side="right")

    def _action_selected(self, label):
        for key, value in ACTION_LABELS.items():
            if value == label:
                self.action_var.set(key)
                break
        self._on_action_change()

    def _alias_combo(self, parent, textvariable):
        return ttk.Combobox(parent, textvariable=textvariable, values=self.aliases, width=24)

    def _on_action_change(self):
        for child in self.fields_frame.winfo_children():
            child.destroy()
        self.swatches = {}
        action_type = self.action_var.get()
        f = self.fields_frame

        if action_type == "combo":
            row = self._labeled_row(f, "Alias modificador")
            self._alias_combo(row, self.modifier_var).pack(side="left")
            row = self._labeled_row(f, "Alias gatillo")
            self._alias_combo(row, self.trigger_var).pack(side="left")
        elif action_type in ("switch", "onoff", "blinkonoff"):
            row = self._labeled_row(f, "Alias \"on\"")
            self._alias_combo(row, self.on_alias_var).pack(side="left")
            row = self._labeled_row(f, "Alias \"off\"")
            self._alias_combo(row, self.off_alias_var).pack(side="left")
        elif action_type == "doorchase":
            row = self._labeled_row(f, "Alias A")
            self._alias_combo(row, self.alias_a_var).pack(side="left")
            row = self._labeled_row(f, "Alias B")
            self._alias_combo(row, self.alias_b_var).pack(side="left")
        else:
            row = self._labeled_row(f, "Alias")
            self._alias_combo(row, self.alias_var).pack(side="left")

        if action_type in ("chase", "on", "alarm"):
            label = "LEDs (orden, coma)" if action_type == "chase" else "LEDs (ej: 1 o 1,2,3 o 1-10)"
            row = self._labeled_row(f, label)
            tk.Entry(row, textvariable=self.led_ids_var, bg=SURFACE1, fg=TEXT, insertbackground=TEXT,
                     relief="flat", width=26).pack(side="left")
        elif action_type == "doorchase":
            row = self._labeled_row(f, "LEDs direccion A (orden, coma)")
            tk.Entry(row, textvariable=self.led_ids_a_var, bg=SURFACE1, fg=TEXT, insertbackground=TEXT,
                     relief="flat", width=26).pack(side="left")
            row = self._labeled_row(f, "LEDs direccion B (orden, coma)")
            tk.Entry(row, textvariable=self.led_ids_b_var, bg=SURFACE1, fg=TEXT, insertbackground=TEXT,
                     relief="flat", width=26).pack(side="left")
        else:
            row = self._labeled_row(f, "LED id")
            tk.Entry(row, textvariable=self.led_id_var, bg=SURFACE1, fg=TEXT, insertbackground=TEXT,
                     relief="flat", width=8).pack(side="left")

        if action_type == "color":
            self._color_row(f, "Color al apretar", "colorOn")
            self._color_row(f, "Color al soltar", "colorOff")
        elif action_type == "on":
            self._color_row(f, "Color", "color")
        elif action_type == "blink":
            self._color_row(f, "Color A", "colorA")
            self._color_row(f, "Color B", "colorB")
            self._color_row(f, "Color al soltar", "colorReleased")
            row = self._labeled_row(f, "Intervalo (seg)")
            tk.Entry(row, textvariable=self.interval_var, bg=SURFACE1, fg=TEXT, insertbackground=TEXT,
                     relief="flat", width=8).pack(side="left")
            self._full_preview_button(f, "Probar blink completo (3 seg)", self._preview_blink)
        elif action_type == "chase":
            self._color_row(f, "Color", "color")
            row = self._labeled_row(f, "Intervalo (seg)")
            tk.Entry(row, textvariable=self.interval_var, bg=SURFACE1, fg=TEXT, insertbackground=TEXT,
                     relief="flat", width=8).pack(side="left")
            self._full_preview_button(f, "Probar chase completo (2 vueltas)", self._preview_chase)
        elif action_type == "toggle":
            self._color_row(f, "Color A", "colorA")
            self._color_row(f, "Color B", "colorB")
        elif action_type == "breathe":
            self._color_row(f, "Color", "color")
            row = self._labeled_row(f, "Ciclo (seg)")
            tk.Entry(row, textvariable=self.cycle_var, bg=SURFACE1, fg=TEXT, insertbackground=TEXT,
                     relief="flat", width=8).pack(side="left")
            self._full_preview_button(f, "Probar breathe completo (1 ciclo)", self._preview_breathe)
        elif action_type == "axis":
            self._color_row(f, "Color (brillo max)", "color")
        elif action_type == "combo":
            self._color_row(f, "Color", "color")
        elif action_type == "pulse":
            self._color_row(f, "Color del flash", "color")
            row = self._labeled_row(f, "Duracion (seg)")
            tk.Entry(row, textvariable=self.duration_var, bg=SURFACE1, fg=TEXT, insertbackground=TEXT,
                     relief="flat", width=8).pack(side="left")
        elif action_type == "switch":
            self._color_row(f, "Color mientras \"on\" sostenido", "colorOn")
            self._color_row(f, "Color mientras \"off\" sostenido", "colorOff")
            tk.Label(f, text="Si ningun alias esta sostenido (switch de 3 posiciones), el LED se apaga.",
                     bg=BASE, fg=SUBTEXT, font=("Segoe UI", 8), wraplength=380, justify="left").pack(
                anchor="w", pady=(4, 0))
        elif action_type == "onoff":
            self._color_row(f, "Color \"on\" (persiste entre reinicios)", "colorOn")
            self._color_row(f, "Color \"off\" (persiste entre reinicios)", "colorOff")
        elif action_type == "blinkonoff":
            self._color_row(f, "Color de parpadeo \"on\"", "blinkColorOn")
            self._color_row(f, "Color de parpadeo \"off\"", "blinkColorOff")
            row = self._labeled_row(f, "Intervalo de parpadeo (seg)")
            tk.Entry(row, textvariable=self.blink_interval_var, bg=SURFACE1, fg=TEXT, insertbackground=TEXT,
                     relief="flat", width=8).pack(side="left")
            row = self._labeled_row(f, "Duracion del parpadeo (seg)")
            tk.Entry(row, textvariable=self.blink_duration_var, bg=SURFACE1, fg=TEXT, insertbackground=TEXT,
                     relief="flat", width=8).pack(side="left")
            self._optional_color_row(f, "Color fijo al terminar \"on\"", "colorOn", self.keep_color_on_var)
            self._optional_color_row(f, "Color fijo al terminar \"off\"", "colorOff", self.keep_color_off_var)
        elif action_type == "doorchase":
            self._color_row(f, "Color direccion A", "colorA")
            self._color_row(f, "Color direccion B", "colorB")
            row = self._labeled_row(f, "Intervalo (seg)")
            tk.Entry(row, textvariable=self.interval_var, bg=SURFACE1, fg=TEXT, insertbackground=TEXT,
                     relief="flat", width=8).pack(side="left")
            row = self._labeled_row(f, "Vueltas")
            tk.Entry(row, textvariable=self.loops_var, bg=SURFACE1, fg=TEXT, insertbackground=TEXT,
                     relief="flat", width=8).pack(side="left")
        elif action_type == "alarm":
            self._color_row(f, "Color de alarma", "color")
        elif action_type == "blinkpulse":
            self._color_row(f, "Color del parpadeo", "color")
            row = self._labeled_row(f, "Intervalo de parpadeo (seg)")
            tk.Entry(row, textvariable=self.interval_var, bg=SURFACE1, fg=TEXT, insertbackground=TEXT,
                     relief="flat", width=8).pack(side="left")
            row = self._labeled_row(f, "Duracion (seg)")
            tk.Entry(row, textvariable=self.duration_var, bg=SURFACE1, fg=TEXT, insertbackground=TEXT,
                     relief="flat", width=8).pack(side="left")
            row = self._labeled_row(f, "Debounce (seg, 0 = sin limite)")
            tk.Entry(row, textvariable=self.debounce_var, bg=SURFACE1, fg=TEXT, insertbackground=TEXT,
                     relief="flat", width=8).pack(side="left")

    def _color_row(self, parent, label_text, key):
        row = self._labeled_row(parent, label_text)
        color3 = self.rule.get(key, DEFAULT_COLORS.get(key, [3, 3, 3]))
        swatch = ColorSwatchButton(
            row, color3,
            on_change=lambda c, k=key: self.rule.__setitem__(k, c),
            on_preview=lambda c: self._preview(c),
        )
        swatch.pack(side="left")
        self.swatches[key] = swatch

    def _optional_color_row(self, parent, label_text, key, keep_var):
        """Like _color_row, but gated by a checkbox - unchecked means this
        rule's key should be saved as None (e.g. blinkonoff's colorOn/
        colorOff: "revert to whatever the LED showed before the blink"
        instead of a fixed color). The swatch is only built/shown while
        checked, and only read back in _confirm() while checked too."""
        row = self._labeled_row(parent, label_text)
        tk.Checkbutton(row, text="Fijar color", variable=keep_var, command=self._on_action_change,
                       bg=BASE, fg=TEXT, selectcolor=SURFACE1, activebackground=BASE,
                       activeforeground=TEXT).pack(side="left", padx=(0, 8))
        if keep_var.get():
            existing = self.rule.get(key)
            color3 = existing if existing is not None else DEFAULT_COLORS.get(key, [3, 3, 3])
            swatch = ColorSwatchButton(
                row, color3,
                on_change=lambda c, k=key: self.rule.__setitem__(k, c),
                on_preview=lambda c: self._preview(c),
            )
            swatch.pack(side="left")
            self.swatches[key] = swatch
        else:
            tk.Label(row, text="(vuelve al color de antes del parpadeo)", bg=BASE, fg=SUBTEXT,
                     font=("Segoe UI", 8, "italic")).pack(side="left")

    def _preview(self, color3):
        action_type = self.action_var.get()
        try:
            if action_type in ("chase", "on", "alarm"):
                led_id = int(self.led_ids_var.get().split(",")[0])
            elif action_type == "doorchase":
                led_id = int(self.led_ids_a_var.get().split(",")[0])
            else:
                led_id = int(self.led_id_var.get())
        except (ValueError, IndexError):
            messagebox.showerror("Error", "LED id invalido para probar.")
            return
        self.preview_cb(self.device_var.get(), led_id, color3)

    def _full_preview_button(self, parent, label_text, command):
        row = tk.Frame(parent, bg=BASE)
        row.pack(fill="x", pady=(6, 0))
        tk.Button(row, text=label_text, command=command, bg=SURFACE2, fg=ACCENT,
                  activebackground=ACCENT, activeforeground=BASE, relief="flat",
                  font=("Segoe UI", 9, "bold"), padx=10, pady=4).pack(side="left")

    def _run_preview_thread(self, target):
        if getattr(self, "_preview_running", False):
            return  # A preview is already running - ignore extra clicks instead of stacking threads.
        self._preview_running = True

        def wrapper():
            try:
                target()
            finally:
                self._preview_running = False

        threading.Thread(target=wrapper, daemon=True).start()

    def _preview_blink(self):
        try:
            led_id = int(self.led_id_var.get())
            interval = float(self.interval_var.get())
        except ValueError:
            messagebox.showerror("Error", "Revisa el LED id y el intervalo.")
            return
        device = self.device_var.get()
        color_a = self.swatches["colorA"].get()
        color_b = self.swatches["colorB"].get()
        color_released = self.swatches["colorReleased"].get()

        def run():
            end_time = time.monotonic() + 3.0
            show_a = True
            while time.monotonic() < end_time:
                self.preview_cb(device, led_id, color_a if show_a else color_b)
                show_a = not show_a
                time.sleep(interval)
            self.preview_cb(device, led_id, color_released)

        self._run_preview_thread(run)

    def _preview_chase(self):
        try:
            led_ids = [int(x.strip()) for x in self.led_ids_var.get().split(",") if x.strip()]
            interval = float(self.interval_var.get())
        except ValueError:
            messagebox.showerror("Error", "Revisa la lista de LEDs y el intervalo.")
            return
        if not led_ids:
            messagebox.showerror("Error", "La lista de LEDs esta vacia.")
            return
        device = self.device_var.get()
        color = self.swatches["color"].get()

        def run():
            for _lap in range(2):
                previous = None
                for led_id in led_ids:
                    if previous is not None:
                        self.preview_cb(device, previous, [0, 0, 0])
                    self.preview_cb(device, led_id, color)
                    previous = led_id
                    time.sleep(interval)
                self.preview_cb(device, previous, [0, 0, 0])

        self._run_preview_thread(run)

    def _preview_breathe(self):
        try:
            led_id = int(self.led_id_var.get())
            cycle_seconds = float(self.cycle_var.get())
        except ValueError:
            messagebox.showerror("Error", "Revisa el LED id y el ciclo.")
            return
        device = self.device_var.get()
        color = self.swatches["color"].get()
        levels = (0, 1, 2, 3, 2, 1)

        def run():
            step_seconds = cycle_seconds / len(levels)
            for level in levels:
                dimmed = [round(c * level / 3) for c in color]
                self.preview_cb(device, led_id, dimmed)
                time.sleep(step_seconds)
            self.preview_cb(device, led_id, [0, 0, 0])

        self._run_preview_thread(run)

    def _confirm(self):
        action_type = self.action_var.get()
        rule = dict(self.rule)
        rule["actionType"] = action_type
        rule["device"] = self.device_var.get()
        rule["alias"] = self.alias_var.get().strip()
        rule["modifierAlias"] = self.modifier_var.get().strip()
        rule["triggerAlias"] = self.trigger_var.get().strip()
        rule["onAlias"] = self.on_alias_var.get().strip()
        rule["offAlias"] = self.off_alias_var.get().strip()
        rule["aliasA"] = self.alias_a_var.get().strip()
        rule["aliasB"] = self.alias_b_var.get().strip()

        try:
            rule["interval"] = float(self.interval_var.get())
            rule["cycleSeconds"] = float(self.cycle_var.get())
            rule["duration"] = float(self.duration_var.get())
            rule["blinkInterval"] = float(self.blink_interval_var.get())
            rule["blinkDuration"] = float(self.blink_duration_var.get())
            rule["loops"] = int(self.loops_var.get())
            rule["debounce"] = float(self.debounce_var.get())
            if action_type in ("chase", "on", "alarm"):
                rule["ledIds"] = parse_led_ids(self.led_ids_var.get())
                if not rule["ledIds"]:
                    raise ValueError("empty led list")
            elif action_type == "doorchase":
                rule["ledIdsA"] = parse_led_ids(self.led_ids_a_var.get())
                rule["ledIdsB"] = parse_led_ids(self.led_ids_b_var.get())
                if not rule["ledIdsA"] or not rule["ledIdsB"]:
                    raise ValueError("empty led list")
            else:
                rule["ledId"] = int(self.led_id_var.get())
        except ValueError:
            messagebox.showerror(
                "Error", "Revisa los numeros (LED id / intervalo / duracion / vueltas / debounce).")
            return

        for key, swatch in self.swatches.items():
            rule[key] = swatch.get()

        # blinkonoff's colorOn/colorOff are optional - unchecked means
        # "revert to whatever was there before", saved as null in the JSON
        # (see _optional_color_row's own docs).
        if action_type == "blinkonoff":
            rule["colorOn"] = self.swatches["colorOn"].get() if self.keep_color_on_var.get() else None
            rule["colorOff"] = self.swatches["colorOff"].get() if self.keep_color_off_var.get() else None

        if action_type == "combo":
            if not rule["modifierAlias"] or not rule["triggerAlias"]:
                messagebox.showerror("Error", "Elegi los dos alias del combo.")
                return
        elif action_type in ("switch", "onoff", "blinkonoff"):
            if not rule["onAlias"] or not rule["offAlias"]:
                messagebox.showerror("Error", "Elegi los dos alias (on / off).")
                return
        elif action_type == "doorchase":
            if not rule["aliasA"] or not rule["aliasB"]:
                messagebox.showerror("Error", "Elegi los dos alias (A / B).")
                return
        elif not rule["alias"]:
            messagebox.showerror("Error", "Elegi un alias.")
            return

        self.result = rule
        self.destroy()


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Virpil LED - Editor de reglas")
        self.configure(bg=BASE)
        self.geometry("820x900")
        self.minsize(780, 640)
        self._configure_style()

        self.data = default_data()
        self.known_aliases, self.known_device_paths = load_known_aliases()

        self._build()
        self._load_from_disk()

    def _configure_style(self):
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure("TCombobox", fieldbackground=SURFACE1, background=SURFACE1, foreground=TEXT,
                         arrowcolor=ACCENT, bordercolor=SURFACE2)
        style.map("TCombobox", fieldbackground=[("readonly", SURFACE1)])
        style.configure("Treeview", background=SURFACE0, fieldbackground=SURFACE0, foreground=TEXT,
                         bordercolor=SURFACE1, rowheight=24)
        style.configure("Treeview.Heading", background=SURFACE1, foreground=ACCENT, relief="flat")
        style.map("Treeview", background=[("selected", SURFACE2)], foreground=[("selected", ACCENT)])

    def _section_label(self, parent, text):
        tk.Label(parent, text=text, bg=BASE, fg=ACCENT, font=("Segoe UI", 10, "bold")).pack(
            anchor="w", pady=(14, 4))

    def _flat_button(self, parent, text, command, primary=False):
        return tk.Button(parent, text=text, command=command,
                          bg=ACCENT if primary else SURFACE1, fg=BASE if primary else TEXT,
                          activebackground=ACCENT_HOVER if primary else SURFACE2,
                          activeforeground=BASE if primary else TEXT,
                          relief="flat", padx=10, pady=4,
                          font=("Segoe UI", 9, "bold" if primary else "normal"))

    def _device_names(self):
        names = [d["name"] for d in self.data["devices"]]
        return names or ["panel"]

    def _build(self):
        root = tk.Frame(self, bg=BASE, padx=18, pady=14)
        root.pack(fill="both", expand=True)

        # --- devices section ---
        self._section_label(root, "Dispositivos")
        devices_frame = tk.Frame(root, bg=BASE)
        devices_frame.pack(fill="x")
        self.devices_tree = ttk.Treeview(devices_frame, columns=("name", "vid", "pid"), show="headings", height=3)
        self.devices_tree.heading("name", text="Nombre")
        self.devices_tree.heading("vid", text="VID")
        self.devices_tree.heading("pid", text="PID")
        self.devices_tree.column("name", width=120)
        self.devices_tree.column("vid", width=80)
        self.devices_tree.column("pid", width=80)
        self.devices_tree.pack(side="left", fill="x", expand=True)
        devices_btns = tk.Frame(devices_frame, bg=BASE)
        devices_btns.pack(side="left", padx=(8, 0))
        self._flat_button(devices_btns, "+ Agregar", self._add_device).pack(fill="x", pady=2)
        self._flat_button(devices_btns, "Quitar", self._remove_device).pack(fill="x", pady=2)
        self._flat_button(devices_btns, "Detectar desde alias", self._detect_device).pack(fill="x", pady=(10, 2))

        # --- options section ---
        self._section_label(root, "Opciones generales")
        opt_row = tk.Frame(root, bg=BASE)
        opt_row.pack(fill="x")
        self.self_test_var = tk.BooleanVar(value=True)
        tk.Checkbutton(opt_row, text="Self-test al arrancar", variable=self.self_test_var,
                        bg=BASE, fg=TEXT, selectcolor=SURFACE1, activebackground=BASE,
                        activeforeground=TEXT).pack(side="left")
        tk.Label(opt_row, text="Alias de apagado total", bg=BASE, fg=SUBTEXT).pack(side="left", padx=(20, 4))
        self.all_off_var = tk.StringVar()
        ttk.Combobox(opt_row, textvariable=self.all_off_var, values=[""] + self.known_aliases,
                     width=18).pack(side="left")

        # --- initial colors section ---
        self._section_label(root, "Colores iniciales")
        colors_frame = tk.Frame(root, bg=BASE)
        colors_frame.pack(fill="x")
        self.colors_tree = ttk.Treeview(colors_frame, columns=("device", "led", "color"), show="headings", height=4)
        self.colors_tree.heading("device", text="Dispositivo")
        self.colors_tree.heading("led", text="LED")
        self.colors_tree.heading("color", text="Color")
        self.colors_tree.column("device", width=100)
        self.colors_tree.column("led", width=60)
        self.colors_tree.column("color", width=100)
        self.colors_tree.pack(side="left", fill="x", expand=True)
        colors_btns = tk.Frame(colors_frame, bg=BASE)
        colors_btns.pack(side="left", padx=(8, 0))
        self._flat_button(colors_btns, "+ Agregar", self._add_initial_color).pack(fill="x", pady=2)
        self._flat_button(colors_btns, "Quitar", self._remove_initial_color).pack(fill="x", pady=2)
        self._flat_button(colors_btns, "Probar secuencia", self._preview_startup_sequence).pack(fill="x", pady=(10, 2))

        # --- rules section ---
        self._section_label(root, "Reglas")
        rules_frame = tk.Frame(root, bg=BASE)
        rules_frame.pack(fill="both", expand=True)
        self.rules_tree = ttk.Treeview(
            rules_frame, columns=("alias", "device", "action", "led"), show="headings", height=10)
        self.rules_tree.heading("alias", text="Alias")
        self.rules_tree.heading("device", text="Dispositivo")
        self.rules_tree.heading("action", text="Accion")
        self.rules_tree.heading("led", text="LED(s)")
        self.rules_tree.column("alias", width=140)
        self.rules_tree.column("device", width=100)
        self.rules_tree.column("action", width=240)
        self.rules_tree.column("led", width=100)
        self.rules_tree.pack(side="left", fill="both", expand=True)
        self.rules_tree.bind("<Double-1>", lambda _e: self._edit_rule())
        rules_btns = tk.Frame(rules_frame, bg=BASE)
        rules_btns.pack(side="left", padx=(8, 0), fill="y")
        self._flat_button(rules_btns, "+ Nueva regla", self._add_rule).pack(fill="x", pady=2)
        self._flat_button(rules_btns, "Editar", self._edit_rule).pack(fill="x", pady=2)
        self._flat_button(rules_btns, "Quitar", self._remove_rule).pack(fill="x", pady=2)

        # --- footer ---
        footer = tk.Frame(root, bg=BASE)
        footer.pack(fill="x", pady=(14, 0))
        self.status_var = tk.StringVar(value="")
        tk.Label(footer, textvariable=self.status_var, bg=BASE, fg=SUCCESS).pack(side="left")
        self._flat_button(footer, "Guardar", self._save, primary=True).pack(side="right")
        self._flat_button(footer, "Recargar del disco", self._load_from_disk).pack(side="right", padx=(0, 8))

    # --- devices -------------------------------------------------------------
    def _add_device(self):
        name, vid, pid = self._ask_device_details()
        if not name:
            return
        self.data["devices"].append({"name": name, "vid": vid, "pid": pid})
        self._refresh_devices_tree()

    def _remove_device(self):
        sel = self.devices_tree.selection()
        if not sel:
            return
        index = self.devices_tree.index(sel[0])
        del self.data["devices"][index]
        self._refresh_devices_tree()

    def _ask_device_details(self, vid="", pid=""):
        result = {}

        def confirm():
            name = name_var.get().strip()
            if not name:
                messagebox.showerror("Error", "Poné un nombre para el dispositivo.")
                return
            if not vid_var.get().strip() or not pid_var.get().strip():
                messagebox.showerror("Error", "Faltan VID/PID.")
                return
            result["name"] = name
            result["vid"] = vid_var.get().strip()
            result["pid"] = pid_var.get().strip()
            dialog.destroy()

        dialog = tk.Toplevel(self)
        dialog.title("Dispositivo")
        dialog.configure(bg=BASE)
        dialog.transient(self)
        dialog.grab_set()

        row = tk.Frame(dialog, bg=BASE)
        row.pack(padx=14, pady=(14, 4))
        tk.Label(row, text="Nombre:", bg=BASE, fg=TEXT, width=8, anchor="w").pack(side="left")
        name_var = tk.StringVar()
        tk.Entry(row, textvariable=name_var, bg=SURFACE1, fg=TEXT, insertbackground=TEXT,
                 relief="flat", width=16).pack(side="left")

        row = tk.Frame(dialog, bg=BASE)
        row.pack(padx=14, pady=4)
        tk.Label(row, text="VID:", bg=BASE, fg=TEXT, width=8, anchor="w").pack(side="left")
        vid_var = tk.StringVar(value=vid)
        tk.Entry(row, textvariable=vid_var, bg=SURFACE1, fg=TEXT, insertbackground=TEXT,
                 relief="flat", width=16).pack(side="left")

        row = tk.Frame(dialog, bg=BASE)
        row.pack(padx=14, pady=4)
        tk.Label(row, text="PID:", bg=BASE, fg=TEXT, width=8, anchor="w").pack(side="left")
        pid_var = tk.StringVar(value=pid)
        tk.Entry(row, textvariable=pid_var, bg=SURFACE1, fg=TEXT, insertbackground=TEXT,
                 relief="flat", width=16).pack(side="left")

        self._flat_button(dialog, "OK", confirm, primary=True).pack(pady=12)
        self.wait_window(dialog)
        return result.get("name"), result.get("vid"), result.get("pid")

    def _refresh_devices_tree(self):
        self.devices_tree.delete(*self.devices_tree.get_children())
        for d in self.data["devices"]:
            self.devices_tree.insert("", "end", values=(d["name"], d["vid"], d["pid"]))

    # --- device detection -------------------------------------------------
    def _detect_device(self):
        candidates = detect_vid_pid_candidates(self.known_device_paths)
        if not candidates:
            messagebox.showinfo(
                "Sin datos",
                "No encontre alias cableados todavia en Nexus para este script "
                "(scripts_config.json esta vacio o no tiene input aliases). "
                "Cablea al menos un boton en el panel de Scripts de Nexus primero.")
            return
        existing = {(d["vid"].lower(), d["pid"].lower()) for d in self.data["devices"]}
        new_candidates = [c for c in candidates if c not in existing]
        if not new_candidates:
            messagebox.showinfo("Sin novedades", "Todos los dispositivos encontrados en tus alias ya estan agregados.")
            return

        picker = tk.Toplevel(self)
        picker.title("Elegi el dispositivo a agregar")
        picker.configure(bg=BASE)
        tk.Label(picker, text="Se encontraron estos dispositivos entre tus alias:",
                 bg=BASE, fg=TEXT).pack(padx=14, pady=(14, 6))
        for vid, pid in new_candidates:
            def choose(v=vid, p=pid):
                picker.destroy()
                name, real_vid, real_pid = self._ask_device_details(vid=v, pid=p)
                if name:
                    self.data["devices"].append({"name": name, "vid": real_vid, "pid": real_pid})
                    self._refresh_devices_tree()
            self._flat_button(picker, f"VID {vid}  PID {pid}", choose).pack(fill="x", padx=14, pady=3)
        tk.Frame(picker, height=10, bg=BASE).pack()

    # --- initial colors -----------------------------------------------------
    def _add_initial_color(self):
        _rgb, hex_color = colorchooser.askcolor(title="Color inicial")
        if hex_color is None:
            return
        device, led_ids, brightness = self._ask_device_led_ids_and_brightness()
        if not led_ids:
            return
        base_color3 = _normalize_to_full_brightness(hex_to_rgb3(hex_color))
        color3 = [round(c * brightness / 3) for c in base_color3]
        for led_id in led_ids:
            self.data["initialColors"].append({"device": device, "ledId": led_id, "color": color3})
        self._refresh_colors_tree()

    def _remove_initial_color(self):
        sel = self.colors_tree.selection()
        if not sel:
            return
        index = self.colors_tree.index(sel[0])
        del self.data["initialColors"][index]
        self._refresh_colors_tree()

    def _ask_device_led_ids_and_brightness(self):
        result = {}

        def confirm():
            try:
                ids = parse_led_ids(entry_var.get())
            except ValueError:
                messagebox.showerror("Error", "No pude leer esa lista de LEDs.")
                return
            if not ids:
                messagebox.showerror("Error", "Poné al menos un LED.")
                return
            result["device"] = device_var.get()
            result["ids"] = ids
            result["brightness"] = brightness_var.get()
            dialog.destroy()

        dialog = tk.Toplevel(self)
        dialog.title("LED(s)")
        dialog.configure(bg=BASE)
        dialog.transient(self)
        dialog.grab_set()

        device_row = tk.Frame(dialog, bg=BASE)
        device_row.pack(padx=14, pady=(14, 4))
        tk.Label(device_row, text="Dispositivo:", bg=BASE, fg=TEXT).pack(side="left")
        device_var = tk.StringVar(value=self._device_names()[0])
        ttk.Combobox(device_row, textvariable=device_var, state="readonly",
                     values=self._device_names(), width=16).pack(side="left")

        tk.Label(dialog, text="Numero(s) de LED - ej: 1  o  1,2,3  o  1-10:", bg=BASE, fg=TEXT).pack(
            padx=14, pady=(10, 4))
        entry_var = tk.StringVar(value="1")
        tk.Entry(dialog, textvariable=entry_var, bg=SURFACE1, fg=TEXT, insertbackground=TEXT,
                 relief="flat", width=20).pack(padx=14)

        brightness_row = tk.Frame(dialog, bg=BASE)
        brightness_row.pack(padx=14, pady=(10, 0))
        tk.Label(brightness_row, text="Brillo (0-3):", bg=BASE, fg=TEXT).pack(side="left")
        brightness_var = tk.IntVar(value=3)
        tk.Scale(brightness_row, from_=0, to=3, orient="horizontal", variable=brightness_var,
                 length=90, bg=BASE, fg=TEXT, troughcolor=SURFACE2, highlightthickness=0,
                 showvalue=True).pack(side="left")

        self._flat_button(dialog, "OK", confirm, primary=True).pack(pady=12)
        self.wait_window(dialog)
        return result.get("device"), result.get("ids"), result.get("brightness", 3)

    def _refresh_colors_tree(self):
        self.colors_tree.delete(*self.colors_tree.get_children())
        for entry in self.data["initialColors"]:
            self.colors_tree.insert("", "end", values=(entry.get("device", ""), entry["ledId"], rgb3_to_hex(entry["color"])))

    # --- rules ---------------------------------------------------------------
    def _add_rule(self):
        dialog = RuleDialog(self, self.known_aliases, self._device_names(), self._preview_led)
        self.wait_window(dialog)
        if dialog.result:
            self.data["rules"].append(dialog.result)
            self._refresh_rules_tree()

    def _edit_rule(self):
        sel = self.rules_tree.selection()
        if not sel:
            return
        index = self.rules_tree.index(sel[0])
        dialog = RuleDialog(self, self.known_aliases, self._device_names(), self._preview_led,
                             rule=self.data["rules"][index])
        self.wait_window(dialog)
        if dialog.result:
            self.data["rules"][index] = dialog.result
            self._refresh_rules_tree()

    def _remove_rule(self):
        sel = self.rules_tree.selection()
        if not sel:
            return
        index = self.rules_tree.index(sel[0])
        del self.data["rules"][index]
        self._refresh_rules_tree()

    def _refresh_rules_tree(self):
        self.rules_tree.delete(*self.rules_tree.get_children())
        for rule in self.data["rules"]:
            alias_text, device_text, action_text, led_text = rule_summary(rule)
            self.rules_tree.insert("", "end", values=(alias_text, device_text, action_text, led_text))

    def _preview_led(self, device, led_id, color3):
        device_entry = next((d for d in self.data["devices"] if d["name"] == device), None)
        if device_entry is None:
            messagebox.showerror("Error", f"No conozco el dispositivo '{device}'.")
            return
        vid, pid = device_entry["vid"].strip(), device_entry["pid"].strip()
        if not vid or not pid:
            messagebox.showerror("Error", "Falta VID/PID de ese dispositivo.")
            return
        if not os.path.exists(VPC_LED_TOOL):
            messagebox.showerror("Error", f"No encuentro {VPC_LED_TOOL}")
            return
        r, g, b = color3
        subprocess.Popen(
            [VPC_LED_TOOL, vid, pid, str(led_id), str(r), str(g), str(b)],
            creationflags=subprocess.CREATE_NEW_CONSOLE,
        )

    def _preview_startup_sequence(self):
        """Simulates exactly what virpil_led_control.py does when Nexus
        starts it: an optional self-test sweep (every known (device, LED)
        pair, one at a time, white then off), then settling into the
        initial colors - same order, same 0.15s step, as run_self_test()
        in the script."""
        if getattr(self, "_startup_preview_running", False):
            return
        self._startup_preview_running = True

        initial_colors = list(self.data["initialColors"])
        self_test_on = self.self_test_var.get()

        pairs = {(entry.get("device", ""), entry["ledId"]) for entry in initial_colors}
        for rule in self.data["rules"]:
            device = rule.get("device", "")
            action_type = rule["actionType"]
            if action_type in ("chase", "on", "alarm"):
                pairs.update((device, led_id) for led_id in rule.get("ledIds", []))
            elif action_type == "doorchase":
                pairs.update((device, led_id) for led_id in rule.get("ledIdsA", []))
                pairs.update((device, led_id) for led_id in rule.get("ledIdsB", []))
            elif "ledId" in rule:
                pairs.add((device, rule["ledId"]))
        pairs = sorted(pairs)

        def run():
            try:
                if self_test_on:
                    for device, led_id in pairs:
                        self._preview_led(device, led_id, [3, 3, 3])
                        time.sleep(0.15)
                        self._preview_led(device, led_id, [0, 0, 0])
                for entry in initial_colors:
                    self._preview_led(entry.get("device", ""), entry["ledId"], entry["color"])
            finally:
                self._startup_preview_running = False

        threading.Thread(target=run, daemon=True).start()

    # --- load/save -------------------------------------------------------------
    def _load_from_disk(self):
        try:
            with open(RULES_FILE, "r", encoding="utf-8") as f:
                self.data = json.load(f)
            self.status_var.set(f"Cargado desde {RULES_FILE}")
        except (OSError, ValueError):
            self.data = default_data()
            self.status_var.set("Sin archivo de reglas todavia - arrancando en blanco.")

        if not self.data.get("devices"):
            self.data["devices"] = default_data()["devices"]

        self.self_test_var.set(self.data.get("selfTestOnStart", True))
        self.all_off_var.set(self.data.get("allOffButtonAlias", ""))
        self._refresh_devices_tree()
        self._refresh_colors_tree()
        self._refresh_rules_tree()

    def _save(self):
        self.data["defaultDevice"] = self._device_names()[0]
        self.data["selfTestOnStart"] = self.self_test_var.get()
        self.data["allOffButtonAlias"] = self.all_off_var.get().strip()

        os.makedirs(os.path.dirname(RULES_FILE), exist_ok=True)
        with open(RULES_FILE, "w", encoding="utf-8") as f:
            json.dump(self.data, f, indent=2)

        if DEPLOYED_RULES_FILE and os.path.isdir(os.path.dirname(DEPLOYED_RULES_FILE)):
            with open(DEPLOYED_RULES_FILE, "w", encoding="utf-8") as f:
                json.dump(self.data, f, indent=2)
            self.status_var.set(f"Guardado en {RULES_FILE} y sincronizado al deploy local")
        else:
            self.status_var.set(f"Guardado en {RULES_FILE}")


if __name__ == "__main__":
    App().mainloop()
