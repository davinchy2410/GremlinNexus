pragma Singleton
import QtQuick

// Central design-system palette/spacing/motion constants for the whole app.
// FUI / Data Terminal redesign: replaces the old Catppuccin Mocha
// glassmorphism palette with deep space-blues + a bright cyan accent and
// zero corner radii everywhere - a "hard scanner/military simulator"
// terminal look rather than a soft, rounded consumer app. Every raw
// Rectangle across the app that reads Theme.radiusSmall/Medium/Large
// therefore goes perfectly square just from this one file changing.
QtObject {
    // --- Elevation levels, darkest (window chrome) to lightest (raised surfaces).
    readonly property color crust: "#03050a"
    readonly property color mantle: "#070a14"
    readonly property color base: "#0b1021"
    readonly property color surface0: "#101830"
    readonly property color surface1: "#17203f"
    readonly property color surface2: "#202b52"

    // --- Text.
    readonly property color text: "#d8e6f3"
    readonly property color subtext0: "#7891ab"
    readonly property color overlay0: "#4d6a85"

    // --- Accents.
    readonly property color accent: "#00f3ff"          // Bright neon cyan - primary accent (selection, active state).
    readonly property color accentHover: "#7dfbff"     // Lighter cyan - hover/lighter variant.
    readonly property color accentSecondary: "#3fa9f5" // Technical blue - secondary accent, reserved for future screens.
    readonly property color success: "#2ee6a8"         // Terminal green - positive/"running" state (e.g. the Engine switch).
    readonly property color warning: "#ff9500"         // Technical orange - qWarning() lines in the Log Console.
    readonly property color danger: "#ff3b30"          // Technical red - qCritical()/qFatal() lines in the Log Console.

    // --- Geometry. FUI: no rounded corners anywhere in this design language.
    readonly property int radiusSmall: 0
    readonly property int radiusMedium: 0
    readonly property int radiusLarge: 0

    // --- Shadows. Single source of truth so every DropShadow/MultiEffect
    // in the app looks like the same light source rather than a patchwork
    // of hand-picked alphas (Fase 15).
    readonly property color shadowColor: Qt.rgba(0, 0, 0, 0.4)

    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 16
    readonly property int spacingLg: 24

    // --- Motion. Every interactive micro-animation in the app should use
    // one of these instead of a hand-picked duration, so the whole UI feels
    // like one consistent, deliberately-tuned system rather than a
    // patchwork of slightly different timings.
    readonly property int animFast: 120
    readonly property int animMedium: 200
    readonly property int animSlow: 320
}
