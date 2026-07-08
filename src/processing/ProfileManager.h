#pragma once

#include <memory>
#include <vector>

#include <QMap>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>

class EventRouter;
class QJsonObject;
class IActionHandler;
class IKeyboardBackend;
class IVirtualOutputDevice;

/**
 * @brief Loads/saves EventRouter binding profiles from/to JSON files.
 *
 * Stateless beyond the file I/O + JSON <-> handler translation; the actual
 * routing state lives in whichever EventRouter is passed to loadProfile().
 *
 * Expected JSON schema:
 * @code
 * {
 *   "profileName": "Default",
 *   "calibrations": [
 *     {
 *       "systemPath": "\\\\?\\hid#...",
 *       "axisIndex": 0,
 *       "calibratedMin": 1000,
 *       "calibratedMax": 4000
 *     }
 *   ],
 *   "bindings": [
 *     {
 *       "sourceDevice": "",           // "" = wildcard, matches any device
 *       "sourceAxis": 0,
 *       "actionType": "CurveHandler",
 *       "mode": "Global",             // optional; defaults to "Global" if absent
 *       "targetDeviceType": "vjoy",   // optional; defaults to "vjoy". "vigem" targets a ViGEmBus
 *                                     // virtual Xbox 360 controller instead - see resolveTargetDevice()'s
 *                                     // own docs. Every "targetOutputId" field below is interpreted
 *                                     // against whichever type is named here: vJoy ids are [1, 16],
 *                                     // ViGEm ids are [1, 4] (one of up to 4 ad-hoc X360 pads).
 *       "targetOutputId": 1,          // vJoy device id, [1, 16]
 *       "targetAxis": 0,
 *       "parameters": {
 *         "deadzone": 0.05,
 *         "sensitivity": 1.0,
 *         "inputMin": 0,
 *         "inputMax": 65535,
 *         "outputMin": 0,
 *         "outputMax": 32767,
 *         "invert": false        // optional; defaults to false. Physically flips
 *                                 // output polarity - distinct from "inverting" a
 *                                 // curve/points in the graphical editor.
 *       }
 *     },
 *     {
 *       "sourceDevice": "",
 *       "sourceButton": 5,
 *       "actionType": "ModeSwitch",
 *       "mode": "Global",             // which mode this button must be active in to fire; "Global" = always
 *       "parameters": {
 *         "targetMode": "Combat"      // mode to switch *to* when this button is pressed
 *       }
 *     },
 *     {
 *       "sourceDevice": "",
 *       "sourceButton": 6,
 *       "actionType": "ButtonRemapHandler",
 *       "mode": "Global",
 *       "targetOutputId": 1,          // vJoy device id, [1, 16]
 *       "targetButton": 2
 *     },
 *     {
 *       // POV hat remap (Fase 19): fires a discrete direction on a vJoy
 *       // POV hat while sourceButton is held - typically bound to one of a
 *       // physical hat's own 4 synthetic Up/Right/Down/Left buttons (see
 *       // ProfileEditorViewModel::buttonDisplayName()).
 *       "sourceDevice": "",
 *       "sourceButton": 15,
 *       "actionType": "HatRemapHandler",
 *       "mode": "Global",
 *       "targetOutputId": 1,          // vJoy device id, [1, 16]
 *       "targetHat": 0,               // POV hat index, [0, 4)
 *       "targetDirection": 0          // 0=Up, 1=Right, 2=Down, 3=Left
 *     },
 *     {
 *       "sourceDevice": "",
 *       "sourceButton": 7,
 *       "actionType": "KeyboardHandler",
 *       "mode": "Global",
 *       "parameters": {
 *         "scanCode": 30              // hardware scan code (PS/2 Set 1), not a virtual-key code
 *       }
 *     },
 *     {
 *       // Timed button-press sequence (Fase 10.8's Action Picker "Macro" option
 *       // authors the two-step press/wait/release shorthand below; "steps" also
 *       // accepts an arbitrary hand-authored sequence of the same three types).
 *       "sourceDevice": "",
 *       "sourceButton": 13,
 *       "actionType": "MacroHandler",
 *       "mode": "Global",
 *       "targetOutputId": 1,          // vJoy device id, [1, 16]
 *       "parameters": {
 *         "steps": [
 *           { "type": "PressButton", "buttonIndex": 4 },
 *           { "type": "Wait", "waitMs": 100 },
 *           { "type": "ReleaseButton", "buttonIndex": 4 }
 *         ]
 *       }
 *     },
 *     {
 *       "sourceDevice": "",
 *       "sourceButton": 8,
 *       "actionType": "TemporaryModeSwitch",
 *       "mode": "Global",
 *       "parameters": {
 *         "targetMode": "Combat"      // active only while sourceButton is held; restores the prior mode on release
 *       }
 *     },
 *     {
 *       "sourceDevice": "",
 *       "sourceButton": 9,
 *       "actionType": "TrimHandler",
 *       "mode": "Global",
 *       "targetOutputId": 1,          // vJoy device id, [1, 16]
 *       "targetAxis": 5,
 *       "parameters": {
 *         "stepValue": 128,           // added to the running value per press; negative to trim down
 *         "initialValue": 16383       // optional; defaults to vJoy's own axis center
 *       }
 *     },
 *     {
 *       "sourceDevice": "",
 *       "sourceAxis": 0,
 *       "actionType": "MouseHandler",
 *       "mode": "Global",
 *       "parameters": {
 *         "mouseAction": "MoveX"      // "MoveX"/"MoveY" (needs "sourceAxis") or "LeftClick"/"RightClick"/"MiddleClick" (needs "sourceButton")
 *       }
 *     },
 *     {
 *       "sourceDevice": "",
 *       "sourceAxis": 1,
 *       "actionType": "ConditionHandler",
 *       "mode": "Global",
 *       "parameters": {
 *         "modSystemPath": "",        // "" = wildcard, any device's modButtonIndex
 *         "modButtonIndex": 10,
 *         "requirePressed": true      // wrappedAction only fires while this matches the modifier's physical state
 *       },
 *       "wrappedAction": {            // any other binding-shaped object (its own "sourceDevice"/"sourceAxis"/
 *                                     // "sourceButton"/"mode" are ignored - only used to instantiate the handler)
 *         "actionType": "CurveHandler",
 *         "targetOutputId": 1,
 *         "targetAxis": 1
 *       }
 *     },
 *     {
 *       "sourceDevice": "",
 *       "sourceButton": 11,
 *       "actionType": "ToggleHandler",
 *       "mode": "Global",
 *       "wrappedAction": {
 *         "actionType": "ButtonRemapHandler",
 *         "targetOutputId": 1,
 *         "targetButton": 4
 *       }
 *     },
 *     {
 *       "sourceDevice": "",
 *       "sourceButton": 12,
 *       "actionType": "TempoHandler",
 *       "mode": "Global",
 *       "parameters": {
 *         "longPressMs": 500,         // optional; default 500
 *         "doubleTapMs": 250          // optional; default 250
 *       },
 *       // Each list fires every action in cascade when its gesture wins; all 3 optional.
 *       // A single legacy "shortAction"/"longAction"/"doubleAction" object (pre-cascade
 *       // schema) is still accepted in place of its *Actions array, as a one-action list.
 *       "shortActions": [ { "actionType": "ButtonRemapHandler", "targetOutputId": 1, "targetButton": 5 } ],
 *       "longActions":  [ { "actionType": "ModeSwitch", "parameters": { "targetMode": "Combat" } } ],
 *       "doubleActions": [ { "actionType": "KeyboardHandler", "parameters": { "scanCode": 30 } } ]
 *     },
 *     {
 *       "sourceDevice": "",
 *       "sourceAxis": 6,
 *       "actionType": "AxisToButtonHandler",
 *       "mode": "Global",
 *       "parameters": {
 *         "threshold": 32000,
 *         "invert": false             // false: fires *above* threshold; true: fires *below* it
 *       },
 *       "wrappedAction": { "actionType": "ModeSwitch", "parameters": { "targetMode": "Combat" } }
 *     },
 *     {
 *       // Differential merge (pedals -> rudder): author TWO mirrored bindings, one per
 *       // physical axis, each naming the *other* axis as "otherSystemPath"/"otherAxisIndex" -
 *       // moving either pedal re-evaluates the same formula and re-stages the same vJoy axis.
 *       "sourceDevice": "leftPedal",
 *       "sourceAxis": 0,
 *       "actionType": "MergeAxisHandler",
 *       "mode": "Global",
 *       "targetOutputId": 1,
 *       "targetAxis": 3,
 *       "parameters": {
 *         "otherSystemPath": "rightPedal",
 *         "otherAxisIndex": 0,
 *         "isSubtraction": true       // true: centered difference; false: plain average
 *       }
 *     },
 *     {
 *       // Splits one physical axis' travel in half across two vJoy axes
 *       // (Fase 20.39, splitMode added Fase 20.41/20.42): the "lower" and
 *       // "upper" targets each span their own full [0, 65535] output range
 *       // over one half of the axis' travel - e.g. one throttle axis
 *       // driving two separate vJoy sliders, or a centered joystick/rudder
 *       // axis driving two independent pedals/triggers either side of rest.
 *       // splitMode picks which rest position that split is centered
 *       // around: 0 (CenterToEdges, default) assumes the axis rests at
 *       // center (32767, most joysticks/rudders) with both targets reading
 *       // 0 at rest; 1 (Sequential) assumes it rests at 0 (most throttles/
 *       // sliders), ramping "lower" 0->max first, then "upper" 0->max.
 *       // Unlike MergeAxisHandler above, this is a single self-contained
 *       // binding (no top-level targetOutputId/targetAxis of its own -
 *       // both targets live under "parameters").
 *       "sourceDevice": "",
 *       "sourceAxis": 10,
 *       "actionType": "SplitAxisHandler",
 *       "mode": "Global",
 *       "parameters": {
 *         "lowerTargetOutputId": 1,
 *         "lowerTargetAxis": 0,
 *         "lowerInvert": false,
 *         "upperTargetOutputId": 1,
 *         "upperTargetAxis": 1,
 *         "upperInvert": false,
 *         "splitMode": 0             // optional; default 0 (CenterToEdges); 1 = Sequential
 *       }
 *     },
 *     {
 *       // Splits an axis' travel into independent threshold zones, each
 *       // firing its own vJoy button on entry/exit (Fase 10.9) - e.g. the
 *       // last 10% of a throttle axis triggering the afterburner detent
 *       // button. Distinct from AxisToButtonHandler (a single-threshold
 *       // axis->button *bridge* to any wrapped handler) - see
 *       // AxisSplitterHandler's own class docs for why they're separate.
 *       "sourceDevice": "",
 *       "sourceAxis": 8,
 *       "actionType": "AxisSplitterHandler",
 *       "mode": "Global",
 *       "targetOutputId": 1,          // vJoy device id, [1, 16]
 *       "parameters": {
 *         "inputMin": 0,              // optional; default 0
 *         "inputMax": 65535,          // optional; default 65535
 *         "zones": [
 *           { "min": 0.9, "max": 1.0, "targetButton": 5 }   // fractions of [inputMin, inputMax]
 *         ]
 *       }
 *     },
 *     {
 *       // Rotary/cyclic button (Fase 13): each press fires the next
 *       // action in "actions" (wraps back to the first after the last)
 *       // instead of always firing the same one - e.g. cycling a vJoy
 *       // button through three different remaps on successive clicks.
 *       "sourceDevice": "",
 *       "sourceButton": 14,
 *       "actionType": "SequenceHandler",
 *       "mode": "Global",
 *       "parameters": {
 *         "actions": [
 *           { "actionType": "ButtonRemapHandler", "targetOutputId": 1, "targetButton": 6 },
 *           { "actionType": "ButtonRemapHandler", "targetOutputId": 1, "targetButton": 7 }
 *         ]
 *       }
 *     },
 *     {
 *       // Simple-moving-average anti-jitter filter (Fase 13) wrapped
 *       // around an axis handler - smooths the last "windowSize" raw
 *       // samples before forwarding to "wrappedAction" (typically a
 *       // CurveHandler), killing tremor from cheap potentiometers.
 *       "sourceDevice": "",
 *       "sourceAxis": 9,
 *       "actionType": "SmoothingHandler",
 *       "mode": "Global",
 *       "parameters": { "windowSize": 5 },
 *       "wrappedAction": { "actionType": "CurveHandler", "targetOutputId": 1, "targetAxis": 9 }
 *     },
 *     {
 *       "sourceDevice": "",
 *       "sourceAxis": 7,
 *       "actionType": "CurveHandler",
 *       "mode": "Global",
 *       "targetOutputId": 1,
 *       "targetAxis": 7,
 *       "parameters": {
 *         "curvePoints": [            // optional; non-empty replaces the Bezier curve with a
 *           { "x": 0.0, "y": 0.0 },   // piecewise-linear interpolation through these points
 *           { "x": 0.5, "y": 0.2 },   // (deadzone/sensitivity still apply around it unchanged)
 *           { "x": 1.0, "y": 1.0 }
 *         ]
 *       }
 *     },
 *     {
 *       // Pauses this handler's own completion for "delayMs" without
 *       // blocking the EventRouter tick (QTimer::singleShot). Standalone
 *       // leaf action - has no wrapped target to fire once the delay
 *       // elapses (see DelayAction's own class docs).
 *       "sourceDevice": "",
 *       "sourceButton": 16,
 *       "actionType": "DelayAction",
 *       "mode": "Global",
 *       "parameters": { "delayMs": 500 }
 *     },
 *     {
 *       // Plays a .wav/.mp3 file (Qt Multimedia) on press.
 *       "sourceDevice": "",
 *       "sourceButton": 17,
 *       "actionType": "AudioAction",
 *       "mode": "Global",
 *       "parameters": { "filePath": "C:/Sounds/click.wav" }
 *     },
 *     {
 *       // Reads "text" out loud (QTextToSpeech) on press.
 *       "sourceDevice": "",
 *       "sourceButton": 18,
 *       "actionType": "TTSAction",
 *       "mode": "Global",
 *       "parameters": { "text": "Gear down" }
 *     }
 *   ]
 * }
 * @endcode
 *
 * "mode" scopes *when* a binding is active (see EventRouter's own class
 * docs for exactly how Global-mode bindings combine with the currently
 * active mode); it is unrelated to "actionType", which picks *what kind*
 * of handler the binding instantiates. "mode" and the source* fields are
 * only meaningful on a *routed* binding (a top-level "bindings" array entry,
 * or a value passed to loadProfile itself) - a nested binding object
 * ("wrappedAction"/"shortActions"/"longActions"/"doubleActions") is only ever
 * instantiated, never routed on its own, so those fields are ignored there.
 */
