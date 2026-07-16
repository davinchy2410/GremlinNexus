#include "ProfileEditorViewModel.h"

#include <algorithm>
#include <cstdlib>

#include <QDebug>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QUrl>

#include "AutoSwitchManager.h"
#include "DeviceInfo.h"
#include "DeviceManager.h"
#include "EventRouter.h"
#include "KeyboardHandler.h"
#include "PwaServer.h"
#include "SCIntegrationManager.h"

namespace {
/// EventRouter::kGlobalMode is EventRouter's own literal, not a value this
/// ViewModel controls - m_modes' seed entry uses the exact same string
/// (rather than a friendlier display label like "Default") so a binding
/// registered under it keeps EventRouter's "always active regardless of
/// the selected mode" special-casing instead of silently becoming a
/// different, non-special-cased mode that just happens to look similar.

/// Stable per-device identity key for device-tab-order persistence (Fase -
/// drag-to-reorder device tabs): VID:PID, parsed back out of vendorProduct's
/// own "VID:xxxx PID:xxxx" display format (see makeDeviceEntry()) rather than
/// a redundant new DeviceEntry field. Falls back to systemPath for a device
/// with no real VID/PID to key on (an R14/Legacy import's synthetic offline
/// placeholder hardcodes vendorProduct to a fixed literal string instead -
/// see makeDeviceEntry()'s orphan-row construction), so those still get a
/// distinct key of their own rather than colliding under one shared literal.
///
/// Ambiguous for two physically-identical devices sharing the exact same
/// VID/PID (both resolve to the same key, so only the last one processed
/// keeps its saved position) - the same known limitation
/// ProfileManager::loadProfile()'s own resolveMigratedPath() already
/// documents and accepts for orphaned-device path migration; not worth
/// solving twice for a corner case this project already lives with elsewhere.
QString deviceOrderKey(const QString &vendorProduct, const QString &systemPath)
{
    static const QRegularExpression pattern(QStringLiteral("VID:(\\S+) PID:(\\S+)"));
    const QRegularExpressionMatch match = pattern.match(vendorProduct);
    return match.hasMatch() ? (match.captured(1) + QLatin1Char(':') + match.captured(2)) : systemPath;
}

/// Readable tab label for an orphaned/offline device row (see
/// makeDeviceEntry()'s orphan-row construction) - the raw systemPath these
/// rows previously used verbatim as their name is a full Windows device
/// instance path (e.g. "\\?\HID#VID_3344&PID_C4B0&MI_00#..."), unreadable
/// as a tab title and impossible to visually tell apart from a dozen others
/// at a glance. Pulls out just the VID/PID - same "VID_xxxx&PID_yyyy" shape
/// DeviceManager::parseVidPid() looks for in a real device path - so the
/// tab at least shows which physical device it once was without needing to
/// hover/inspect. Falls back to the raw systemPath unchanged for a path
/// that doesn't carry a VID/PID at all (e.g. a "[Legacy] <name>" synthetic
/// source tag LegacyProfileImporter uses instead of a real device path).
QString offlineDeviceLabel(const QString &systemPath)
{
    static const QRegularExpression pattern(QStringLiteral("VID_([0-9A-Fa-f]{4})&PID_([0-9A-Fa-f]{4})"));
    const QRegularExpressionMatch match = pattern.match(systemPath);
    if (!match.hasMatch()) {
        return systemPath;
    }
    return QStringLiteral("Offline (VID:%1 PID:%2)").arg(match.captured(1).toUpper(), match.captured(2).toUpper());
}

/// Display name for buttonIndex out of a device with numButtons total
/// buttons and numHats POV hats (Fase 16.7). The last numHats*4 buttons are
/// synthetic Up/Right/Down/Left entries DeviceManager appends per hat (see
/// DeviceManager::parseHidReport()) rather than real physical buttons - this
/// is the single place that decides how those get labeled, shared by
/// makeDeviceEntry() (building the bindable input list) and
/// onButtonPressedForDetection() (Quick Bind), so the two can never disagree
/// on a synthetic button's name.
QString buttonDisplayName(int buttonIndex, int numButtons, int numHats)
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

/// Short display label for a binding's own JSON (IActionHandler::toJson()
/// shape) - shared by bindAction() (a binding just applied this session) and
/// makeDeviceEntry() (Fase 19: a route that already existed in m_router
/// *before* this input was ever touched, e.g. one loaded from a profile at
/// startup - see that function's own docs for why this was previously always
/// "Unbound" instead).
QString bindingLabelForActionJson(const QJsonObject &binding)
{
    // Fase 20.40: short per-axis name (vJoy's own [0, 7] axis index space) -
    // shared by CurveHandler/SmoothingHandler's own label below and the
    // MergeAxisHandler/SplitAxisHandler previews further down, so all three
    // agree on what axis N is called instead of each maintaining its own copy.
    auto getAxisName = [](int axis) -> QString {
        switch (axis) {
            case 0: return QStringLiteral("X");
            case 1: return QStringLiteral("Y");
            case 2: return QStringLiteral("Z");
            case 3: return QStringLiteral("Rx");
            case 4: return QStringLiteral("Ry");
            case 5: return QStringLiteral("Rz");
            case 6: return QStringLiteral("Slider 1");
            case 7: return QStringLiteral("Slider 2");
            default: return QStringLiteral("Axis %1").arg(axis);
        }
    };

    // ViGEm integration: a binding's top-level "targetDeviceType" ("vjoy",
    // the default, or "vigem") picks which prefix its targetOutputId gets
    // labelled with - shared by every single-device handler kind below so a
    // ViGEm-targeted binding reads "Xbox N" instead of a misleading "vJoy N".
    auto getDeviceLabel = [](const QJsonObject &b) -> QString {
        const int outputId = b.value(QStringLiteral("targetOutputId")).toInt(1);
        const bool isVigem = b.value(QStringLiteral("targetDeviceType")).toString() == QStringLiteral("vigem");
        return isVigem ? QStringLiteral("Xbox %1").arg(outputId) : QStringLiteral("vJoy %1").arg(outputId);
    };

    QString label = binding.value(QStringLiteral("actionType")).toString();
    if (label == QStringLiteral("CurveHandler") || label == QStringLiteral("SmoothingHandler")) {
        const int targetAxis = binding.value(QStringLiteral("targetAxis")).toInt();
        label = QStringLiteral("%1 : Axis %2").arg(getDeviceLabel(binding), getAxisName(targetAxis));
    } else if (label == QStringLiteral("ButtonRemapHandler")) {
        label = QStringLiteral("%1 : Btn %2").arg(getDeviceLabel(binding))
                                             .arg(binding.value(QStringLiteral("targetButton")).toInt() + 1);
    } else if (label == QStringLiteral("HatRemapHandler")) {
        static const QStringList kDirections{
            QStringLiteral("Up"), QStringLiteral("Right"), QStringLiteral("Down"), QStringLiteral("Left")};
        const int direction = binding.value(QStringLiteral("targetDirection")).toInt();
        const QString directionLabel = direction >= 0 && direction < kDirections.size()
            ? kDirections.at(direction) : QString::number(direction);
        label = QStringLiteral("%1 : Hat %2 %3").arg(getDeviceLabel(binding))
                                                 .arg(binding.value(QStringLiteral("targetHat")).toInt() + 1)
                                                 .arg(directionLabel);
    } else if (label == QStringLiteral("KeyboardHandler")) {
        const int scanCode =
            binding.value(QStringLiteral("parameters")).toObject().value(QStringLiteral("scanCode")).toInt();
        label = QStringLiteral("Keyboard: %1").arg(KeyboardHandler::scanCodeToKeyName(static_cast<uint16_t>(scanCode)));
    } else if (label == QStringLiteral("MacroHandler")) {
        // Fase (Macro binding preview): a compact per-step summary instead
        // of just the bare word "Macro", same "abbreviate + join" idiom
        // SequenceHandler's own preview uses below - so a multi-step macro
        // still fits the Pill's width. PressButton/ReleaseButton steps are
        // rendered by synthesizing a ButtonRemapHandler fragment and
        // recursing into THIS SAME function, so a macro button step reads
        // exactly like any other button reference in the app (e.g. "vJ1 B3")
        // instead of duplicating that formatting; Wait steps borrow
        // DelayAction's own "Delay %1ms" label the same way. Key/mouse steps
        // have no existing top-level handler shape to borrow from, so
        // they're formatted directly here. A trailing "+"/"-" marks a
        // press/release half of a discrete step (Wait/MouseScroll have
        // neither - they're not press/release pairs).
        const QJsonArray steps =
            binding.value(QStringLiteral("parameters")).toObject().value(QStringLiteral("steps")).toArray();
        QStringList stepLabels;
        for (const QJsonValue &stepValue : steps) {
            const QJsonObject step = stepValue.toObject();
            const QString stepType = step.value(QStringLiteral("type")).toString();
            QString stepLabel;
            if (stepType == QStringLiteral("PressButton") || stepType == QStringLiteral("ReleaseButton")) {
                QJsonObject fragment;
                fragment[QStringLiteral("actionType")] = QStringLiteral("ButtonRemapHandler");
                fragment[QStringLiteral("targetButton")] = step.value(QStringLiteral("buttonIndex"));
                fragment[QStringLiteral("targetOutputId")] = binding.value(QStringLiteral("targetOutputId"));
                fragment[QStringLiteral("targetDeviceType")] = binding.value(QStringLiteral("targetDeviceType"));
                stepLabel = bindingLabelForActionJson(fragment);
                stepLabel.replace(QStringLiteral("vJoy "), QStringLiteral("vJ"));
                stepLabel.replace(QStringLiteral(" : Btn "), QStringLiteral(" B"));
                stepLabel += stepType == QStringLiteral("PressButton") ? QStringLiteral("+") : QStringLiteral("-");
            } else if (stepType == QStringLiteral("PressKey") || stepType == QStringLiteral("ReleaseKey")) {
                const int scanCode = step.value(QStringLiteral("scanCode")).toInt();
                stepLabel = KeyboardHandler::scanCodeToKeyName(static_cast<uint16_t>(scanCode));
                stepLabel += stepType == QStringLiteral("PressKey") ? QStringLiteral("+") : QStringLiteral("-");
            } else if (stepType == QStringLiteral("PressMouseButton") ||
                       stepType == QStringLiteral("ReleaseMouseButton")) {
                stepLabel = step.value(QStringLiteral("targetAction")).toString();
                stepLabel +=
                    stepType == QStringLiteral("PressMouseButton") ? QStringLiteral("+") : QStringLiteral("-");
            } else if (stepType == QStringLiteral("MouseScroll")) {
                stepLabel = step.value(QStringLiteral("targetAction")).toString();
            } else if (stepType == QStringLiteral("Wait")) {
                QJsonObject fragment;
                fragment[QStringLiteral("actionType")] = QStringLiteral("DelayAction");
                QJsonObject fragmentParameters;
                fragmentParameters[QStringLiteral("delayMs")] = step.value(QStringLiteral("waitMs"));
                fragment[QStringLiteral("parameters")] = fragmentParameters;
                stepLabel = bindingLabelForActionJson(fragment);
            }
            stepLabels.append(stepLabel.isEmpty() ? QStringLiteral("?") : stepLabel);
        }
        label = stepLabels.isEmpty() ? QStringLiteral("Macro")
                                      : QStringLiteral("Macro: %1").arg(stepLabels.join(QStringLiteral(" > ")));
    } else if (label == QStringLiteral("DelayAction")) {
        const int delayMs = binding.value(QStringLiteral("parameters")).toObject()
                                    .value(QStringLiteral("delayMs")).toInt();
        label = QStringLiteral("Delay %1ms").arg(delayMs);
    } else if (label == QStringLiteral("AudioAction")) {
        label = QStringLiteral("Audio");
    } else if (label == QStringLiteral("TTSAction")) {
        label = QStringLiteral("TTS");
    } else if (label == QStringLiteral("TempoHandler")) {
        // Fase 20.17 (extended for cascading actions): a short summary of
        // every action queued for each gesture instead of just the bare
        // "TempoHandler" type name, recursing into this same function so
        // each one renders with the exact same per-type formatting (e.g.
        // "vJoy 1 : Btn 5") a top-level binding of that type would get, then
        // abbreviating + joining them the same way SequenceHandler's own
        // per-step preview does below - so several cascaded actions on one
        // gesture still fit the Pill's width. Prefers the current
        // "shortActions"/"longActions" array schema, falling back to the
        // pre-cascade single-object "shortAction"/"longAction" for profiles
        // saved before TempoHandler supported multiple actions per gesture.
        auto gestureLabel = [](const QJsonObject &b, const QString &arrayKey, const QString &legacyKey) -> QString {
            QJsonArray actions = b.value(arrayKey).toArray();
            if (actions.isEmpty() && b.contains(legacyKey)) {
                actions.append(b.value(legacyKey));
            }
            if (actions.isEmpty()) {
                return QStringLiteral("-");
            }
            QStringList parts;
            for (const QJsonValue &actionValue : actions) {
                QString part = bindingLabelForActionJson(actionValue.toObject());
                if (part.isEmpty()) {
                    part = QStringLiteral("-");
                } else {
                    part.replace(QStringLiteral("vJoy "), QStringLiteral("vJ"));
                    part.replace(QStringLiteral(" : Btn "), QStringLiteral(" B"));
                }
                parts.append(part);
            }
            return parts.join(QStringLiteral("+"));
        };

        const QString shortStr = gestureLabel(binding, QStringLiteral("shortActions"), QStringLiteral("shortAction"));
        const QString longStr = gestureLabel(binding, QStringLiteral("longActions"), QStringLiteral("longAction"));
        label = QStringLiteral("S: %1 / L: %2").arg(shortStr, longStr);
    } else if (label == QStringLiteral("SequenceHandler")) {
        // Fase 20.23: a compact per-step preview instead of just a step
        // count - recurses into this same function so each step renders
        // with its own type's normal formatting (e.g. "vJoy 1 : Btn 5"),
        // then abbreviates that down ("vJoy " -> "vJ", " : Btn " -> " B")
        // so a multi-step sequence still fits the Pill's width instead of
        // blowing it out with several full-length labels chained together.
        const QJsonArray actions =
            binding.value(QStringLiteral("parameters")).toObject().value(QStringLiteral("actions")).toArray();
        QStringList stepLabels;
        for (int i = 0; i < actions.size(); ++i) {
            QString stepLabel = bindingLabelForActionJson(actions[i].toObject());
            if (stepLabel.isEmpty()) {
                stepLabel = QStringLiteral("-");
            } else {
                stepLabel.replace(QStringLiteral("vJoy "), QStringLiteral("vJ"));
                stepLabel.replace(QStringLiteral(" : Btn "), QStringLiteral(" B"));
            }
            stepLabels.append(stepLabel);
        }
        label = QStringLiteral("[%1]").arg(stepLabels.join(QStringLiteral(" > ")));
    } else if (label == QStringLiteral("TemporaryModeSwitch")) {
        const QString targetMode =
            binding.value(QStringLiteral("parameters")).toObject().value(QStringLiteral("targetMode")).toString();
        label = QStringLiteral("Shift -> %1").arg(targetMode);
    } else if (label == QStringLiteral("ModeSwitch")) {
        const QString targetMode =
            binding.value(QStringLiteral("parameters")).toObject().value(QStringLiteral("targetMode")).toString();
        label = QStringLiteral("Toggle -> %1").arg(targetMode);
    } else if (label == QStringLiteral("MergeAxisHandler")) {
        const int targetAxis = binding.value(QStringLiteral("targetAxis")).toInt();
        const bool isSubtraction = binding.value(QStringLiteral("parameters")).toObject()
                                       .value(QStringLiteral("isSubtraction")).toBool(true);
        label = QStringLiteral("Merge -> %1 %2 (%3)").arg(getDeviceLabel(binding), getAxisName(targetAxis),
                                                           isSubtraction ? QStringLiteral("Diff") : QStringLiteral("Add"));
    } else if (label == QStringLiteral("SplitAxisHandler")) {
        const QJsonObject parameters = binding.value(QStringLiteral("parameters")).toObject();
        const int lowerTargetOutputId = parameters.value(QStringLiteral("lowerTargetOutputId")).toInt(1);
        const int lowerTargetAxis = parameters.value(QStringLiteral("lowerTargetAxis")).toInt();
        const int upperTargetOutputId = parameters.value(QStringLiteral("upperTargetOutputId")).toInt(1);
        const int upperTargetAxis = parameters.value(QStringLiteral("upperTargetAxis")).toInt();
        const int splitMode = parameters.value(QStringLiteral("splitMode")).toInt(0);
        label = QStringLiteral("Split (%1) -> vJ%2 %3 / vJ%4 %5")
                    .arg(splitMode == 1 ? QStringLiteral("Seq") : QStringLiteral("Center"))
                    .arg(lowerTargetOutputId)
                    .arg(getAxisName(lowerTargetAxis))
                    .arg(upperTargetOutputId)
                    .arg(getAxisName(upperTargetAxis));
    }
    return label;
}

/// Recursively checks whether binding (or anything nested inside it - a
/// ConditionHandler/MergeAxisHandler wrapped in a Tempo gesture or Sequence
/// step, say) references ANOTHER input's identity as part of its own live
/// logic, rather than just an output target - "ConditionHandler" (gated on a
/// specific modifier button, read live via EventRouter::isButtonPressed() -
/// see that class' own docs) and "MergeAxisHandler" (reads a second "other"
/// axis live via EventRouter::getAxisValue()) are the two kinds that do this
/// today. copyAction() uses this to warn up front that pasting such a copy
/// onto a different input does NOT give it its own independent modifier/
/// other-axis - both copies keep reading the exact same external
/// (systemPath, index) live, at dispatch time, same as if the user had
/// bound the SAME modifier/other-axis to both inputs by hand.
///
/// Same wrapper/cascade traversal shape as ProfileManager's own (internal,
/// mutating) renameModeInBinding() - wrappedAction; TempoHandler's
/// shortActions/longActions/doubleActions and their pre-cascade legacy
/// singular form; SequenceHandler's parameters.actions - kept in sync with
/// that function's depth guard (32) for the same "corrupt/malicious profile
/// nesting a reference cycle" reasoning, though this side only ever walks a
/// single already-in-memory clipboard entry, never untrusted disk JSON.
bool bindingReferencesExternalInput(const QJsonObject &binding, int depth = 0)
{
    if (depth > 32) {
        return false;
    }

    const QString actionType = binding.value(QStringLiteral("actionType")).toString();
    if (actionType == QStringLiteral("ConditionHandler") || actionType == QStringLiteral("MergeAxisHandler")) {
        return true;
    }

    if (binding.contains(QStringLiteral("wrappedAction")) &&
        bindingReferencesExternalInput(binding.value(QStringLiteral("wrappedAction")).toObject(), depth + 1)) {
        return true;
    }

    static const QStringList kActionListKeys{
        QStringLiteral("shortActions"), QStringLiteral("longActions"), QStringLiteral("doubleActions")};
    for (const QString &key : kActionListKeys) {
        const QJsonArray list = binding.value(key).toArray();
        for (const QJsonValue &actionValue : list) {
            if (bindingReferencesExternalInput(actionValue.toObject(), depth + 1)) {
                return true;
            }
        }
    }

    static const QStringList kLegacySingularKeys{
        QStringLiteral("shortAction"), QStringLiteral("longAction"), QStringLiteral("doubleAction")};
    for (const QString &key : kLegacySingularKeys) {
        if (binding.contains(key) && bindingReferencesExternalInput(binding.value(key).toObject(), depth + 1)) {
            return true;
        }
    }

    const QJsonArray sequenceActions =
        binding.value(QStringLiteral("parameters")).toObject().value(QStringLiteral("actions")).toArray();
    for (const QJsonValue &actionValue : sequenceActions) {
        if (bindingReferencesExternalInput(actionValue.toObject(), depth + 1)) {
            return true;
        }
    }

    return false;
}

/// Sprint QoL Part 2: a binding's optional free-text "parameters.note" -
/// ActionPickerPopup.qml/CurveEditorView.qml's own "Nota / Descripción"
/// TextField writes it, InputRow.qml's info icon/ToolTip reads it back via
/// makeInputEntry()'s "actionNote" role. "" (not present, or present but
/// empty) means no note - shared by findBindingLabel() (reading from a live
/// route's own toJson()) and bindAction() (reading from the JSON about to be
/// applied, before it's even routed).
QString noteFromActionJson(const QJsonObject &binding)
{
    return binding.value(QStringLiteral("parameters")).toObject().value(QStringLiteral("note")).toString();
}

/// Finds the (if any) route in routes matching (systemPath, isAxis, index),
/// and - if its handler can describe itself (see IActionHandler::toJson())
/// - writes its display label into outLabel, its optional note into outNote
/// (see noteFromActionJson() - "" if none), and returns true. Shared by
/// makeDeviceEntry() (building a device row for the first time) and
/// updateAllInputBindingLabels() (Fase 20: refreshing every row already
/// built, e.g. right after loadProfileFromPath() repopulates m_router).
bool findBindingLabel(const std::vector<EventRouter::RouteDescriptor> &routes, const QString &systemPath, bool isAxis,
                      int index, const QString &currentMode, QString &outLabel, QString &outNote)
{
    for (const EventRouter::RouteDescriptor &route : routes) {
        if (route.isAxis != isAxis || route.index != index || route.systemPath != systemPath) {
            continue;
        }
        if (route.mode != currentMode) {
            continue;
        }
        if (!route.handler) {
            continue;
        }
        const QJsonObject json = route.handler->toJson();
        if (json.isEmpty() || !json.contains(QStringLiteral("actionType"))) {
            continue; // Handler doesn't know how to describe itself yet - see IActionHandler::toJson().
        }
        outLabel = bindingLabelForActionJson(json);
        // Sprint QoL Part 2: read from the handler object itself, NOT
        // noteFromActionJson(json) - a concrete handler's own toJson() never
        // mentions "note" (it isn't part of any subclass' own serialization
        // logic - see IActionHandler::note()'s own docs), so parsing it back
        // out of `json` here would always come back empty. The note lives
        // on the base IActionHandler class instead, set once by
        // ProfileManager::instantiateHandler()'s wrapper around
        // instantiateHandlerImpl() when this route was first loaded/bound.
        outNote = route.handler->note();
        return true;
    }
    return false;
}

/// Whether node ITSELF (not anything nested under it - see
/// containsMatchingDestination() below for the recursive walk) and b (both
/// IActionHandler::toJson() binding fragments) target the exact same vJoy
/// output - same actionType, same targetOutputId (outputId - node's own if
/// it has one, else whatever its nearest ancestor's was - see
/// containsMatchingDestination()'s inheritedOutputId), and same secondary
/// destination field (whichever of targetButton/targetAxis/
/// targetHat+targetDirection both sides actually have; a HatRemapHandler's
/// targetDirection is part of its own destination - two different
/// directions on the same hat are not a collision). Neither side matching
/// any of the three known secondary fields is treated as "not the same
/// destination" rather than a false match. This is the original (Fase
/// 20.2) flat-only comparison, unchanged - containsMatchingDestination()
/// below is what adds the recursive walk on top of it.
bool flatDestinationMatches(const QJsonObject &node, int outputId, const QJsonObject &b)
{
    if (node.value(QStringLiteral("actionType")).toString() != b.value(QStringLiteral("actionType")).toString()) {
        return false;
    }
    if (outputId != b.value(QStringLiteral("targetOutputId")).toInt()) {
        return false;
    }
    if (node.contains(QStringLiteral("targetButton")) && b.contains(QStringLiteral("targetButton"))) {
        return node.value(QStringLiteral("targetButton")).toInt() == b.value(QStringLiteral("targetButton")).toInt();
    }
    if (node.contains(QStringLiteral("targetAxis")) && b.contains(QStringLiteral("targetAxis"))) {
        return node.value(QStringLiteral("targetAxis")).toInt() == b.value(QStringLiteral("targetAxis")).toInt();
    }
    if (node.contains(QStringLiteral("targetHat")) && b.contains(QStringLiteral("targetHat"))) {
        return node.value(QStringLiteral("targetHat")).toInt() == b.value(QStringLiteral("targetHat")).toInt() &&
               node.value(QStringLiteral("targetDirection")).toInt() == b.value(QStringLiteral("targetDirection")).toInt();
    }
    return false;
}

/// Whether ANY vJoy slot embedded directly in a container node's own
/// "parameters" (not a nested child *action* - see containsMatchingDestination()
/// for those) matches b's own destination: MacroHandler's
/// "parameters.steps[].buttonIndex" (PressButton/ReleaseButton steps - see
/// MacroHandler::toJson()) and AxisSplitterHandler's
/// "parameters.zones[].targetButton" (see AxisSplitterHandler::toJson())
/// both share their parent node's own targetOutputId (outputId) rather than
/// carrying one of their own, so they're compared against b purely on
/// (outputId, button index) - NOT on actionType, unlike
/// flatDestinationMatches() above: a step/zone is a raw slot reference, not
/// an action fragment of its own, so "does b ultimately drive this exact
/// vJoy button" is the only question that matters, regardless of what kind
/// of action b happens to be. SplitAxisHandler's own
/// "parameters.lower/upperTargetOutputId" + "parameters.lower/upperTargetAxis"
/// pairs are fully self-contained (neither shares outputId) and are checked
/// the same way, against b's targetAxis instead.
bool parametersContainMatchingSlot(const QJsonObject &parameters, int outputId, const QJsonObject &b)
{
    if (b.contains(QStringLiteral("targetButton")) && outputId == b.value(QStringLiteral("targetOutputId")).toInt()) {
        const int wantedButton = b.value(QStringLiteral("targetButton")).toInt();

        const QJsonArray steps = parameters.value(QStringLiteral("steps")).toArray();
        for (const QJsonValue &stepValue : steps) {
            const QJsonObject step = stepValue.toObject();
            if (step.contains(QStringLiteral("buttonIndex"))
                && step.value(QStringLiteral("buttonIndex")).toInt() == wantedButton) {
                return true;
            }
        }

        const QJsonArray zones = parameters.value(QStringLiteral("zones")).toArray();
        for (const QJsonValue &zoneValue : zones) {
            const QJsonObject zone = zoneValue.toObject();
            if (zone.contains(QStringLiteral("targetButton"))
                && zone.value(QStringLiteral("targetButton")).toInt() == wantedButton) {
                return true;
            }
        }
    }

    if (b.contains(QStringLiteral("targetAxis"))) {
        const int wantedAxis = b.value(QStringLiteral("targetAxis")).toInt();
        const int wantedOutputId = b.value(QStringLiteral("targetOutputId")).toInt();
        if (parameters.contains(QStringLiteral("lowerTargetAxis"))
            && parameters.value(QStringLiteral("lowerTargetOutputId")).toInt() == wantedOutputId
            && parameters.value(QStringLiteral("lowerTargetAxis")).toInt() == wantedAxis) {
            return true;
        }
        if (parameters.contains(QStringLiteral("upperTargetAxis"))
            && parameters.value(QStringLiteral("upperTargetOutputId")).toInt() == wantedOutputId
            && parameters.value(QStringLiteral("upperTargetAxis")).toInt() == wantedAxis) {
            return true;
        }
    }

    return false;
}

/// Recursively walks a's own binding tree - a itself, then (if a is a
/// container) everything nested under "wrappedAction"
/// (ConditionHandler/ToggleHandler/AxisToButtonHandler),
/// "shortActions"/"longActions"/"doubleActions" (TempoHandler),
/// "parameters.actions" (SequenceHandler), plus the embedded-slot cases
/// parametersContainMatchingSlot() covers (MacroHandler/AxisSplitterHandler/
/// SplitAxisHandler) - looking for any node/slot that targets the same vJoy
/// destination as b. inheritedOutputId is the nearest enclosing
/// targetOutputId a nested node without its own falls back to, mirroring
/// ProfileManager::scanVjoyOccupancy()'s own inheritance rule (built for the
/// vJoy Auditor) - this walk deliberately covers the exact same nested
/// shapes that function does, so a bind buried inside a Tempo cascade, a
/// Toggle's wrappedAction, or a Macro's own steps is caught here just as
/// reliably as it's counted "occupied" there. If ProfileManager ever grows
/// a new wrapped/cascaded action shape, this needs the same update.
bool containsMatchingDestination(const QJsonObject &a, int inheritedOutputId, const QJsonObject &b)
{
    const int outputId = a.contains(QStringLiteral("targetOutputId"))
        ? a.value(QStringLiteral("targetOutputId")).toInt()
        : inheritedOutputId;

    if (flatDestinationMatches(a, outputId, b)) {
        return true;
    }

    const QJsonValue wrapped = a.value(QStringLiteral("wrappedAction"));
    if (wrapped.isObject() && containsMatchingDestination(wrapped.toObject(), outputId, b)) {
        return true;
    }

    static const QStringList kActionListKeys = {QStringLiteral("shortActions"), QStringLiteral("longActions"),
                                                   QStringLiteral("doubleActions")};
    for (const QString &key : kActionListKeys) {
        const QJsonArray list = a.value(key).toArray();
        for (const QJsonValue &value : list) {
            if (value.isObject() && containsMatchingDestination(value.toObject(), outputId, b)) {
                return true;
            }
        }
    }

    const QJsonObject parameters = a.value(QStringLiteral("parameters")).toObject();
    if (parameters.isEmpty()) {
        return false;
    }

    const QJsonArray actions = parameters.value(QStringLiteral("actions")).toArray();
    for (const QJsonValue &value : actions) {
        if (value.isObject() && containsMatchingDestination(value.toObject(), outputId, b)) {
            return true;
        }
    }

    return parametersContainMatchingSlot(parameters, outputId, b);
}

/// Whether a (a live route's own IActionHandler::toJson(), possibly a
/// container wrapping/cascading other actions) and b (bindAction()'s new
/// binding - always a flat leaf, never itself walked) target the exact same
/// vJoy output (Fase 20.2, extended to look inside containers) - used by
/// bindAction() to keep an output from ever being driven by two different
/// physical inputs at once, even when the old binding is nested inside a
/// Toggle/Condition/Tempo/Sequence/Macro/SplitAxis wrapper instead of a
/// plain top-level action. See containsMatchingDestination()'s own docs for
/// exactly which nested shapes are covered.
bool sameBindingDestination(const QJsonObject &a, const QJsonObject &b)
{
    return containsMatchingDestination(a, /*inheritedOutputId=*/0, b);
}
} // namespace

