import QtQuick
import GremblingNexus

// FUI / Data Terminal panel (previously "Glassmorphism panel", Fase 14):
// kept the same name/API to avoid a mass refactor across every screen that
// already instantiates it (SettingsView's three cards, AutoSwitchPopup's
// background, CalibrationWizardPopup's background), but the look underneath
// is now the opposite of glass - a flat, fully opaque dark panel with a
// crisp 1px accent-cyan border and no blur/shadow at all, matching the
// "no more soft shadows/glow, hard terminal edges" art direction.
Item {
    id: root

    property color baseColor: Theme.surface0
    property real bgOpacity: 1.0
    property real cornerRadius: Theme.radiusMedium

    /// Strict 1px cyan border by default now (previously a barely-visible
    /// white hairline) - callers that still want a distinct glow color
    /// (e.g. AutoSwitchPopup) can keep overriding this. shadowColor is kept
    /// as a property purely so existing overrides (AutoSwitchPopup) don't
    /// break the binding; it's unused now that MultiEffect is gone.
    property color borderColor: Theme.accent
    property color shadowColor: Theme.shadowColor

    Rectangle {
        id: backgroundRect
        anchors.fill: parent
        radius: root.cornerRadius
        color: Qt.rgba(root.baseColor.r, root.baseColor.g, root.baseColor.b, root.bgOpacity)
        border.width: 1
        border.color: root.borderColor
    }
}