class ProfileManager : public QObject
{
    Q_OBJECT

public:
    /// Calibrated raw min/max for one device axis (Fase 10.9's Calibration
    /// Wizard) - min/max are raw hardware units, the same space DeviceManager
    /// reports axis values in, so getAxisCalibration()'s result can replace
    /// a CurveHandler binding's inputMin/inputMax as-is.
    struct AxisCalibration
    {
        int min = 0;
        int max = 0;
    };

    /**
     * @brief Loads a profile from filePath and applies it to router.
     *
     * Clears router's existing routes/devices first (EventRouter::clearRoutes()),
     * then for each entry in "bindings": instantiates the handler tree named
     * by "actionType" (recursively, for wrapper types - see the schema
     * docs above), wires up whatever leaf handlers need (a vJoy device for
     * "CurveHandler", ...), and adds the resulting top-level route under
     * that binding's "mode" (default "Global").
     *
     * Returns false if filePath can't be opened, isn't valid JSON, or has
     * no "bindings" array — i.e. if the profile itself is unusable. A
     * malformed individual binding inside an otherwise-valid profile is
     * logged (qWarning) and skipped rather than failing the whole load, so
     * one bad entry can't take down an otherwise-good profile.
     */
    bool loadProfile(const QString &filePath, EventRouter &router);