ProfileEditorViewModel::ProfileEditorViewModel(EventRouter &router, AutoSwitchManager &autoSwitch,
                                                 PwaServer &pwaServer, QObject *parent)
    : QAbstractListModel(parent)
    , m_router(router)
    , m_autoSwitch(autoSwitch)
    , m_pwaServer(pwaServer)
    , m_currentMode(EventRouter::kGlobalMode)
{
    m_currentProfileData[QStringLiteral("profileName")] = QStringLiteral("Untitled");
    m_currentProfileData[QStringLiteral("bindings")] = QJsonArray();

    m_modes.append(EventRouter::kGlobalMode);

    // Fase (drag-to-reorder device tabs): loaded before the initial device
    // population loop below so that loop's own deviceInsertionIndex() calls
    // already honor whatever order the user last dragged into, instead of
    // plain discovery order on every subsequent launch.
    m_deviceTabOrder = QSettings()
                           .value(QStringLiteral("DeviceTabOrder"))
                           .toString()
                           .split(QLatin1Char(','), Qt::SkipEmptyParts);

    const QList<DeviceInfo> connected = DeviceManager::instance().getConnectedDevices();
    for (const DeviceInfo &device : connected) {
        DeviceEntry entry = makeDeviceEntry(device, m_router);
        const int insertAt = deviceInsertionIndex(entry.vendorProduct, entry.systemPath);
        m_devices.insert(insertAt, std::move(entry));
    }

    connect(&DeviceManager::instance(), &DeviceManager::deviceAdded, this, &ProfileEditorViewModel::onDeviceAdded);
    connect(&DeviceManager::instance(), &DeviceManager::deviceRemoved, this, &ProfileEditorViewModel::onDeviceRemoved);
    connect(&DeviceManager::instance(), &DeviceManager::axisMoved, this,
            &ProfileEditorViewModel::onAxisMovedForDetection);
    connect(&DeviceManager::instance(), &DeviceManager::buttonPressed, this,
            &ProfileEditorViewModel::onButtonPressedForDetection);

    // Fase 20.4: reopen whatever profile was last loaded/saved, so the app
    // doesn't always start on the empty "Untitled" profile the user then has
    // to go find manually.
    const QString lastProfile = QSettings().value(QStringLiteral("LastProfile")).toString();
    if (!lastProfile.isEmpty() && QFile::exists(lastProfile)) {
        loadProfileFromPath(lastProfile);
    }
}

int ProfileEditorViewModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_devices.size();
}

QVariant ProfileEditorViewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_devices.size()) {
        return {};
    }

    const DeviceEntry &device = m_devices.at(index.row());
    switch (role) {
    case DeviceNameRole: return device.name;
    case VendorProductRole: return device.vendorProduct;
    case SystemPathRole: return device.systemPath;
    case InputsRole: return device.inputs;
    case TesterDisplayNameRole: {
        if (!device.name.contains(QStringLiteral("vjoy"), Qt::CaseInsensitive)) {
            return device.name;
        }
        int vjoyOrdinal = 0;
        for (int row = 0; row <= index.row(); ++row) {
            if (m_devices.at(row).name.contains(QStringLiteral("vjoy"), Qt::CaseInsensitive)) {
                ++vjoyOrdinal;
            }
        }
        return tr("vJoy %1 Output").arg(vjoyOrdinal);
    }
    default: return {};
    }
}

QHash<int, QByteArray> ProfileEditorViewModel::roleNames() const
{
    return {
        {DeviceNameRole, "deviceName"},
        {VendorProductRole, "vendorProduct"},
        {SystemPathRole, "systemPath"},
        {InputsRole, "inputs"},
        {TesterDisplayNameRole, "testerDisplayName"},
    };
}

QStringList ProfileEditorViewModel::axisNamesForDevice(int deviceRow) const
{
    if (deviceRow < 0 || deviceRow >= m_devices.size()) {
        return {};
    }

    QStringList names;
    const QVariantList &inputs = m_devices.at(deviceRow).inputs;
    for (const QVariant &inputVariant : inputs) {
        const QVariantMap input = inputVariant.toMap();
        if (input.value(QStringLiteral("kind")).toString() == QStringLiteral("axis")) {
            names.append(input.value(QStringLiteral("name")).toString());
        }
    }
    return names;
}

QString ProfileEditorViewModel::systemPathForDevice(int deviceRow) const
{
    if (deviceRow < 0 || deviceRow >= m_devices.size()) {
        return QString();
    }
    return m_devices.at(deviceRow).systemPath;
}

