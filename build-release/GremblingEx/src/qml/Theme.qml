pragma Singleton
import QtQuick

// Central design-system palette/spacing/motion constants for the whole app
// (Phase 10). Catppuccin Mocha-derived dark palette: deep, low-saturation
// blues/purples rather than flat black/gray, so panels at different
// elevations (crust/mantle/base/surface0-2) stay readable against each
// other without resorting to pure black or harsh borders.
QtObject {
    // --- Elevation levels, darkest (window chrome) to lightest (raised surfaces).
    readonly property color crust: "#11111b"
    readonly property color mantle: "#181825"
    readonly property color base: "#1e1e2e"
    readonly property color surface0: "#313244"
    readonly property color surface1: "#45475a"
    readonly property color surface2: "#585b70"

    // --- Text.
    readonly property color text: "#cdd6f4"
    readonly property color subtext0: "#a6adc8"
    readonly property color overlay0: "#6c7086"

    // --- Accents.
    readonly property color accent: "#cba6f7"          // Mauve - primary accent (selection, active state).
    readonly property color accentHover: "#b4befe"     // Lavender - hover/lighter variant.
    readonly property color accentSecondary: "#89b4fa" // Blue - secondary accent, reserved for future screens.
    readonly property color success: "#a6e3a1"         // Green - positive/"running" state (e.g. the Engine switch).
    readonly property color warning: "#f9e2af"         // Yellow - qWarning() lines in the Log Console (Fase 14).
    readonly property color danger: "#f38ba8"          // Red - qCritical()/qFatal() lines in the Log Console (Fase 14).

    // --- Geometry.
    readonly property int radiusSmall: 10
    readonly property int radiusMedium: 16
    readonly property int radiusLarge: 20

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