    /// Serializes profileData to filePath as pretty-printed JSON.
    bool saveProfile(const QString &filePath, const QJsonObject &profileData);

    /// Renders router's *live* routing table (Fase 14, same "walk
    /// allRoutes(), read each handler's own toJson()" approach as
    /// serializeProfile()) as a simple one-page-per-overflow PDF table at
    /// filePath: "Input -> Action Type -> Details", one row per route,
    /// via QPdfWriter/QPainter. A route whose handler doesn't override
    /// toJson() (see IActionHandler's docs) still gets a row - just with
    /// "(unsupported)" in place of an action type/details - rather than
    /// being silently dropped from the cheatsheet. Returns false if
    /// filePath can't be opened for writing.
    bool exportCheatsheetPdf(const QString &filePath, const EventRouter &router,
                              const QString &profileName = QStringLiteral("Untitled")) const;

    /// Instantiates binding's handler (recursing into "wrappedAction"/
    /// "shortActions"/"longActions"/"doubleActions" for wrapper actionTypes)
    /// and registers the resulting route on router under binding's
    /// "sourceAxis" or "sourceButton" (whichever is present) and "mode".
    /// Returns false (after logging why) if the actionType is unsupported,
    /// the binding is otherwise malformed, or it has neither a
    /// "sourceAxis" nor a "sourceButton". Public (Fase 10.8) so a single
    /// binding can be applied incrementally at runtime - e.g. the Profiles
    /// screen's Action Picker (ProfileEditorViewModel::bindAction()) -
    /// without going through loadProfile()'s router.clearRoutes() +
    /// whole-file loop, which would wipe every other binding already in
    /// effect. loadProfile() itself is just this, called once per entry in
    /// a profile's "bindings" array.
    bool applyBinding(const QJsonObject &binding, EventRouter &router);