bool ProfileEditorViewModel::loadProfileFromPath(const QString &filePath)
{
    const QString localPath = QUrl(filePath).isLocalFile() ? QUrl(filePath).toLocalFile() : filePath;

    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "ProfileEditorViewModel: could not open profile" << localPath << ":" << file.errorString();
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "ProfileEditorViewModel: profile" << localPath << "is not a valid JSON object";
        return false;
    }

    if (!m_profileManager.loadProfile(localPath, m_router)) {
        return false;
    }

    // Cached only once the load has actually succeeded, so a failed Load
    // never clobbers whatever profile Save would otherwise still round-trip.
    m_currentProfileData = doc.object();
    // Fase 20.3: the file's own basename always wins over whatever
    // "profileName" (if any) is stored inside the JSON - two profiles saved
    // under different filenames but with the same (or no) internal
    // profileName previously showed identical/blank names in the UI, which
    // didn't actually identify which file was loaded.
    //
    // Bugfix: baseName() itself can come back "" for a degenerate path (no
    // filename component at all - e.g. one ending in a path separator) -
    // and unlike a genuinely MISSING "profileName" key, an explicitly
    // stored empty string is NOT rescued by currentProfileName()'s own
    // QJsonValue::toString("Untitled") default (that default only applies
    // when the stored value's type isn't String at all - Qt still
    // considers "" a perfectly valid String). Left uncaught, that's exactly
    // the "profile name renders as an empty/corrupt string" bug this
    // fallback exists for - sanitize it here, once, so every reader of
    // "profileName" (currentProfileName(), saveProfileToPath(),
    // exportCheatsheetPdf(), quickSave()'s Save dialog default, ...) sees a
    // real name instead of silently inheriting an empty one.
    // Bugfix: baseName() truncates at the FIRST '.' in the filename, not just
    // the extension - a profile named "...[ENH][NXT][4.8.0][LIVE][R14].json"
    // was being shown as "...[ENH][NXT][4" (everything after the "4.8.0"
    // version string's own dot got silently dropped). completeBaseName()
    // only strips the last '.' onward (the actual extension), matching what
    // LegacyProfileImporter/R14ProfileImporter already do for their own
    // "Imported: %1" name.
    QString importedName = QFileInfo(localPath).completeBaseName().trimmed();
    if (importedName.isEmpty()) {
        importedName = QStringLiteral("Untitled");
    }
    m_currentProfileData[QStringLiteral("profileName")] = importedName;
    m_currentFilePath = localPath;
    emit currentProfileNameChanged();
    QSettings().setValue(QStringLiteral("LastProfile"), localPath);

    rebuildDeviceListFromRouter();
    return true;
}

void ProfileEditorViewModel::rebuildDeviceListFromRouter()
{
    m_modes.clear();
    m_modes.append(EventRouter::kGlobalMode);
    for (const EventRouter::RouteDescriptor &route : m_router.allRoutes()) {
        if (!m_modes.contains(route.mode)) {
            m_modes.append(route.mode);
        }
    }
    emit modesChanged();

    // Fase SC-7.3: synthesize an offline placeholder row for any
    // sourceDevice the freshly-loaded routes reference that isn't a real,
    // currently-connected device - e.g. a "[Legacy] <name>" string
    // LegacyProfileImporter tags every imported binding's source with, or a
    // real device that simply isn't plugged in right now. Without a row,
    // that source has nothing to select in Swap Devices' ComboBoxes at all,
    // so its bindings could never be retargeted onto real hardware.
    // Swap To suggestion (see DeviceEntry::orphanedDeviceName's own docs):
    // built from the raw loaded JSON (m_currentProfileData - the caller is
    // responsible for it already reflecting whatever was just loaded, be it
    // a file or an Undo snapshot), not m_router - route.handler->toJson()
    // never carries "sourceDeviceName" (it's bolted onto the binding only at
    // serializeProfile() time, alongside sourceDevice/sourceAxis/mode - see
    // that function), so it doesn't survive being instantiated into a
    // RouteDescriptor. A device absent here (an old profile saved before
    // this field existed, or one never saved while its device was
    // connected) just leaves orphanedDeviceName empty below - no worse than
    // before this existed.
    QHash<QString, QString> orphanNameBySourceDevice;
    for (const QJsonValue &val : m_currentProfileData.value(QStringLiteral("bindings")).toArray()) {
        const QJsonObject binding = val.toObject();
        const QString sourceDevice = binding.value(QStringLiteral("sourceDevice")).toString();
        const QString sourceDeviceName = binding.value(QStringLiteral("sourceDeviceName")).toString();
        if (!sourceDevice.isEmpty() && !sourceDeviceName.isEmpty()) {
            orphanNameBySourceDevice.insert(sourceDevice, sourceDeviceName);
        }
    }

    QSet<QString> seenOrphanDevices;
    for (const EventRouter::RouteDescriptor &route : m_router.allRoutes()) {
        const QString &sourceDevice = route.systemPath;
        if (sourceDevice.isEmpty() || seenOrphanDevices.contains(sourceDevice) || indexOfSystemPath(sourceDevice) >= 0) {
            continue;
        }
        seenOrphanDevices.insert(sourceDevice);

        const QString knownName = orphanNameBySourceDevice.value(sourceDevice);

        DeviceEntry virtualDev;
        virtualDev.name = knownName.isEmpty() ? offlineDeviceLabel(sourceDevice)
                                               : QStringLiteral("Offline: %1").arg(knownName);
        virtualDev.systemPath = sourceDevice;
        virtualDev.vendorProduct = QStringLiteral("Offline / Imported Device");
        virtualDev.isMock = true;
        virtualDev.orphanedDeviceName = knownName;
        virtualDev.numAxes = 8;
        virtualDev.numButtons = 128;
        for (int i = 0; i < virtualDev.numAxes; ++i) {
            virtualDev.inputs.append(
                makeInputEntry(QStringLiteral("Axis %1").arg(i), QStringLiteral("axis"), i, false, QString()));
        }
        for (int i = 0; i < virtualDev.numButtons; ++i) {
            virtualDev.inputs.append(
                makeInputEntry(QStringLiteral("Button %1").arg(i + 1), QStringLiteral("button"), i, false, QString()));
        }

        beginInsertRows(QModelIndex(), m_devices.size(), m_devices.size());
        m_devices.append(virtualDev);
        endInsertRows();
    }

    // Fase SC-7.4: moved here, after the orphan-device rows above are
    // inserted - calling this earlier meant every "[Legacy] ..." row was
    // still built with hasBinding=false baked in below by makeInputEntry(),
    // since updateAllInputBindingLabels() had already finished inspecting
    // the router before those rows even existed to inspect.
    updateAllInputBindingLabels();
}

void ProfileEditorViewModel::pushUndoSnapshot(const QString &description)
{
    m_undoSnapshot = m_profileManager.serializeProfile(m_router, currentProfileName());
    emit undoableActionPerformed(description);
}

bool ProfileEditorViewModel::undoLastAction()
{
    if (m_undoSnapshot.isEmpty()) {
        return false;
    }

    if (!m_profileManager.loadProfileFromJson(m_undoSnapshot, m_router, QStringLiteral("undo snapshot"))) {
        return false;
    }

    // profileName round-trips through m_undoSnapshot itself (serializeProfile()
    // always writes it) - reusing it wholesale here, rather than just the
    // "bindings"/"modeHierarchy" router-facing parts, keeps rebuildDeviceListFromRouter()'s
    // own orphan-name lookup (reads m_currentProfileData) working exactly
    // as it does for a normal file load.
    m_currentProfileData = m_undoSnapshot;
    m_undoSnapshot = QJsonObject();

    rebuildDeviceListFromRouter();
    return true;
}

bool ProfileEditorViewModel::saveProfileToPath(const QString &filePath)
{
    const QString localPath = QUrl(filePath).isLocalFile() ? QUrl(filePath).toLocalFile() : filePath;
    const QString profileName =
        m_currentProfileData.value(QStringLiteral("profileName")).toString(QStringLiteral("Untitled"));
    const QJsonObject liveProfile = m_profileManager.serializeProfile(m_router, profileName);
    if (!m_profileManager.saveProfile(localPath, liveProfile)) {
        return false;
    }
    m_currentFilePath = localPath;
    QSettings().setValue(QStringLiteral("LastProfile"), localPath);
    return true;
}

void ProfileEditorViewModel::quickSave()
{
    if (m_currentFilePath.isEmpty()) {
        emit saveDialogRequested();
    } else {
        saveProfileToPath(m_currentFilePath);
        qInfo() << "Quick saved to:" << m_currentFilePath;
    }
}

int ProfileEditorViewModel::deviceRowForSystemPath(const QString &systemPath) const
{
    return indexOfSystemPath(systemPath);
}

int ProfileEditorViewModel::curvesTargetDeviceRow() const
{
    return m_curvesTargetDeviceRow;
}

QString ProfileEditorViewModel::curvesTargetInputName() const
{
    return m_curvesTargetInputName;
}

void ProfileEditorViewModel::setCurrentDeviceForCurves(const QString &systemPath)
{
    setCurveEditorTarget(systemPath, QString());
}

void ProfileEditorViewModel::setCurveEditorTarget(const QString &systemPath, const QString &inputName)
{
    const int row = indexOfSystemPath(systemPath);
    if (row < 0) {
        return;
    }
    m_curvesTargetDeviceRow = row;
    m_curvesTargetInputName = inputName;
    emit curvesTargetDeviceRowChanged();
    emit curvesTargetInputNameChanged();
}

bool ProfileEditorViewModel::bindAction(const QString &devicePath, const QString &inputName, const QString &mode,
                                         const QString &actionDataJson)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(actionDataJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "ProfileEditorViewModel::bindAction: actionDataJson is not a valid JSON object:"
                    << parseError.errorString();
        return false;
    }

    const int deviceRow = indexOfSystemPath(devicePath);
    if (deviceRow < 0) {
        qWarning() << "ProfileEditorViewModel::bindAction: unknown device" << devicePath;
        return false;
    }

    int resolvedIndex = -1;
    bool isAxis = false;
    const QVariantList &inputs = m_devices.at(deviceRow).inputs;
    for (const QVariant &inputVariant : inputs) {
        const QVariantMap input = inputVariant.toMap();
        if (input.value(QStringLiteral("name")).toString() == inputName) {
            resolvedIndex = input.value(QStringLiteral("inputIndex")).toInt();
            isAxis = input.value(QStringLiteral("kind")).toString() == QStringLiteral("axis");
            break;
        }
    }
    if (resolvedIndex < 0) {
        qWarning() << "ProfileEditorViewModel::bindAction: unknown input" << inputName << "on device" << devicePath;
        return false;
    }

    QJsonObject binding = doc.object();
    binding[QStringLiteral("sourceDevice")] = devicePath;
    binding[QStringLiteral("mode")] = mode.isEmpty() ? EventRouter::kGlobalMode : mode;
    if (isAxis) {
        binding[QStringLiteral("sourceAxis")] = resolvedIndex;
    } else {
        binding[QStringLiteral("sourceButton")] = resolvedIndex;
    }

    if (!m_profileManager.applyBinding(binding, m_router)) {
        return false;
    }

    // Fase 20.2: exclusive output mapping - if some other physical input was
    // already driving the exact same vJoy destination this binding just
    // claimed, cut that old route loose (nullptr handler) so an output is
    // never silently fought over by two sources. Excludes the route for the
    // input just (re)bound above, which now legitimately owns this destination.
    for (const EventRouter::RouteDescriptor &route : m_router.allRoutes()) {
        if (!route.handler) {
            continue;
        }
        if (route.systemPath == devicePath && route.index == resolvedIndex && route.isAxis == isAxis) {
            continue;
        }
        if (!sameBindingDestination(route.handler->toJson(), binding)) {
            continue;
        }

        if (route.isAxis) {
            m_router.addAxisRoute(route.systemPath, route.index, nullptr, route.mode);
        } else {
            m_router.addButtonRoute(route.systemPath, route.index, nullptr, route.mode);
        }

        const int oldDeviceRow = indexOfSystemPath(route.systemPath);
        if (oldDeviceRow < 0) {
            continue;
        }
        for (const QVariant &oldInputVariant : m_devices.at(oldDeviceRow).inputs) {
            const QVariantMap oldInput = oldInputVariant.toMap();
            const bool oldIsAxis = oldInput.value(QStringLiteral("kind")).toString() == QStringLiteral("axis");
            if (oldIsAxis == route.isAxis && oldInput.value(QStringLiteral("inputIndex")).toInt() == route.index) {
                updateInputBindingLabel(oldDeviceRow, oldInput.value(QStringLiteral("name")).toString(), QString());
                break;
            }
        }
    }

    const QString label = bindingLabelForActionJson(binding);
    const QString note = noteFromActionJson(binding);
    updateInputBindingLabel(deviceRow, inputName, label, note);
    return true;
}

