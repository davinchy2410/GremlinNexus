#pragma once

#include <QString>
#include <QStringList>

/// Shared human-readable axis/button naming, so every screen that lists a
/// physical device's channels (Profiles' own device tree, the Scripts
/// panel's alias picker) agrees on what channel N is called instead of each
/// maintaining its own copy - same reasoning ProfileEditorViewModel.cpp's
/// own bindingLabelForActionJson() already gives for its (different, vJoy/
/// ViGEm-target-facing) naming table. Moved here from
/// ProfileEditorViewModel.cpp (Fase 19.6) once the Scripts panel's alias
/// picker needed the exact same names for its own device/channel dropdown.
namespace InputNaming {

/// axisIndex is in [0, numAxes) - true analog axes only (see DeviceInfo's
/// own docs on hats not being reported as axes).
inline QString axisDisplayName(int axisIndex)
{
    switch (axisIndex) {
        case 0: return QStringLiteral("Axis X");
        case 1: return QStringLiteral("Axis Y");
        case 2: return QStringLiteral("Axis Z (Throttle/Rudder)");
        case 3: return QStringLiteral("Axis Rx (Pitch/Roll)");
        case 4: return QStringLiteral("Axis Ry");
        case 5: return QStringLiteral("Axis Rz");
        default: return QStringLiteral("Slider %1").arg(axisIndex - 5);
    }
}

/// buttonIndex is in [0, numButtons) - numButtons includes 4 synthetic
/// entries per POV hat appended after the device's real physical buttons
/// (see DeviceInfo::numButtons's own docs), which this labels "POV H
/// <Direction>" instead of "Button N".
inline QString buttonDisplayName(int buttonIndex, int numButtons, int numHats)
{
    const int physicalButtonCount = numButtons - numHats * 4;
    if (buttonIndex < physicalButtonCount) {
        return QStringLiteral("Button %1").arg(buttonIndex + 1);
    }

    const int hatButtonIndex = buttonIndex - physicalButtonCount;
    const int hatNumber = hatButtonIndex / 4 + 1; // 1-based: "POV 1", "POV 2", ...
    static const QStringList kDirections{
        QStringLiteral("Up"), QStringLiteral("Right"), QStringLiteral("Down"), QStringLiteral("Left")};
    return QStringLiteral("POV %1 %2").arg(hatNumber).arg(kDirections.at(hatButtonIndex % 4));
}

} // namespace InputNaming