    /// Rebuilds a full profile JSON object (same "profileName"/"bindings"
    /// shape loadProfile()/saveProfile() use) from router's *live* routing
    /// table (Fase 10.8) - each handler's own IActionHandler::toJson()
    /// supplies the actionType/parameters/target* fields, so this reflects
    /// every binding actually in effect (both ones loaded from a file and
    /// ones added at runtime via applyBinding()), not a cached snapshot of
    /// whatever was last read from disk. Routes whose handler doesn't
    /// override toJson() (still returns the IActionHandler default - see
    /// its docs) are skipped, with a qWarning, rather than writing out a
    /// binding that would just fail to reload.
    QJsonObject serializeProfile(const EventRouter &router, const QString &profileName = QStringLiteral("Untitled")) const;

    /// Renames every trace of oldName to newName across router's *live*
    /// routing table - the "mode" field of every binding (at any nesting
    /// depth - Tempo cascade slots, Sequence steps, ConditionHandler's
    /// wrappedAction, ...) AND every ModeSwitch/TemporaryModeSwitch
    /// binding's own "parameters.targetMode", wherever oldName appears as
    /// either. There is no in-place "re-key just one mode" operation on
    /// EventRouter's own routing tables (see its class docs - each mode is
    /// its own RouteTable inside an unordered_map keyed by mode name), so
    /// this instead: (1) serializeProfile()s the current live state, (2)
    /// walks that JSON tree replacing oldName with newName everywhere
    /// above, (3) router.clearRoutes() + re-applies every (now-renamed)
    /// binding via applyBinding() - the same "reload from a JSON tree"
    /// path loadProfile() itself uses, just sourced from memory instead of
    /// a file. If router's own *live* active mode (router.currentMode())
    /// was oldName, it's switched to newName too via router.setMode() so
    /// live dispatch doesn't go dark for whoever is currently in it.
    /// Returns false (no-op) if oldName/newName is empty, they're equal,
    /// oldName is EventRouter::kGlobalMode (never renamable - it's the
    /// engine's own hardcoded always-active fallback, not a user-named
    /// mode), or newName already names any mode currently in use (rejected
    /// rather than silently merging two modes' bindings together).
    bool renameMode(const QString &oldName, const QString &newName, EventRouter &router);