void ProfileEditorViewModel::unbindAction(const QString &devicePath, const QString &inputName, const QString &mode)
{
    const int deviceRow = indexOfSystemPath(devicePath);
    if (deviceRow < 0) {
        qWarning() << "ProfileEditorViewModel::unbindAction: unknown device" << devicePath;
        return;
    }

    int resolvedIndex = -1;
    bool isAxis = false;
    const QVariantList &inputs = m_devices.at(deviceRow).inputs;
    for (const QVariant &inputVariant : inputs) {
        const QVariantMap input = inputVariant.toMap();
        if (input.value(QStringLiteral("name")).toString() == inputName) {
            resolvedIndex = input.value(QStringLiteral("inputIndex")).toInt();
            isAxis = input.value(QStringLiteral("kind")).toString() == QStringLiteral("axis");
            break;
        }
    }
    if (resolvedIndex < 0) {
        qWarning() << "ProfileEditorViewModel::unbindAction: unknown input" << inputName << "on device" << devicePath;
        return;
    }

    const QString resolvedMode = mode.isEmpty() ? EventRouter::kGlobalMode : mode;
    if (isAxis) {
        m_router.addAxisRoute(devicePath, resolvedIndex, nullptr, resolvedMode);
    } else {
        m_router.addButtonRoute(devicePath, resolvedIndex, nullptr, resolvedMode);
    }

    updateInputBindingLabel(deviceRow, inputName, QString());
}

QString ProfileEditorViewModel::getActionDataJson(const QString &devicePath, const QString &inputName,
                                                    const QString &mode) const
{
    const QString resolvedMode = mode.isEmpty() ? EventRouter::kGlobalMode : mode;

    const int deviceRow = indexOfSystemPath(devicePath);
    if (deviceRow < 0) {
        return QString();
    }

    int resolvedIndex = -1;
    bool isAxis = false;
    const QVariantList &inputs = m_devices.at(deviceRow).inputs;
    for (const QVariant &inputVariant : inputs) {
        const QVariantMap input = inputVariant.toMap();
        if (input.value(QStringLiteral("name")).toString() == inputName) {
            resolvedIndex = input.value(QStringLiteral("inputIndex")).toInt();
            isAxis = input.value(QStringLiteral("kind")).toString() == QStringLiteral("axis");
            break;
        }
    }
    if (resolvedIndex < 0) {
        return QString();
    }

    for (const EventRouter::RouteDescriptor &route : m_router.allRoutes()) {
        if (route.systemPath == devicePath && route.isAxis == isAxis && route.index == resolvedIndex &&
            route.mode == resolvedMode) {
            if (route.handler) {
                QJsonObject json = route.handler->toJson();
                // Sprint QoL Part 2: route.handler->toJson() itself never
                // mentions "note" (see IActionHandler::note()'s own docs) -
                // merged in here the same way ProfileManager::
                // serializeProfile() does for the saved-to-disk JSON, so
                // ActionPickerPopup.qml's/CurveEditorView.qml's own "Nota /
                // Descripción" field restores correctly when re-opening an
                // already-bound input, not just right after Apply.
                const QString note = route.handler->note();
                if (!note.isEmpty()) {
                    QJsonObject parameters = json.value(QStringLiteral("parameters")).toObject();
                    parameters[QStringLiteral("note")] = note;
                    json[QStringLiteral("parameters")] = parameters;
                }
                return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
            }
            break;
        }
    }
    return QString();
}

bool ProfileEditorViewModel::hasCurve(const QString &devicePath, const QString &inputName) const
{
    const QString json = getActionDataJson(devicePath, inputName, m_currentMode);
    if (json.isEmpty()) {
        return false;
    }

    const QJsonObject binding = QJsonDocument::fromJson(json.toUtf8()).object();
    if (binding.value(QStringLiteral("actionType")).toString() != QStringLiteral("CurveHandler")) {
        return false;
    }

    // Every axis binding is a CurveHandler internally (it's also what
    // applies deadzone/sensitivity/input-range scaling), so actionType
    // alone can't tell a user-shaped curve apart from a plain linear
    // pass-through - inspect "parameters" itself instead.
    const QJsonObject parameters = binding.value(QStringLiteral("parameters")).toObject();

    // Chequeo de Puntos: CurveHandler::toJson() only writes "curvePoints" at
    // all when m_curvePoints is non-empty (see CurveHandler.cpp), so a
    // straight-line default axis has no such key and curvePoints.size()
    // below is simply 0 - falls through to the sensitivity check, not a
    // false positive. More than 2 points is unambiguously a custom curve
    // (S-curve or any other multi-point shape). Exactly 2 points is only
    // "custom" if they aren't the default straight-line endpoints
    // [-1,-1]->[1,1] - i.e. the user inverted or clipped the line.
    const QJsonArray curvePoints = parameters.value(QStringLiteral("curvePoints")).toArray();
    if (curvePoints.size() > 2) {
        return true;
    }
    if (curvePoints.size() == 2) {
        const QJsonObject p0 = curvePoints.at(0).toObject();
        const QJsonObject p1 = curvePoints.at(1).toObject();
        const double x0 = p0.value(QStringLiteral("x")).toDouble();
        const double y0 = p0.value(QStringLiteral("y")).toDouble();
        const double x1 = p1.value(QStringLiteral("x")).toDouble();
        const double y1 = p1.value(QStringLiteral("y")).toDouble();
        if (x0 != -1.0 || y0 != -1.0 || x1 != 1.0 || y1 != 1.0) {
            return true;
        }
    }

    // Chequeo de Sensibilidad: cualquier valor distinto del neutro 1.0
    // means the user pushed the curve's overall gain up or down.
    if (parameters.value(QStringLiteral("sensitivity")).toDouble(1.0) != 1.0) {
        return true;
    }

    // Exclusiones deliberadas: "deadzone"/"inputMin"/"inputMax" (and any
    // other pure input-range scaling parameter) are NOT checked - an axis
    // with a deadzone but an otherwise straight line at sensitivity 1.0 is
    // still the "base curve" as far as this indicator is concerned, not a
    // user-authored shape.
    return false;
}

void ProfileEditorViewModel::newProfile()
{
    m_router.clearRoutes();

    m_currentProfileData = QJsonObject();
    m_currentProfileData[QStringLiteral("profileName")] = QStringLiteral("Untitled");
    m_currentFilePath.clear();

    m_modes.clear();
    m_modes.append(EventRouter::kGlobalMode);
    emit modesChanged();
    setCurrentMode(EventRouter::kGlobalMode);

    updateAllInputBindingLabels();
    emit currentProfileNameChanged();
}

int ProfileEditorViewModel::nextAvailableVjoyButton(int targetOutputId) const
{
    const ProfileManager::VJoyOccupancy occupancy = m_profileManager.vjoyOccupancy(targetOutputId, m_router);
    const int total = occupancy.buttons.size();
    if (total <= 0) {
        return 0;
    }

    // -1 (nothing recorded yet for this device) makes the very first
    // candidate below (startAfter + 1) % total == 0, i.e. button 1 - the
    // same starting point the old implementation defaulted to.
    const int startAfter = m_lastAssignedVjoyButton.value(targetOutputId, -1);

    for (int offset = 1; offset <= total; ++offset) {
        const int candidate = (startAfter + offset) % total;
        if (!occupancy.buttons.at(candidate)) {
            return candidate;
        }
    }

    // Every one of the 128 buttons on this device is already occupied -
    // not a meaningful "free slot" to suggest, but a safe, non-throwing
    // fallback rather than looping forever or returning an out-of-range index.
    return 0;
}

void ProfileEditorViewModel::recordVjoyButtonAssigned(int targetOutputId, int targetButton)
{
    m_lastAssignedVjoyButton[targetOutputId] = targetButton;
}

bool ProfileEditorViewModel::create1to1Mapping(const QString &devicePath, int targetOutputId)
{
    const int deviceRow = indexOfSystemPath(devicePath);
    if (deviceRow < 0) {
        qWarning() << "ProfileEditorViewModel::create1to1Mapping: unknown device" << devicePath;
        return false;
    }
    if (targetOutputId < 1 || targetOutputId > 16) {
        qWarning() << "ProfileEditorViewModel::create1to1Mapping: targetOutputId" << targetOutputId
                    << "is outside vJoy's valid range [1, 16]";
        return false;
    }

    // Copied, not a reference: applyBinding()'s eventual updateInputBindingLabel()
    // call mutates m_devices[deviceRow].inputs in place, which would otherwise
    // invalidate this same loop's iteration over the live list mid-pass.
    const QVariantList inputs = m_devices.at(deviceRow).inputs;

    // Fase 20: a POV hat's 4 synthetic Up/Right/Down/Left buttons (see
    // buttonDisplayName()) live past a device's real physical buttons within
    // numButtons - 1:1 mapping one of those straight through as a plain
    // ButtonRemapHandler targets a vJoy button index (128+) vJoy doesn't
    // support, so those specific inputIndex values need to become a
    // HatRemapHandler targeting a vJoy POV hat instead.
    int numButtons = 0;
    int numHats = 0;
    const QList<DeviceInfo> connected = DeviceManager::instance().getConnectedDevices();
    for (const DeviceInfo &d : connected) {
        if (d.systemPath == devicePath) {
            numButtons = d.numButtons;
            numHats = d.numHats;
            break;
        }
    }
    const int physicalButtons = numButtons - (numHats * 4);

    int mapped = 0;
    for (const QVariant &inputVariant : inputs) {
        const QVariantMap input = inputVariant.toMap();
        const QString inputName = input.value(QStringLiteral("name")).toString();
        const QString kind = input.value(QStringLiteral("kind")).toString();
        const int inputIndex = input.value(QStringLiteral("inputIndex")).toInt();
        const bool isAxis = kind == QStringLiteral("axis");

        QJsonObject binding;
        binding[QStringLiteral("sourceDevice")] = devicePath;
        binding[QStringLiteral("mode")] = m_currentMode;
        binding[QStringLiteral("targetOutputId")] = targetOutputId;

        if (isAxis) {
            binding[QStringLiteral("sourceAxis")] = inputIndex;
            binding[QStringLiteral("actionType")] = QStringLiteral("CurveHandler");
            binding[QStringLiteral("targetAxis")] = inputIndex;
        } else {
            if (inputIndex >= physicalButtons && physicalButtons > 0) {
                const int hatFlat = inputIndex - physicalButtons;
                binding[QStringLiteral("sourceButton")] = inputIndex;
                binding[QStringLiteral("actionType")] = QStringLiteral("HatRemapHandler");
                binding[QStringLiteral("targetHat")] = hatFlat / 4;
                binding[QStringLiteral("targetDirection")] = hatFlat % 4;
            } else {
                binding[QStringLiteral("sourceButton")] = inputIndex;
                binding[QStringLiteral("actionType")] = QStringLiteral("ButtonRemapHandler");
                binding[QStringLiteral("targetButton")] = inputIndex;
            }
        }

        if (!m_profileManager.applyBinding(binding, m_router)) {
            continue;
        }
        updateInputBindingLabel(deviceRow, inputName, binding.value(QStringLiteral("actionType")).toString());
        ++mapped;
    }

    qInfo() << "ProfileEditorViewModel: 1:1 mapped" << mapped << "of" << inputs.size() << "input(s) on" << devicePath
             << "to vJoy device" << targetOutputId;
    return mapped > 0;
}

