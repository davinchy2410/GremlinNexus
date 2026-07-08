import QtQuick
import QtQuick.Effects
import GremblingEx

// Standardized Glassmorphism panel (Fase 14): the semi-transparent, soft-
// shadowed card background pattern previously hand-duplicated per screen
// (SettingsView's three cards, AutoSwitchPopup's background,
// CalibrationWizardPopup's background - each with its own near-identical
// Rectangle+MultiEffect pair). A single Item so it drops in wherever a
// Popup's own "background:" or a plain card container needs this look,
// anchors.fill'd by its own parent's layout.
Item {
    id: root

    property color baseColor: Theme.surface0
    property real bgOpacity: 0.85
    property real cornerRadius: Theme.radiusMedium

    /// Border/shadow default to a barely-visible white hairline + the
    /// app-wide neutral drop shadow (matching SettingsView/
    /// CalibrationWizardPopup's own prior look) - AutoSwitchPopup overrides
    /// both to Theme.accent for its own distinct glow instead.
    property color borderColor: Qt.rgba(1, 1, 1, 0.08)
    property color shadowColor: Theme.shadowColor

    Rectangle {
        id: backgroundRect
        anchors.fill: parent
        radius: root.cornerRadius
        color: Qt.rgba(root.baseColor.r, root.baseColor.g, root.baseColor.b, root.bgOpacity)
        border.width: 1
        border.color: root.borderColor
    }

    MultiEffect {
        source: backgroundRect
        anchors.fill: backgroundRect
        shadowEnabled: true
        shadowColor: root.shadowColor
        shadowBlur: 20
        shadowVerticalOffset: 8
        blurMax: 64
    }
}