    /// Looks up a calibration recorded for (systemPath, axisIndex) via
    /// setAxisCalibration() or loaded from a profile's "calibrations" array.
    /// Returns false (outMin/outMax untouched) if this axis has never been
    /// calibrated.
    bool getAxisCalibration(const QString &systemPath, int axisIndex, int &outMin, int &outMax) const;

    /// Records (or overwrites) the calibrated raw range for one device axis -
    /// the Calibration Wizard's eventual save action. Only affects future
    /// instantiateCurveHandler()/instantiateAxisSplitterHandler() calls, not
    /// any handler already instantiated from an earlier bindAction()/
    /// loadProfile() call.
    void setAxisCalibration(const QString &systemPath, int axisIndex, int calibratedMin, int calibratedMax);

    /// One vJoy device's occupancy snapshot for the vJoy Auditor popup - see
    /// vjoyOccupancy() below.
    struct VJoyOccupancy
    {
        /// Size 8, index-aligned with VJoyDevice's own axis order (see
        /// VJoyDevice::setAxis()): 0=X, 1=Y, 2=Z, 3=Rx, 4=Ry, 5=Rz,
        /// 6=Slider, 7=Dial.
        QVector<bool> axes = QVector<bool>(8, false);

        /// Size 128, index N == vJoy button N+1 (0-based throughout this
        /// codebase's own binding schema - see ButtonRemapHandler's
        /// targetButton, which is fed straight into
        /// IVirtualOutputDevice::setButton() with no +1/-1 adjustment).
        QVector<bool> buttons = QVector<bool>(128, false);
    };