QStringList ProfileEditorViewModel::deviceDisplayNames() const
{
    QStringList names;
    for (const DeviceEntry &device : m_devices) {
        names.append(device.name);
    }
    return names;
}

void ProfileEditorViewModel::addAutoSwitchRule(const QString &exeName, const QString &profilePath)
{
    m_autoSwitch.addRule(exeName, profilePath);
}

void ProfileEditorViewModel::removeAutoSwitchRule(const QString &exeName)
{
    m_autoSwitch.removeRule(exeName);
}

void ProfileEditorViewModel::setAutoSwitchDefaultProfile(const QString &profilePath)
{
    m_autoSwitch.setDefaultProfile(profilePath);
}

QVariantMap ProfileEditorViewModel::autoSwitchRules() const
{
    QVariantMap rules;
    QHashIterator<QString, QString> i(m_autoSwitch.rules());
    while (i.hasNext()) {
        i.next();
        rules.insert(i.key(), i.value());
    }
    return rules;
}

QString ProfileEditorViewModel::autoSwitchDefaultProfile() const
{
    return m_autoSwitch.defaultProfilePath();
}

void ProfileEditorViewModel::openPwaEditor()
{
    // "localhost" (not m_pwaServer.serverIp()'s LAN address) since this is
    // always opened on the same machine PwaServer itself is running on -
    // token is required (see this method's own header docs): index.html's
    // token = params.get('token') || '' would otherwise never match
    // m_pwaServer's real securityToken and the editor session would just
    // sit at the WebSocket's auth timeout.
    const QString url = QStringLiteral("http://localhost:%1/?mode=editor&token=%2")
                             .arg(PwaServer::kHttpPort)
                             .arg(m_pwaServer.securityToken());
    QDesktopServices::openUrl(QUrl(url));
}

ProfileManager &ProfileEditorViewModel::profileManager()
{
    return m_profileManager;
}

bool ProfileEditorViewModel::swapDevices(int fromDeviceRow, int toDeviceRow)
{
    if (fromDeviceRow < 0 || fromDeviceRow >= m_devices.size() || toDeviceRow < 0 ||
        toDeviceRow >= m_devices.size() || fromDeviceRow == toDeviceRow) {
        qWarning() << "ProfileEditorViewModel::swapDevices: invalid device row(s)" << fromDeviceRow << toDeviceRow;
        return false;
    }

    pushUndoSnapshot(tr("Devices swapped"));

    const QString fromPath = m_devices.at(fromDeviceRow).systemPath;
    const QString toPath = m_devices.at(toDeviceRow).systemPath;
    const int targetMaxAxes = m_devices.at(toDeviceRow).numAxes;
    const int targetMaxButtons = m_devices.at(toDeviceRow).numButtons;

    // Fase SC-7.9: Smart Swap - a blind swapDeviceSystemPaths() moved every
    // binding wholesale, including any ghost/out-of-range one (index >=
    // the destination's own numAxes/numButtons) that the destination
    // physically can't represent and would then silently fail to show. Only
    // a binding that actually fits the destination's real input count moves;
    // anything that doesn't stays behind on fromPath as a visible "lost
    // mappings tray" the user can Copy across by hand and clean up later
    // with Clear All, rather than being moved somewhere it can't be seen or
    // acted on.
    int remainingCount = 0;
    const auto routes = m_router.allRoutes();
    for (const auto &route : routes) {
        if (route.systemPath != fromPath) {
            continue;
        }

        bool canMove = false;
        if (route.isAxis && route.index < targetMaxAxes) {
            canMove = true;
        } else if (!route.isAxis && route.index < targetMaxButtons) {
            canMove = true;
        }

        if (canMove) {
            if (route.isAxis) {
                m_router.addAxisRoute(toPath, route.index, route.handler, route.mode);
                m_router.addAxisRoute(fromPath, route.index, nullptr, route.mode);
            } else {
                m_router.addButtonRoute(toPath, route.index, route.handler, route.mode);
                m_router.addButtonRoute(fromPath, route.index, nullptr, route.mode);
            }
        } else {
            ++remainingCount;
        }
    }

    // Only remove the source row if nothing was left behind for it to still
    // show - the same "empty legacy device vanishes" behavior as before
    // (Fase SC-7.3/7.4), just no longer assuming a swap always empties it.
    if (remainingCount == 0 && m_devices.at(fromDeviceRow).vendorProduct == QStringLiteral("Offline / Imported Device")) {
        beginRemoveRows(QModelIndex(), fromDeviceRow, fromDeviceRow);
        m_devices.removeAt(fromDeviceRow);
        endRemoveRows();
    }

    updateAllInputBindingLabels();
    return true;
}

void ProfileEditorViewModel::clearDeviceBindings(const QString &devicePath)
{
    const int deviceRow = indexOfSystemPath(devicePath);
    if (deviceRow < 0) {
        return;
    }

    pushUndoSnapshot(tr("Bindings cleared"));

    // Clears every route associated with this device, in every mode - a
    // plain snapshot copy (not a reference) since addAxisRoute()/
    // addButtonRoute() below mutate the same routing table allRoutes() just
    // read from.
    const auto routes = m_router.allRoutes();
    for (const auto &route : routes) {
        if (route.systemPath == devicePath) {
            if (route.isAxis) {
                m_router.addAxisRoute(devicePath, route.index, nullptr, route.mode);
            } else {
                m_router.addButtonRoute(devicePath, route.index, nullptr, route.mode);
            }
        }
    }

    // If this was a synthesized orphan-device placeholder (Fase SC-7.3), it
    // has no bindings left to show now - remove it from the visual list.
    if (m_devices.at(deviceRow).vendorProduct == QStringLiteral("Offline / Imported Device")) {
        beginRemoveRows(QModelIndex(), deviceRow, deviceRow);
        m_devices.removeAt(deviceRow);
        endRemoveRows();
    }

    updateAllInputBindingLabels();
}

void ProfileEditorViewModel::copyAction(const QString &devicePath, const QString &inputName, const QString &mode,
                                          const QString &inputKind)
{
    m_clipboardJson = getActionDataJson(devicePath, inputName, mode);
    m_clipboardKind = inputKind;

    // See clipboardWarning's own Q_PROPERTY docs and
    // bindingReferencesExternalInput()'s own docs on exactly what this
    // catches (ConditionHandler/MergeAxisHandler, anywhere in the copied
    // action's nesting) and why it matters.
    m_clipboardWarning.clear();
    if (!m_clipboardJson.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(m_clipboardJson.toUtf8());
        if (doc.isObject() && bindingReferencesExternalInput(doc.object())) {
            m_clipboardWarning = tr("Pasting will share the SAME modifier button / other axis as the original - "
                                     "it won't get its own independent copy.");
        }
    }

    emit clipboardChanged();
}

bool ProfileEditorViewModel::hasCopiedAction(const QString &inputKind) const
{
    return !m_clipboardJson.isEmpty() && m_clipboardKind == inputKind;
}

bool ProfileEditorViewModel::hasCopiedAxis() const
{
    return hasCopiedAction(QStringLiteral("axis"));
}

bool ProfileEditorViewModel::hasCopiedButton() const
{
    return hasCopiedAction(QStringLiteral("button"));
}

void ProfileEditorViewModel::pasteAction(const QString &devicePath, const QString &inputName, const QString &mode,
                                           const QString &inputKind)
{
    if (!hasCopiedAction(inputKind)) {
        return;
    }
    // bindAction() re-parses this JSON and rebinds it to whatever routing
    // index inputName resolves to on devicePath - not the (device, index)
    // it was originally copied from.
    bindAction(devicePath, inputName, mode, m_clipboardJson);
}

bool ProfileEditorViewModel::exportCheatsheetPdf(const QString &filePath)
{
    const QString localPath = QUrl(filePath).isLocalFile() ? QUrl(filePath).toLocalFile() : filePath;
    const QString profileName =
        m_currentProfileData.value(QStringLiteral("profileName")).toString(QStringLiteral("Untitled"));
    return m_profileManager.exportCheatsheetPdf(localPath, m_router, profileName);
}

QString ProfileEditorViewModel::resolveSCInput(const QString &scInput) const
{
    const QString profileName =
        m_currentProfileData.value(QStringLiteral("profileName")).toString(QStringLiteral("Untitled"));
    const QJsonObject liveProfile = m_profileManager.serializeProfile(m_router, profileName);
    return SCIntegrationManager::instance().resolvePhysicalSource(scInput, liveProfile);
}

bool ProfileEditorViewModel::listenForInputs() const
{
    return m_listenForInputs;
}

void ProfileEditorViewModel::setListenForInputs(bool listen)
{
    if (m_listenForInputs == listen) {
        return;
    }
    m_listenForInputs = listen;
    if (!m_listenForInputs) {
        // Fase 20.12: drop stale baselines on the way out, so turning
        // listening back on later starts fresh instead of comparing a new
        // session's first reading against a value captured last time.
        m_axisBaselines.clear();
    }
    emit listenForInputsChanged();
}

QStringList ProfileEditorViewModel::modes() const
{
    return m_modes;
}

QString ProfileEditorViewModel::currentMode() const
{
    return m_currentMode;
}

void ProfileEditorViewModel::setCurrentMode(const QString &mode)
{
    if (m_currentMode == mode || !m_modes.contains(mode)) {
        return;
    }
    m_currentMode = mode;
    emit currentModeChanged();
    // Fase 20.26: findBindingLabel() now filters by m_currentMode, but that
    // alone doesn't repaint anything - without this, switching modes left
    // every InputRow showing whatever hasBinding/bindingLabel it was last
    // recomputed with (e.g. from the previous mode), not this mode's own
    // bindings, until some unrelated bind/unbind happened to refresh it.
    updateAllInputBindingLabels();
}

QString ProfileEditorViewModel::currentProfileName() const
{
    return m_currentProfileData.value(QStringLiteral("profileName")).toString(QStringLiteral("Untitled"));
}

void ProfileEditorViewModel::addMode(const QString &name)
{
    if (name.isEmpty() || m_modes.contains(name)) {
        return;
    }
    m_modes.append(name);
    // Sprint 5 (Familias de Modos): every new mode defaults to inheriting
    // straight from Global - without this, a freshly created mode would be
    // its own root (see EventRouter::setModeParent()'s docs) and never fall
    // back to Global's bindings at all, a silent behavior change from every
    // mode created before this feature existed. ModeManagerPopup.qml lets
    // the user re-point it at a different parent afterward.
    m_router.setModeParent(name, EventRouter::kGlobalMode);
    emit modesChanged();
    setCurrentMode(name);
}

void ProfileEditorViewModel::removeMode(const QString &name)
{
    if (name == EventRouter::kGlobalMode || !m_modes.contains(name)) {
        return;
    }
    m_modes.removeAll(name);
    emit modesChanged();

    if (m_currentMode == name) {
        m_currentMode = EventRouter::kGlobalMode;
        emit currentModeChanged();
    }
}

