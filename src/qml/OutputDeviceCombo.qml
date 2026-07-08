import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GremblingNexus

// Two-step vJoy/Xbox output-device picker (ViGEm UX refactor): replaces the
// old single flat "vJoy 1".."vJoy 16","Xbox 360 Controller 1".."4" ComboBox,
// which forced scrolling past all 16 vJoy entries to reach an Xbox slot.
// deviceTypeCombo picks the backend ("vJoy" or "Xbox 360"); deviceIdCombo's
// own model swaps between vJoy's 16 ids and ViGEm's 4 ad-hoc slots to match,
// so switching to Xbox is instant instead of a long scroll. Every popup that
// used to bind a plain AppComboBox to "outputDeviceNames" now embeds one of
// these instead and reads targetDeviceType/targetOutputId off it directly.
RowLayout {
    id: root
    spacing: Theme.spacingXs

    readonly property bool isXbox: deviceTypeCombo.currentIndex === 1
    readonly property string targetDeviceType: root.isXbox ? "vigem" : "vjoy"
    readonly property int targetOutputId: deviceIdCombo.currentIndex + 1

    /// Emitted whenever the user (not a programmatic setFromTarget() call)
    /// changes either combo - lets a per-row delegate (e.g. SequencePopup's
    /// step list) write targetDeviceType/targetOutputId straight back into
    /// its own backing model on every edit, instead of only reading this
    /// picker's value once at an outer Apply button click.
    signal changed()

    /// Restores deviceTypeCombo/deviceIdCombo from actionData's own
    /// targetDeviceType/targetOutputId fields (or any object with that
    /// shape) - the read-side counterpart of targetDeviceType/
    /// targetOutputId above. Pass null/undefined (or omit targetOutputId)
    /// to reset to the default vJoy 1.
    function setFromTarget(actionData) {
        deviceTypeCombo.currentIndex = (actionData && actionData.targetDeviceType === "vigem") ? 1 : 0;
        const outputId = (actionData && actionData.targetOutputId) || 1;
        deviceIdCombo.currentIndex = outputId - 1;
    }

    AppComboBox {
        id: deviceTypeCombo
        Layout.preferredWidth: 92
        model: ["vJoy", "Xbox 360"]
        // A user-driven type switch resets the id to a safe default rather
        // than leaving whatever numeric index the old range happened to
        // land on (e.g. vJoy 16 -> Xbox would otherwise clamp to Xbox 4).
        onActivated: {
            deviceIdCombo.currentIndex = 0;
            root.changed();
        }
    }
    AppComboBox {
        id: deviceIdCombo
        Layout.preferredWidth: 64
        model: root.isXbox ? ["1", "2", "3", "4"]
                           : Array.from({length: 16}, (_, i) => String(i + 1))
        onActivated: root.changed()
    }
}