    /// vJoy Auditor popup: scans router's *live* routing table (the same
    /// walk serializeProfile() already does - every RouteDescriptor's
    /// handler->toJson()) for every binding that targets vJoy device
    /// targetOutputId, marking the exact axis/button index it occupies.
    /// Recurses into every nested binding shape this class's own JSON
    /// schema (see the class docs above) actually produces -
    /// "wrappedAction" (ConditionHandler/ToggleHandler/
    /// AxisToButtonHandler), "shortActions"/"longActions"/"doubleActions"
    /// (TempoHandler), "parameters.actions" (SequenceHandler),
    /// "parameters.steps[].buttonIndex" (MacroHandler),
    /// "parameters.zones[].targetButton" (AxisSplitterHandler), and
    /// SplitAxisHandler's self-contained "parameters.lower/upperTargetOutputId"
    /// + "parameters.lower/upperTargetAxis" pairs - so a button/axis buried
    /// inside a cascade or wrapper still shows as occupied, not just a
    /// top-level binding's own targetAxis/targetButton. Returns every slot
    /// false for a targetOutputId outside vJoy's valid [1, 16] range,
    /// mirroring resolveTargetDevice()'s own tolerant range handling. Read
    /// fresh on every call (not cached) so a bind added earlier this
    /// session always shows up, including one added after the Auditor was
    /// last opened.
    VJoyOccupancy vjoyOccupancy(int targetOutputId, const EventRouter &router) const;

signals:
    /// Emitted whenever setAxisCalibration() records a new calibration.
    void profileChanged();

private:
    /// Builds the IActionHandler binding describes, WITHOUT touching
    /// router's routing tables - the only side effect on router is
    /// registerOutputDevice() for handler kinds that own a vJoy device,
    /// since a device must be ticked regardless of how deep inside a
    /// wrapper chain its handler ends up. This is what applyBinding calls
    /// for the top-level route, and what the three wrapper handlers'
    /// instantiate* functions call on their own nested binding object(s).
    /// Returns nullptr (after logging why) if actionType is unsupported or
    /// binding is otherwise malformed.
    ///
    /// Sprint QoL Part 2: a thin wrapper around instantiateHandlerImpl() -
    /// see that method's own docs for the actual actionType dispatch. All
    /// this adds is calling the freshly-built handler's setNote() with
    /// binding's own "parameters.note" (if any) before returning it, so
    /// every handler kind picks up IActionHandler::note() for free without
    /// each of their own instantiate*Handler() functions needing to touch
    /// it - see IActionHandler::note()'s own docs for why that field lives
    /// there instead of duplicated into every concrete handler.
    /// depth counts wrapper nesting (0 at the top-level binding, +1 per
    /// recursive call from a wrapper's own instantiate*Handler()) - capped
    /// at kMaxHandlerNestingDepth so a corrupted/malicious profile with deep
    /// "wrappedAction"/action-list chains can't exhaust the call stack.
    std::shared_ptr<IActionHandler> instantiateHandler(const QJsonObject &binding, EventRouter &router, int depth = 0);

    /// The actual actionType -> instantiate*Handler() dispatch - see
    /// instantiateHandler() above for the one thing it adds on top of this.
    std::shared_ptr<IActionHandler> instantiateHandlerImpl(const QJsonObject &binding, EventRouter &router, int depth);

    /// Resolves binding's "targetOutputId" (read from idKey) plus its
    /// optional top-level "targetDeviceType" ("vjoy", the default, or
    /// "vigem") into the corresponding cached output device via
    /// VirtualOutputManager - vjoy ids are valid in [1, 16], vigem ids in
    /// [1, 4] (ViGEmDevice's own ad-hoc Xbox 360 slot count). Shared by
    /// every handler kind that owns a single vJoy/ViGEm-backed target, so
    /// the vjoy-vs-vigem branch lives in exactly one place instead of
    /// being duplicated per handler. Attempts acquire() (logging, but not
    /// failing, if it doesn't succeed - see instantiateCurveHandler()'s own
    /// docs on why a binding still gets built either way) but does NOT call
    /// router.registerOutputDevice() - callers do that themselves, since
    /// some (SplitAxisHandler) resolve more than one device per binding.
    /// Returns nullptr (after logging via qWarning, tagged with
    /// handlerName) if binding has no idKey at all, or targetOutputId is
    /// outside its type's valid range.
    std::shared_ptr<IVirtualOutputDevice> resolveTargetDevice(const QJsonObject &binding, const char *handlerName,
                                                                const char *idKey = "targetOutputId");