bool ProfileEditorViewModel::renameMode(const QString &oldName, const QString &newName)
{
    const QString trimmedNewName = newName.trimmed();
    if (oldName == EventRouter::kGlobalMode || !m_modes.contains(oldName)) {
        return false;
    }
    if (trimmedNewName.isEmpty() || trimmedNewName == oldName) {
        return false;
    }

    // trimmedNewName may already be a known mode name - most notably Global
    // itself, always present in m_modes (see the constructor's own
    // m_modes.append(kGlobalMode)) even when it has no bindings of its own.
    // That's exactly the shape a legacy import leaves behind: the imported
    // profile's own root mode (never literally named "Global") comes in as
    // a brand new mode, while Global sits empty with no bindings and no way
    // to fold the import back into it. ProfileManager::renameMode() below
    // is the actual authority on whether a merge into an existing mode name
    // is safe - it refuses if trimmedNewName already has bindings of its
    // own (avoiding a silent route collision), not merely if the name is
    // already known - so an empty existing mode (Global or otherwise) is a
    // legitimate merge target, not just a plain "swap this mode's name" one.
    const bool mergingIntoExistingMode = m_modes.contains(trimmedNewName);

    // Undo (see pushUndoSnapshot()'s own docs): only a real merge is
    // destructive here - oldName's bindings get folded into an existing
    // mode's, which isn't reversible by just renaming back if the target
    // mode already had bindings of its own. A plain rename-to-a-new-name is
    // trivially reversible (rename it back), so it doesn't need a snapshot.
    if (mergingIntoExistingMode) {
        pushUndoSnapshot(tr("Merged mode \"%1\" into \"%2\"").arg(oldName, trimmedNewName));
    }

    if (!m_profileManager.renameMode(oldName, trimmedNewName, m_router)) {
        return false;
    }

    const int modeIndex = m_modes.indexOf(oldName);
    if (modeIndex >= 0) {
        if (mergingIntoExistingMode) {
            // trimmedNewName already has its own entry in m_modes (e.g.
            // Global's, always present) - oldName's bindings now live under
            // that existing entry, so drop oldName's rather than also
            // writing trimmedNewName into its slot, which would otherwise
            // leave two duplicate entries for the same mode name.
            m_modes.removeAt(modeIndex);
        } else {
            m_modes[modeIndex] = trimmedNewName;
        }
    }
    emit modesChanged();

    if (m_currentMode == oldName) {
        m_currentMode = trimmedNewName;
        emit currentModeChanged();
    }

    // Fase (mode rename): every InputRow pill for a ModeSwitch/
    // TemporaryModeSwitch binding literally embeds the mode's name as text
    // (e.g. "Shift -> Combat") - renameMode() above already re-tagged the
    // underlying JSON, but nothing repaints those labels on its own (same
    // reasoning as setCurrentMode()'s own call to this).
    updateAllInputBindingLabels();
    return true;
}

void ProfileEditorViewModel::setModeParent(const QString &mode, const QString &parent)
{
    m_router.setModeParent(mode, parent);
}

QString ProfileEditorViewModel::getModeParent(const QString &mode) const
{
    return m_router.getModeParent(mode);
}

QVariantMap ProfileEditorViewModel::vjoyOccupancy(int targetOutputId) const
{
    const ProfileManager::VJoyOccupancy occupancy = m_profileManager.vjoyOccupancy(targetOutputId, m_router);

    QVariantList axes;
    axes.reserve(occupancy.axes.size());
    for (bool occupied : occupancy.axes) {
        axes.append(occupied);
    }

    QVariantList buttons;
    buttons.reserve(occupancy.buttons.size());
    for (bool occupied : occupancy.buttons) {
        buttons.append(occupied);
    }

    QVariantMap result;
    result[QStringLiteral("axes")] = axes;
    result[QStringLiteral("buttons")] = buttons;
    return result;
}

void ProfileEditorViewModel::onDeviceAdded(const DeviceInfo &device)
{
    const int existing = indexOfSystemPath(device.systemPath);
    if (existing >= 0) {
        m_devices[existing] = makeDeviceEntry(device, m_router);
        const QModelIndex changedIndex = index(existing);
        emit dataChanged(changedIndex, changedIndex);
        return;
    }

    DeviceEntry entry = makeDeviceEntry(device, m_router);
    const int insertAt = deviceInsertionIndex(entry.vendorProduct, entry.systemPath);
    beginInsertRows(QModelIndex(), insertAt, insertAt);
    m_devices.insert(insertAt, std::move(entry));
    endInsertRows();

    // Swap To suggestion (see DeviceEntry::orphanedDeviceName's own docs):
    // a newly-connected device whose name matches an existing offline
    // placeholder's last-known name is very likely the exact same physical
    // device reconnecting under a different systemPath (a genuine VID/PID
    // change, e.g. from a calibration tool - see the AeroMax-R case this
    // was built for) rather than a coincidence, since two different real
    // controllers reporting the identical product string is rare. Only the
    // first match is offered - if more than one offline row happens to
    // share this name, the rest just stay available for a manual Swap To.
    if (!device.deviceName.isEmpty()) {
        for (const DeviceEntry &candidate : std::as_const(m_devices)) {
            if (candidate.vendorProduct == QStringLiteral("Offline / Imported Device") &&
                !candidate.orphanedDeviceName.isEmpty() &&
                candidate.orphanedDeviceName.compare(device.deviceName, Qt::CaseInsensitive) == 0) {
                emit offlineDeviceMatchFound(candidate.systemPath, device.systemPath, device.deviceName);
                break;
            }
        }
    }
}

void ProfileEditorViewModel::onDeviceRemoved(const QString &systemPath)
{
    const int existing = indexOfSystemPath(systemPath);
    if (existing < 0) {
        return;
    }

    beginRemoveRows(QModelIndex(), existing, existing);
    m_devices.removeAt(existing);
    endRemoveRows();
}

void ProfileEditorViewModel::onAxisMovedForDetection(const QString &systemPath, int axisIndex, int value)
{
    if (!m_listenForInputs) {
        return;
    }

    // Fase 20.12: delta-based detection, not a fixed center (32767) - an
    // axis that physically rests somewhere else entirely (e.g. a Throttle
    // resting at raw 0) sat permanently past-threshold away from that
    // center, so its own electrical noise at rest was reported as constant
    // motion. Comparing against this axis's own last-seen value instead
    // means only an actual move past the threshold counts, no matter where
    // the axis happens to rest.
    if (!m_axisBaselines[systemPath].contains(axisIndex)) {
        m_axisBaselines[systemPath][axisIndex] = value;
        return; // First reading for this axis - just record it as the baseline.
    }

    const int deviceRow = indexOfSystemPath(systemPath);
    if (deviceRow < 0) {
        return;
    }
    const DeviceEntry &entry = m_devices.at(deviceRow);

    // Fase (bugfix): Quick Bind used to react to vJoy's own virtual devices
    // too - DeviceManager enumerates them via RawInput exactly like any
    // physical joystick, so whenever a game (or Nexus's own output writes)
    // moved a vJoy axis, Quick Bind would jump the Profiles screen over to
    // that vJoy tab and highlight it as if the user had just wiggled a real
    // stick. vJoy devices are outputs, never something to bind FROM - same
    // "vjoy" substring check DeviceTesterViewModel already uses to label
    // them.
    if (entry.name.contains(QStringLiteral("vjoy"), Qt::CaseInsensitive)) {
        return;
    }

    // Fase 16.7: axisIndex is true-analog-axes only now - a POV hat comes
    // through onButtonPressedForDetection() instead (DeviceManager reports
    // it as 4 synthetic buttons, not a fake axis).
    if (axisIndex < 0 || axisIndex >= entry.numAxes) {
        return;
    }

    // Fase 20.13: the detection threshold is ~10% of this axis's own HID
    // logical range, not a fixed raw-count constant - a fixed threshold
    // (previously 6000) assumed every device reports at 16-bit resolution
    // (0-65535); a lower-resolution stick (e.g. 12-bit, topping out at
    // 4095) can never produce a delta anywhere near that, so every one of
    // its axes went permanently undetectable. Falls back to a 16-bit-sized
    // range if this device didn't report logical min/max for this axis.
    int axisRange = 65535;
    if (axisIndex < entry.axisLogicalMax.size() && axisIndex < entry.axisLogicalMin.size()) {
        axisRange = entry.axisLogicalMax[axisIndex] - entry.axisLogicalMin[axisIndex];
    }
    const int dynamicThreshold = std::max(10, axisRange / 10);

    const int baseline = m_axisBaselines[systemPath][axisIndex];
    if (std::abs(value - baseline) < dynamicThreshold) {
        return;
    }
    // Moves the baseline forward so a strong, continuous motion keeps
    // reporting while it's happening, but stops the instant the axis settles.
    m_axisBaselines[systemPath][axisIndex] = value;

    const QString inputName = entry.inputs.at(axisIndex).toMap().value(QStringLiteral("name")).toString();
    emit hardwareInputDetected(systemPath, inputName);
}

void ProfileEditorViewModel::onButtonPressedForDetection(const QString &systemPath, int buttonIndex, bool pressed)
{
    if (!m_listenForInputs || !pressed) {
        return;
    }

    const int deviceRow = indexOfSystemPath(systemPath);
    if (deviceRow < 0) {
        return;
    }
    const DeviceEntry &entry = m_devices.at(deviceRow);

    // Fase (bugfix): see onAxisMovedForDetection()'s own docs - Quick Bind
    // must never react to vJoy's own virtual devices.
    if (entry.name.contains(QStringLiteral("vjoy"), Qt::CaseInsensitive)) {
        return;
    }

    if (buttonIndex < 0 || buttonIndex >= entry.numButtons) {
        return;
    }

    // Fase 16.7: shares buttonDisplayName() with makeDeviceEntry() so a POV
    // hat's synthetic button reports the exact same "POV H <Direction>"
    // name the bindable input list itself uses - otherwise
    // DeviceCard::highlightInput()'s name match would silently fail to find
    // it, and Quick Bind would never highlight a hat direction.
    emit hardwareInputDetected(systemPath, buttonDisplayName(buttonIndex, entry.numButtons, entry.numHats));
}

void ProfileEditorViewModel::updateInputBindingLabel(int deviceRow, const QString &inputName, const QString &label,
                                                       const QString &note)
{
    if (deviceRow < 0 || deviceRow >= m_devices.size()) {
        return;
    }

    // Fase 20.5: back to a reference, not the Fase 20.4 copy - reassigning
    // m_devices[deviceRow].inputs wholesale made Qt Quick's Repeater tear
    // down and rebuild every delegate in the row (not just refresh the one
    // that changed), which broke the geometry scrollToItem() depends on.
    // dataChanged() is no longer emitted here either - bindingUpdated()
    // below lets QML patch just the one delegate in place instead.
    QVariantList &inputs = m_devices[deviceRow].inputs;
    for (int i = 0; i < inputs.size(); ++i) {
        QVariantMap input = inputs.at(i).toMap();
        if (input.value(QStringLiteral("name")).toString() != inputName) {
            continue;
        }
        // Fase 20.2: an empty label means "unbind this input" (see
        // unbindAction()) - hasBinding must go false along with it, or
        // InputRow.qml would keep showing its (now blank) bindingLabel
        // instead of falling back to its "Unbound" placeholder text.
        input[QStringLiteral("hasBinding")] = !label.isEmpty();
        input[QStringLiteral("bindingLabel")] = label;
        input[QStringLiteral("actionNote")] = note;
        inputs[i] = input;

        emit bindingUpdated(m_devices.at(deviceRow).systemPath, inputName, !label.isEmpty(), label, note);
        return;
    }
}