    std::shared_ptr<IActionHandler> instantiateCurveHandler(const QJsonObject &binding, EventRouter &router);
    std::shared_ptr<IActionHandler> instantiateModeSwitchHandler(const QJsonObject &binding, EventRouter &router);
    std::shared_ptr<IActionHandler> instantiateButtonRemapHandler(const QJsonObject &binding, EventRouter &router);
    std::shared_ptr<IActionHandler> instantiateHatRemapHandler(const QJsonObject &binding, EventRouter &router);
    std::shared_ptr<IActionHandler> instantiateKeyboardHandler(const QJsonObject &binding);
    std::shared_ptr<IActionHandler> instantiateTemporaryModeSwitchHandler(const QJsonObject &binding,
                                                                            EventRouter &router);
    std::shared_ptr<IActionHandler> instantiateTrimHandler(const QJsonObject &binding, EventRouter &router);
    std::shared_ptr<IActionHandler> instantiateMouseHandler(const QJsonObject &binding);
    std::shared_ptr<IActionHandler> instantiateConditionHandler(const QJsonObject &binding, EventRouter &router, int depth);
    std::shared_ptr<IActionHandler> instantiateToggleHandler(const QJsonObject &binding, EventRouter &router, int depth);
    std::shared_ptr<IActionHandler> instantiateTempoHandler(const QJsonObject &binding, EventRouter &router, int depth);

    /// Shared by instantiateTempoHandler()'s three gesture slots: reads
    /// binding[arrayKey] as a JSON array of action objects (the current
    /// "shortActions"/"longActions"/"doubleActions" schema) and instantiates
    /// each, skipping (with a qWarning) any individual entry that fails -
    /// same forgiving policy as instantiateSequenceHandler()'s own
    /// "parameters.actions" list. Falls back to binding[legacySingularKey]
    /// as a single action object (the pre-cascade schema) when arrayKey is
    /// absent, so profiles saved before TempoHandler supported multiple
    /// actions per gesture still load with that one action intact instead
    /// of silently losing it.
    std::vector<std::shared_ptr<IActionHandler>> instantiateActionList(const QJsonObject &binding,
                                                                          const QString &arrayKey,
                                                                          const QString &legacySingularKey,
                                                                          EventRouter &router, int depth);
    std::shared_ptr<IActionHandler> instantiateAxisToButtonHandler(const QJsonObject &binding, EventRouter &router, int depth);
    std::shared_ptr<IActionHandler> instantiateMergeAxisHandler(const QJsonObject &binding, EventRouter &router);
    std::shared_ptr<IActionHandler> instantiateSplitAxisHandler(const QJsonObject &binding, EventRouter &router);
    std::shared_ptr<IActionHandler> instantiateMacroHandler(const QJsonObject &binding, EventRouter &router);
    std::shared_ptr<IActionHandler> instantiateAxisSplitterHandler(const QJsonObject &binding, EventRouter &router);
    std::shared_ptr<IActionHandler> instantiateSequenceHandler(const QJsonObject &binding, EventRouter &router, int depth);
    std::shared_ptr<IActionHandler> instantiateSmoothingHandler(const QJsonObject &binding, EventRouter &router, int depth);
    std::shared_ptr<IActionHandler> instantiateDelayAction(const QJsonObject &binding);
    std::shared_ptr<IActionHandler> instantiateAudioAction(const QJsonObject &binding);
    std::shared_ptr<IActionHandler> instantiateTTSAction(const QJsonObject &binding);

    /// Lazily creates (on first use) and returns the single
    /// SendInputKeyboardBackend instance shared by every KeyboardHandler
    /// binding this ProfileManager applies - safe to share since the
    /// backend itself is stateless.
    std::shared_ptr<IKeyboardBackend> keyboardBackend();

    std::shared_ptr<IKeyboardBackend> m_keyboardBackend;

    /// Calibrated raw min/max per (systemPath, axisIndex), populated from a
    /// profile's "calibrations" array (loadProfile()) and/or setAxisCalibration().
    QMap<QPair<QString, int>, AxisCalibration> m_calibrations;
};