void ProfileEditorViewModel::updateAllInputBindingLabels()
{
    const std::vector<EventRouter::RouteDescriptor> routes = m_router.allRoutes();

    // Fase 20.5: back to a reference, and bindingUpdated() per changed input
    // instead of one dataChanged() per row - see updateInputBindingLabel()
    // for why a wholesale row reassignment broke QML's auto-focus geometry.
    for (int deviceRow = 0; deviceRow < m_devices.size(); ++deviceRow) {
        QVariantList &inputs = m_devices[deviceRow].inputs;
        const QString &systemPath = m_devices.at(deviceRow).systemPath;

        // Fase SC-7.8: makeDeviceEntry() only ever runs once, the moment a
        // physical device is first detected over USB (Fase SC-7.6) - at that
        // point no profile has been loaded yet, so the router is empty and
        // any out-of-range "ghost" axis/button a *later* Load/Swap/Clear
        // brings into range never gets a row, since nothing else ever
        // revisits this device's input list afterward. Detecting and
        // append()-ing any missing ghost rows here (rather than rebuilding
        // the whole list) means every path that already calls this function
        // - loadProfileFromPath(), swapDevices(), clearDeviceBindings() -
        // picks them up for free, and append (not a wholesale reset) keeps
        // this row's existing QML delegates/auto-focus state intact.
        int currentMaxAxis = m_devices.at(deviceRow).numAxes - 1;
        int currentMaxButton = m_devices.at(deviceRow).numButtons - 1;

        // Account for ghost rows already appended earlier this session.
        for (const QVariant &v : inputs) {
            const QVariantMap map = v.toMap();
            const int idx = map.value(QStringLiteral("inputIndex")).toInt();
            if (map.value(QStringLiteral("kind")).toString() == QStringLiteral("axis") && idx > currentMaxAxis) {
                currentMaxAxis = idx;
            } else if (map.value(QStringLiteral("kind")).toString() == QStringLiteral("button") && idx > currentMaxButton) {
                currentMaxButton = idx;
            }
        }

        int neededMaxAxis = currentMaxAxis;
        int neededMaxButton = currentMaxButton;

        for (const auto &route : routes) {
            if (route.systemPath == systemPath) {
                if (route.isAxis && route.index > neededMaxAxis) {
                    neededMaxAxis = route.index;
                } else if (!route.isAxis && route.index > neededMaxButton) {
                    neededMaxButton = route.index;
                }
            }
        }

        bool appended = false;
        for (int i = currentMaxAxis + 1; i <= neededMaxAxis; ++i) {
            QString axisName;
            switch (i) {
                case 0: axisName = QStringLiteral("Axis X"); break;
                case 1: axisName = QStringLiteral("Axis Y"); break;
                case 2: axisName = QStringLiteral("Axis Z (Throttle/Rudder)"); break;
                case 3: axisName = QStringLiteral("Axis Rx (Pitch/Roll)"); break;
                case 4: axisName = QStringLiteral("Axis Ry"); break;
                case 5: axisName = QStringLiteral("Axis Rz"); break;
                default: axisName = QStringLiteral("Slider %1").arg(i - 5); break;
            }
            if (i >= m_devices.at(deviceRow).numAxes) {
                axisName.append(QStringLiteral(" (Ghost/Hidden)"));
            }

            QString bindingLabel;
            QString actionNote;
            const bool hasBinding = findBindingLabel(routes, systemPath, true, i, m_currentMode, bindingLabel, actionNote);
            inputs.append(makeInputEntry(axisName, QStringLiteral("axis"), i, hasBinding, bindingLabel, actionNote));
            appended = true;
        }

        for (int i = currentMaxButton + 1; i <= neededMaxButton; ++i) {
            QString bindingLabel;
            QString actionNote;
            const bool hasBinding = findBindingLabel(routes, systemPath, false, i, m_currentMode, bindingLabel, actionNote);
            QString name;
            if (i < m_devices.at(deviceRow).numButtons) {
                name = buttonDisplayName(i, m_devices.at(deviceRow).numButtons, m_devices.at(deviceRow).numHats);
            } else {
                name = QStringLiteral("Button %1 (Ghost/Hidden)").arg(i + 1);
            }
            inputs.append(makeInputEntry(name, QStringLiteral("button"), i, hasBinding, bindingLabel, actionNote));
            appended = true;
        }

        if (appended) {
            const QModelIndex rowIdx = index(deviceRow, 0);
            emit dataChanged(rowIdx, rowIdx, {InputsRole});
        }

        for (int i = 0; i < inputs.size(); ++i) {
            QVariantMap input = inputs.at(i).toMap();
            const bool isAxis = input.value(QStringLiteral("kind")).toString() == QStringLiteral("axis");
            const int inputIndex = input.value(QStringLiteral("inputIndex")).toInt();

            QString label;
            QString note;
            const bool hasBinding = findBindingLabel(routes, systemPath, isAxis, inputIndex, m_currentMode, label, note);
            if (input.value(QStringLiteral("hasBinding")).toBool() == hasBinding &&
                input.value(QStringLiteral("bindingLabel")).toString() == label &&
                input.value(QStringLiteral("actionNote")).toString() == note) {
                continue;
            }

            input[QStringLiteral("hasBinding")] = hasBinding;
            input[QStringLiteral("bindingLabel")] = label;
            input[QStringLiteral("actionNote")] = note;
            inputs[i] = input;

            emit bindingUpdated(systemPath, input.value(QStringLiteral("name")).toString(), hasBinding, label, note);
        }
    }
}

QVariantMap ProfileEditorViewModel::makeInputEntry(const QString &name, const QString &kind, int inputIndex,
                                                     bool hasBinding, const QString &bindingLabel, const QString &note)
{
    QVariantMap entry;
    entry[QStringLiteral("name")] = name;
    entry[QStringLiteral("kind")] = kind;
    entry[QStringLiteral("inputIndex")] = inputIndex;
    entry[QStringLiteral("hasBinding")] = hasBinding;
    entry[QStringLiteral("bindingLabel")] = bindingLabel;
    entry[QStringLiteral("actionNote")] = note;
    return entry;
}

ProfileEditorViewModel::DeviceEntry ProfileEditorViewModel::makeDeviceEntry(const DeviceInfo &device,
                                                                              EventRouter &router)
{
    DeviceEntry entry;
    entry.name = device.deviceName;
    entry.vendorProduct = QStringLiteral("VID:%1 PID:%2").arg(device.vendorId, device.productId);
    entry.systemPath = device.systemPath;
    entry.isMock = false;
    entry.numAxes = device.numAxes;
    entry.numHats = device.numHats;
    entry.numButtons = device.numButtons;

    // Fase 20.13: copied so onAxisMovedForDetection() can size its Quick
    // Bind detection threshold to this device's actual axis resolution.
    entry.axisLogicalMin = device.axisLogicalMin;
    entry.axisLogicalMax = device.axisLogicalMax;

    // Fase 19 bugfix: a route already registered in router - whether from a
    // profile loaded before this ViewModel ever built this device's row, or
    // from bindAction() earlier this session - previously left hasBinding/
    // bindingLabel at their unbound default below, since this function had
    // no way to see the routing table at all. allRoutes() gives us that
    // snapshot; matching (systemPath, isAxis, index) against it up front
    // means every input's real binding state (if any) is reflected the
    // first time this row is ever built, not just after the user re-touches it.
    const std::vector<EventRouter::RouteDescriptor> routes = router.allRoutes();
    auto findBinding = [&routes, &device, this](bool isAxis, int index, QString &outLabel, QString &outNote) -> bool {
        return findBindingLabel(routes, device.systemPath, isAxis, index, m_currentMode, outLabel, outNote);
    };

    // Real devices carry no per-input binding info yet (see class docs) -
    // every physical input is listed, always unbound, until bindAction()
    // (or a loaded profile that happens to target it) actually binds it.
    // Hat inputs share axisIndex space with true axes (see DeviceEntry's
    // own docs), so their inputIndex continues past numAxes rather than
    // restarting at 0.
    QVariantList inputs;
    for (int i = 0; i < device.numAxes; ++i) {
        QString axisName;
        switch (i) {
            case 0: axisName = QStringLiteral("Axis X"); break;
            case 1: axisName = QStringLiteral("Axis Y"); break;
            case 2: axisName = QStringLiteral("Axis Z (Throttle/Rudder)"); break;
            case 3: axisName = QStringLiteral("Axis Rx (Pitch/Roll)"); break;
            case 4: axisName = QStringLiteral("Axis Ry"); break;
            case 5: axisName = QStringLiteral("Axis Rz"); break;
            default: axisName = QStringLiteral("Slider %1").arg(i - 5); break;
        }
        QString bindingLabel;
        QString actionNote;
        const bool hasBinding = findBinding(true, i, bindingLabel, actionNote);
        inputs.append(makeInputEntry(axisName, QStringLiteral("axis"), i, hasBinding, bindingLabel, actionNote));
    }
    // Fase 16.7: POV hats no longer get their own "axis"-kind entries here -
    // DeviceManager now reports each hat as 4 synthetic buttons
    // (Up/Right/Down/Left) appended after the device's real physical
    // buttons within device.numButtons, so they're covered by the button
    // loop below - buttonDisplayName() is what tells a real "Button N" apart
    // from a synthetic "POV H <Direction>" one.
    for (int i = 0; i < device.numButtons; ++i) {
        QString bindingLabel;
        QString actionNote;
        const bool hasBinding = findBinding(false, i, bindingLabel, actionNote);
        inputs.append(makeInputEntry(buttonDisplayName(i, device.numButtons, device.numHats),
                                      QStringLiteral("button"), i, hasBinding, bindingLabel, actionNote));
    }
    entry.inputs = inputs;

    return entry;
}

int ProfileEditorViewModel::deviceInsertionIndex(const QString &vendorProduct, const QString &systemPath) const
{
    const QString key = deviceOrderKey(vendorProduct, systemPath);
    const int savedRank = m_deviceTabOrder.indexOf(key);
    if (savedRank < 0) {
        return m_devices.size(); // Never explicitly ordered - append, same as before tab reordering existed.
    }

    // First already-present device whose OWN saved rank comes after ours -
    // insert right before it. A device with no saved rank of its own isn't a
    // useful anchor (its position doesn't reflect a user choice) and is
    // skipped rather than treated as "comes after everything".
    for (int i = 0; i < m_devices.size(); ++i) {
        const DeviceEntry &existing = m_devices.at(i);
        const int otherRank = m_deviceTabOrder.indexOf(deviceOrderKey(existing.vendorProduct, existing.systemPath));
        if (otherRank >= 0 && otherRank > savedRank) {
            return i;
        }
    }
    return m_devices.size();
}

void ProfileEditorViewModel::persistDeviceTabOrder()
{
    QStringList order;
    order.reserve(m_devices.size());
    for (const DeviceEntry &entry : m_devices) {
        order.append(deviceOrderKey(entry.vendorProduct, entry.systemPath));
    }
    // Kept in sync in-memory too (not just QSettings) so a device added
    // later THIS session - after a drag already happened - still inserts
    // relative to the freshly-dragged order, not the stale one loaded at
    // startup.
    m_deviceTabOrder = order;
    QSettings().setValue(QStringLiteral("DeviceTabOrder"), order.join(QLatin1Char(',')));
}

void ProfileEditorViewModel::moveDeviceTab(int fromRow, int toRow)
{
    if (fromRow == toRow || fromRow < 0 || fromRow >= m_devices.size() || toRow < 0 || toRow >= m_devices.size()) {
        return;
    }

    // Canonical Qt idiom for QAbstractItemModel::beginMoveRows() +
    // QList::move(): the destination argument is "insert before this row,
    // indexed against the ORIGINAL (pre-move) array" - for a forward move
    // that has to be toRow+1 (toRow itself, pre-move, still holds the row
    // this one needs to land AFTER), a backward move uses toRow directly.
    beginMoveRows(QModelIndex(), fromRow, fromRow, QModelIndex(), toRow > fromRow ? toRow + 1 : toRow);
    m_devices.move(fromRow, toRow);
    endMoveRows();

    persistDeviceTabOrder();
}

int ProfileEditorViewModel::indexOfSystemPath(const QString &systemPath) const
{
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices.at(i).systemPath == systemPath) {
            return i;
        }
    }
    return -1;
}
