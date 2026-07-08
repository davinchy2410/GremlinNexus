#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "ProfileManager.h"

struct DeviceInfo;
class EventRouter;
class AutoSwitchManager;
class PwaServer;

/**
 * @brief Hierarchical (Device -> Physical Input -> optional Binding) model
 *        backing the QML "Profiles" screen (Phase 10 Part 2).
 *
 * The top level (one row per device) is a real QAbstractListModel, so the
 * outer view gets proper role-based delegate reuse/change notification.
 * Each device's "inputs" role is a plain QVariantList of QVariantMap - a
 * flat list nested one level deep rather than a second QAbstractItemModel,
 * since it never needs to be independently sorted/filtered/queried and a
 * device's own input count is small (tens, not thousands): a Repeater
 * consuming a QVariantList directly in QML is both simpler to write here
 * and perfectly adequate at this scale.
 *
 * Data source: real connected devices, via DeviceManager (kept live via
 * deviceAdded/deviceRemoved). Per-input *binding* information has no
 * introspection API yet (EventRouter's routing tables are not queryable
 * by (systemPath, index) from outside) - bindingLabel/hasBinding start as
 * unbound placeholders for every real device's inputs, and only become
 * accurate once bindAction() (Fase 10.8) has been used to bind that
 * specific input *this session*; a route that already existed in
 * m_router before this ViewModel was constructed (e.g. one loaded from a
 * profile at startup, before the Profiles screen ever opened) still shows
 * as unbound here until touched, for the same reason. So the screen is
 * never empty and never lies about data it doesn't have: if no hardware is
 * connected yet, two clearly-fictional devices (with a couple of
 * illustrative *mock* bindings, so the binding-badge UI has something real
 * to render) are shown instead, and are wholesale replaced - not merged -
 * the moment any real device is detected.
 *
 * Fase 10.8 also adds two features layered on top of the same device list:
 *  - Quick Bind: while listenForInputs is true, any physical axis/button
 *    event re-emits as hardwareInputDetected(devicePath, inputName) so the
 *    QML view can auto-scroll to and flash that row - "wiggle the stick,
 *    the UI finds the row for you" instead of counting buttons by hand.
 *  - Modes: a plain in-memory list of mode names (see modes/currentMode)
 *    that bindAction() and the Action Picker's "mode" argument draw from -
 *    this ViewModel does not persist modes on its own; a mode only
 *    "exists" for real once at least one binding is registered under it.
 */
class ProfileEditorViewModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool listenForInputs READ listenForInputs WRITE setListenForInputs NOTIFY listenForInputsChanged)
    Q_PROPERTY(QStringList modes READ modes NOTIFY modesChanged)
    // Fase SC-7.10: exposed as properties (not just the plain Q_INVOKABLE
    // hasCopiedAction() below) because QML only ever re-evaluates an
    // invokable's *binding* when one of its own directly-referenced
    // arguments changes - root.inputKind stays the same string across
    // opening one axis' Action Picker after another, so
    // "visible: profileEditorViewModel.hasCopiedAction(root.inputKind)"
    // silently kept returning whatever it evaluated to the very first time,
    // never re-checking after a later copyAction(). A NOTIFY-backed property
    // re-evaluates its binding whenever clipboardChanged() fires instead.
    Q_PROPERTY(bool hasCopiedAxis READ hasCopiedAxis NOTIFY clipboardChanged)
    Q_PROPERTY(bool hasCopiedButton READ hasCopiedButton NOTIFY clipboardChanged)
    Q_PROPERTY(QString currentMode READ currentMode WRITE setCurrentMode NOTIFY currentModeChanged)
    Q_PROPERTY(QString currentProfileName READ currentProfileName NOTIFY currentProfileNameChanged)
    // Fase (Curves nav rework): which device row CurveEditorView.qml's own
    // Device combo should pre-select next time it's shown - set by
    // setCurrentDeviceForCurves() when a DeviceCard's "Curves" button is
    // clicked, read once by CurveEditorView.qml's Connections handler below.
    Q_PROPERTY(int curvesTargetDeviceRow READ curvesTargetDeviceRow NOTIFY curvesTargetDeviceRowChanged)
    // Sprint Final (Curves deep-linking): which axis input name
    // curvesTargetDeviceRow's own device row should pre-select in
    // CurveEditorView.qml's Axis ComboBox - set together with
    // curvesTargetDeviceRow by setCurveEditorTarget(), read once by the same
    // Connections handler that already reacts to curvesTargetDeviceRowChanged.
    Q_PROPERTY(QString curvesTargetInputName READ curvesTargetInputName NOTIFY curvesTargetInputNameChanged)

public:
    enum Role
    {
        DeviceNameRole = Qt::UserRole + 1,
        VendorProductRole,
        SystemPathRole,
        InputsRole,
    };

    explicit ProfileEditorViewModel(EventRouter &router, AutoSwitchManager &autoSwitch, PwaServer &pwaServer,
                                     QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Names of just the axis-kind inputs for one device row, e.g. for the
    /// Curves screen's "Axis" routing ComboBox (Phase 10 Part 6) - that
    /// screen only cares about axes (a response curve has no meaning for a
    /// button), so this filters out button/hat entries rather than making
    /// the QML side re-filter the full "inputs" list itself. Returns an
    /// empty list for an out-of-range deviceRow.
    Q_INVOKABLE QStringList axisNamesForDevice(int deviceRow) const;

    /// systemPath of one device row - lets a plain index-based ComboBox
    /// selection (e.g. the Device Tester's device picker) resolve back to
    /// the DeviceManager-unique key other ViewModels key their state off
    /// of. Returns an empty string for an out-of-range deviceRow.
    Q_INVOKABLE QString systemPathForDevice(int deviceRow) const;

    /// Loads filePath (a local path or a "file:///..." URL, as produced by
    /// QML's FileDialog.selectedFile) via ProfileManager - clearing and
    /// re-populating m_router's routing table with the profile's bindings -
    /// and, on success, remembers its "profileName" (see saveProfileToPath())
    /// and marks every input the loaded profile bound as m_router's routes
    /// now reflect. Returns whether the load succeeded.
    Q_INVOKABLE bool loadProfileFromPath(const QString &filePath);

    /// Writes m_router's *live* routing table to filePath via
    /// ProfileManager::serializeProfile() (Fase 10.8) - every binding
    /// actually in effect, whether it came from a loaded file or was added
    /// this session via bindAction(), not a cached snapshot of whatever was
    /// last read from disk. Reuses the most recently loaded profile's name
    /// (or "Untitled" if nothing has been loaded yet this session). Returns
    /// whether the write succeeded.
    Q_INVOKABLE bool saveProfileToPath(const QString &filePath);

    /// Global "Quick Save" (Fase 14.1, TopHeader button): re-saves to
    /// m_currentFilePath (whatever loadProfileFromPath()/saveProfileToPath()
    /// last succeeded with this session) without prompting for a path again.
    /// If nothing has been loaded/saved yet this session, there is no path
    /// to reuse - emits saveDialogRequested() instead, so main.qml's global
    /// FileDialog can ask for one (after which the normal
    /// saveProfileToPath() flow caches it for next time).
    Q_INVOKABLE void quickSave();

    /// Row index of the device whose systemPath is systemPath, or -1 if not
    /// found - the reverse of systemPathForDevice(), e.g. for resolving a
    /// DeviceManager-originated hardwareInputDetected(devicePath, ...) back
    /// to a QML Repeater index (see ProfileEditorView.qml's Quick Bind
    /// handling).
    Q_INVOKABLE int deviceRowForSystemPath(const QString &systemPath) const;

    /// Row index most recently targeted via setCurrentDeviceForCurves()/
    /// setCurveEditorTarget(), or -1 if neither has been called this session.
    int curvesTargetDeviceRow() const;

    /// Axis input name most recently targeted via setCurveEditorTarget(), or
    /// "" if that has never been called this session (or the last call was
    /// setCurrentDeviceForCurves()'s own device-only form, which clears it).
    QString curvesTargetInputName() const;

    /// Records which device the Curves screen should pre-select next time
    /// it's shown (Fase: Curves nav rework - DeviceCard's own "Curves"
    /// button calls this with its systemPath before switching
    /// mainViewModel.currentView, so opening Curves lands already targeted
    /// at the device the user clicked from, instead of always resetting to
    /// row 0). A no-op if systemPath doesn't resolve to any known device row.
    /// A thin wrapper around setCurveEditorTarget(systemPath, "") (Sprint
    /// Final) - kept as its own method since it's still a meaningfully
    /// different intent ("just switch device, don't pin an axis") from a
    /// per-axis deep link.
    Q_INVOKABLE void setCurrentDeviceForCurves(const QString &systemPath);

    /// Sprint Final (Curves deep-linking): records BOTH which device row and
    /// which of its own axis inputs the Curves screen should pre-select next
    /// time it's shown - InputRow's own per-axis Curves icon (visible only
    /// for inputKind === "axis") calls this with its devicePath/inputName
    /// before switching mainViewModel.currentView, so the screen opens with
    /// its Axis ComboBox already pointed at the exact axis clicked, instead
    /// of just the right device at whatever axis happened to be selected
    /// last. A no-op (leaves both curvesTargetDeviceRow/curvesTargetInputName
    /// untouched) if systemPath doesn't resolve to any known device row -
    /// same "invalid target is silently ignored" contract as
    /// setCurrentDeviceForCurves() already had.
    Q_INVOKABLE void setCurveEditorTarget(const QString &systemPath, const QString &inputName);

    /// Registers one new binding (Fase 10.8's Action Picker) - devicePath +
    /// inputName resolve to a (kind, index) via that device's own "inputs"
    /// list (see makeInputEntry()'s "inputIndex" role), mode is the route's
    /// mode (falls back to EventRouter::kGlobalMode if empty), and
    /// actionDataJson is a JSON-encoded object with at least "actionType"
    /// and whatever that type's own target*/parameters fields are (see
    /// ProfileManager's class docs for the exact per-type shape - this is
    /// the same shape as one entry of a profile's "bindings" array, minus
    /// "sourceDevice"/"sourceAxis"/"sourceButton"/"mode", which this method
    /// fills in itself from its own devicePath/inputName/mode arguments).
    /// Applies immediately via ProfileManager::applyBinding() (does not
    /// touch any other existing route) and, on success, updates that
    /// input's hasBinding/bindingLabel so the Profiles screen reflects it
    /// without a full reload. Returns whether the bind succeeded.
    Q_INVOKABLE bool bindAction(const QString &devicePath, const QString &inputName, const QString &mode,
                                 const QString &actionDataJson);

    /// Removes whatever is routed at this exact (devicePath, inputName,
    /// mode) - the inverse of bindAction() (Fase 20.2). Replaces the route
    /// with a null handler (rather than erasing the routing-table entry
    /// outright) via EventRouter::addAxisRoute()/addButtonRoute(), the same
    /// "replace whatever was already there" mechanism every other bind goes
    /// through, then resets that input's hasBinding/bindingLabel back to
    /// "Unbound". A no-op (with a qWarning) if devicePath or inputName don't
    /// resolve to a known device/input.
    Q_INVOKABLE void unbindAction(const QString &devicePath, const QString &inputName, const QString &mode);

    /// Fase 20.17: the JSON of whatever binding is currently routed at this
    /// exact (devicePath, inputName, mode) - the read-side counterpart to
    /// bindAction(), used by ActionPickerPopup.qml to re-populate its fields
    /// when re-opening an input that's already bound, instead of always
    /// resetting to blank defaults. Empty string if there's no route, no
    /// handler, or the input name doesn't resolve.
    Q_INVOKABLE QString getActionDataJson(const QString &devicePath, const QString &inputName,
                                            const QString &mode) const;

    /// Sprint 2 (refined): whether inputName on devicePath currently has a
    /// CurveHandler bound under m_currentMode whose shape was actually
    /// authored by the user - lets InputRow.qml show a small curve badge
    /// next to an axis' name. Every axis binding is a CurveHandler
    /// internally (it also carries plain deadzone/sensitivity/input-range
    /// scaling), so actionType alone can't distinguish a real custom curve
    /// from a straight-line default - this also inspects "parameters":
    /// "curvePoints" with more than 2 entries, or exactly 2 that aren't the
    /// default [-1,-1]->[1,1] straight line, or a "sensitivity" != 1.0 all
    /// count as "customized"; "deadzone"/"inputMin"/"inputMax" are
    /// deliberately ignored. False for an unknown devicePath/inputName, any
    /// non-CurveHandler binding (including none at all), or a CurveHandler
    /// whose shape is still the default.
    Q_INVOKABLE bool hasCurve(const QString &devicePath, const QString &inputName) const;

    /// First free vJoy button (0-based) on device targetOutputId (bugfix,
    /// replacing the old Fase 20.1 "historical max + 1" behavior) -
    /// searches upward from whatever recordVjoyButtonAssigned() last
    /// recorded for this exact device (button 0 if nothing's been recorded
    /// yet this session), wrapping around from button 127 back to 0 instead
    /// of stopping there, and returns the first one that isn't occupied.
    /// Free/occupied is read from ProfileManager::vjoyOccupancy() - the
    /// same recursive scan the vJoy Auditor popup uses - so a button
    /// already claimed by a binding buried inside a Tempo/Toggle/Macro
    /// cascade is correctly skipped too, not just a flat top-level
    /// ButtonRemapHandler the old "historical max" scan alone could see.
    /// Lets the Action Picker's "Target Button" stepper suggest a real gap
    /// (e.g. button 21 after the user manually jumped down to button 20)
    /// instead of blindly climbing past whatever the highest button number
    /// ever used happened to be. Returns 0 (button 1) in the degenerate
    /// case where all 128 buttons on this device are already occupied - a
    /// safe fallback, not a meaningful "free slot" claim.
    Q_INVOKABLE int nextAvailableVjoyButton(int targetOutputId) const;

    /// Records that targetButton (0-based) was just assigned to
    /// targetOutputId - either typed manually or accepted from
    /// nextAvailableVjoyButton()'s own suggestion - so nextAvailableVjoyButton()'s
    /// NEXT search for this exact vJoy device starts from here. Call this
    /// once a vJoy button remap bindAction() call actually succeeds; without
    /// it, a manual jump down to a much lower button number is invisible to
    /// the suggester and the following Add would still resume climbing from
    /// wherever it left off before the jump.
    Q_INVOKABLE void recordVjoyButtonAssigned(int targetOutputId, int targetButton);

    /// Discards every live route (EventRouter::clearRoutes()) and resets
    /// this session back to a blank "Untitled" profile with no file path to
    /// reuse (Fase 20.2) - the "New Profile" header button. Unlike
    /// loadProfileFromPath()'s failure path, this always succeeds: there is
    /// nothing to parse/open that could fail.
    Q_INVOKABLE void newProfile();

    /// "1:1 Map" DeviceCard button (Fase 14): binds every one of
    /// devicePath's own axes/hats/buttons (from that device's already-known
    /// "inputs" list - see makeDeviceEntry()) straight through to the same
    /// index on vJoy device targetOutputId - axis/hat N via a CurveHandler
    /// to targetAxis N, button N via a ButtonRemapHandler to targetButton
    /// N - all under currentMode(). Goes through the same
    /// ProfileManager::applyBinding() call bindAction() uses, so its
    /// existing "replace whatever was already routed at this exact (mode,
    /// systemPath, index)" semantics are what keeps re-running this from
    /// ever producing duplicate bindings for a device that already had some
    /// (custom or otherwise) - there is nothing extra to "clear" first.
    /// Returns whether at least one input mapped successfully.
    Q_INVOKABLE bool create1to1Mapping(const QString &devicePath, int targetOutputId);

    /// Display names (DeviceEntry::name) of every device row, in row order -
    /// for the "Swap Devices" dialog's two device ComboBoxes; combine with
    /// systemPathForDevice(index) to resolve a selection back to the
    /// systemPath swapDevices() needs.
    Q_INVOKABLE QStringList deviceDisplayNames() const;

    /// "Swap Devices" dialog (Fase 14, Smart Swap since Fase SC-7.9): moves
    /// every binding currently routed under m_devices[fromDeviceRow]'s
    /// systemPath onto m_devices[toDeviceRow]'s systemPath instead - but only
    /// the ones that actually fit the destination's real input count (index
    /// < its numAxes/numButtons). A binding that doesn't fit (a ghost/
    /// out-of-range one the destination physically can't represent) is left
    /// on the source row instead of being moved somewhere it couldn't be
    /// seen or acted on - see class docs on the "[Legacy]"/orphan-device
    /// placeholders this is most useful for. The source row is only removed
    /// if nothing was left behind for it to still show, and if it was one of
    /// loadProfileFromPath()'s synthesized "Offline / Imported Device"
    /// placeholders (Fase SC-7.3/7.4) to begin with. Every row's
    /// hasBinding/bindingLabel is then refreshed straight from the live
    /// router (updateAllInputBindingLabels()) rather than trying to patch
    /// the affected rows' caches by hand. Returns false for an out-of-range
    /// or equal row pair without touching anything.
    Q_INVOKABLE bool swapDevices(int fromDeviceRow, int toDeviceRow);

    /// Fase SC-7.5: clears every binding (in every mode) routed under
    /// devicePath, then refreshes the UI the same way swapDevices() does. If
    /// devicePath was one of the synthesized "Offline / Imported Device"
    /// placeholder rows (Fase SC-7.3), clearing its only bindings leaves it
    /// with nothing left to show, so that row is removed too - the same
    /// "empty legacy device vanishes" behavior swapDevices() already has,
    /// now reachable without having to Swap onto real hardware first. A
    /// no-op if devicePath isn't a known device row.
    Q_INVOKABLE void clearDeviceBindings(const QString &devicePath);

    /// Fase SC-7.7: copies devicePath/inputName/mode's current binding (via
    /// getActionDataJson()) into a single app-wide clipboard, tagged with
    /// inputKind ("axis"/"button") so pasteAction() can refuse to paste an
    /// axis curve onto a button or vice versa. Overwrites whatever was
    /// copied before - there is only ever one clipboard slot, matching a
    /// plain OS clipboard's own one-thing-at-a-time model rather than a
    /// multi-slot buffer. If the input has no binding, the clipboard is left
    /// holding an empty string, which hasCopiedAction() always reports as
    /// "nothing copied" regardless of inputKind.
    Q_INVOKABLE void copyAction(const QString &devicePath, const QString &inputName, const QString &mode,
                                 const QString &inputKind);

    /// Whether the clipboard currently holds a binding compatible with
    /// inputKind - lets QML decide whether to show a "Paste" button at all.
    Q_INVOKABLE bool hasCopiedAction(const QString &inputKind) const;

    /// hasCopiedAxis/hasCopiedButton Q_PROPERTY READ functions (Fase
    /// SC-7.10) - thin wrappers around hasCopiedAction("axis")/("button").
    bool hasCopiedAxis() const;
    bool hasCopiedButton() const;

    /// Applies the clipboard's copied binding (see copyAction()) onto
    /// devicePath/inputName/mode via bindAction(), which re-parses the
    /// stored JSON and rebinds it to whatever routing index inputName
    /// resolves to on *this* device - the clipboard is never rebound to the
    /// exact same (device, index) it was copied from, so pasting the same
    /// curve/remap onto a different physical input's own index is exactly
    /// the point. A no-op if hasCopiedAction(inputKind) is false (nothing
    /// copied yet, or a kind mismatch).
    Q_INVOKABLE void pasteAction(const QString &devicePath, const QString &inputName, const QString &mode,
                                  const QString &inputKind);

    /// Exports m_router's *live* routing table as a one-page-per-overflow
    /// PDF cheatsheet at filePath (Fase 14) - thin QML-facing wrapper
    /// around ProfileManager::exportCheatsheetPdf(), the same delegation
    /// shape saveProfileToPath() already uses for
    /// ProfileManager::serializeProfile()/saveProfile(). Returns whether
    /// the PDF was written successfully.
    Q_INVOKABLE bool exportCheatsheetPdf(const QString &filePath);

    /// Fase SC-3: cross-references scInput (a Star Citizen <rebind
    /// input="..">, e.g. "js2_button5") against this session's *live*
    /// routing table via SCIntegrationManager::resolvePhysicalSource() -
    /// serializes m_router the same way saveProfileToPath() does (not
    /// m_currentProfileData, which per its own docs stops reflecting new
    /// bindings the instant one is added this session), so a binding made
    /// after the profile was loaded is still found. Returns the physical
    /// device/input description SCIntegrationManager formats, or an empty
    /// string if nothing in the live profile targets that vJoy output.
    Q_INVOKABLE QString resolveSCInput(const QString &scInput) const;

    /// Whether HidHideCLI.exe (Fase 11) is installed - lets DeviceCard.qml's
    /// cloak/uncloak button hide or disable itself when the optional HidHide
    /// dependency isn't present, rather than offering a control that would
    /// silently do nothing. Thin QML-facing wrapper around
    /// HidHideManager::isInstalled().
    Q_INVOKABLE bool isHidHideInstalled() const;

    /// Whether systemPath is currently hidden from non-whitelisted
    /// applications - queries HidHideManager::getCloakedDevices() fresh each
    /// call (per HidHideManager's own docs) rather than caching, since
    /// nothing here is notified of cloak state changing outside of this
    /// process (e.g. via HidHide's own GUI).
    Q_INVOKABLE bool isDeviceCloaked(const QString &systemPath) const;

    /// Cloaks or uncloaks systemPath via HidHideManager - the DeviceCard.qml
    /// button behind this re-reads isDeviceCloaked() right after calling it
    /// (the underlying HidHideCLI.exe call is synchronous) to refresh its
    /// own displayed state, so no extra changed-signal is needed here.
    Q_INVOKABLE void setDeviceCloaked(const QString &systemPath, bool cloak);

    /// Registers (or overwrites) an Auto-Switch rule (Fase 12): whenever
    /// exeName becomes the foreground application's executable, m_autoSwitch
    /// requests loading profilePath. Thin QML-facing wrapper around
    /// AutoSwitchManager::addRule().
    Q_INVOKABLE void addAutoSwitchRule(const QString &exeName, const QString &profilePath);

    /// Removes exeName's Auto-Switch rule, if any. Thin QML-facing wrapper
    /// around AutoSwitchManager::removeRule().
    Q_INVOKABLE void removeAutoSwitchRule(const QString &exeName);

    /// Sets the Auto-Switch profile to fall back to when the foreground
    /// application matches no rule. Thin QML-facing wrapper around
    /// AutoSwitchManager::setDefaultProfile().
    Q_INVOKABLE void setAutoSwitchDefaultProfile(const QString &profilePath);

    Q_INVOKABLE QVariantMap autoSwitchRules() const;
    Q_INVOKABLE QString autoSwitchDefaultProfile() const;

    /// Opens the PWA Desktop Editor (Fase 16) in the system's default
    /// browser - m_pwaServer's own HTTP dashboard (see PwaServer's class
    /// docs) at "/?mode=editor", with the current securityToken appended
    /// (the same "?token=..." query-string convention RemoteControlPopup.qml
    /// already uses for its pairing URL) so the editor session actually
    /// authenticates instead of sitting at the WebSocket's 10s auth timeout
    /// with an empty token. index.html reads "mode=editor" to spoof its own
    /// deviceName to "DesktopEditor" and unlock in-place layout editing.
    Q_INVOKABLE void openPwaEditor();

    /// Exposes m_profileManager (Fase 11) so main.cpp can construct
    /// CalibrationViewModel against the same ProfileManager instance whose
    /// setAxisCalibration() actually affects what saveProfileToPath()/
    /// serializeProfile() write out - a ViewModel-only wrapper method won't
    /// do here since main.cpp needs the reference itself at construction
    /// time, before the QML engine (and therefore any Q_INVOKABLE call) exists.
    ProfileManager &profileManager();

    bool listenForInputs() const;
    void setListenForInputs(bool listen);

    /// Mode names bindAction()/currentMode draw from - always contains at
    /// least EventRouter::kGlobalMode (never removable, see removeMode()).
    QStringList modes() const;

    QString currentMode() const;
    void setCurrentMode(const QString &mode);

    /// "profileName" of the most recently loaded/saved profile this session
    /// (Fase 20.1) - "Untitled" before anything has been loaded/saved, same
    /// fallback saveProfileToPath()/exportCheatsheetPdf() already use. A thin
    /// read-only wrapper around m_currentProfileData - see that field's own
    /// docs for why it's otherwise not read from directly by anything but
    /// this and saveProfileToPath().
    QString currentProfileName() const;

    /// Appends name to modes() (no-op if empty or already present) and
    /// selects it as currentMode - "create and switch to it" is the
    /// expected flow for the Modes dropdown's "+" button, not "create it
    /// or so use it later".
    Q_INVOKABLE void addMode(const QString &name);

    /// Removes name from modes() - a no-op for EventRouter::kGlobalMode
    /// (always present; it is the router's own hardcoded always-active
    /// fallback, not just a UI convenience) or a name not currently in the
    /// list. If name was the selected currentMode, falls back to
    /// kGlobalMode. Does NOT retroactively remove bindings already
    /// registered under that mode in m_router - this only edits the
    /// selectable list; EventRouter has no "drop just one mode's routes"
    /// API, and silently deleting a user's existing bindings as a side
    /// effect of tidying up the dropdown would be a surprising, likely
    /// unwanted, behavior.
    Q_INVOKABLE void removeMode(const QString &name);

    /// Renames oldName to newName everywhere it's actually used - unlike
    /// removeMode() above, this is NOT just a cosmetic edit to the
    /// selectable list: every binding currently tagged "mode": oldName (at
    /// any nesting depth - a Tempo cascade slot, a Sequence step, a
    /// ConditionHandler's wrappedAction, ...) is re-tagged to newName, and
    /// every ModeSwitch/TemporaryModeSwitch binding whose own
    /// "parameters.targetMode" names oldName is updated too - a shallow
    /// list-only rename would otherwise leave every one of those bindings
    /// silently orphaned under a mode name nothing can select anymore. See
    /// ProfileManager::renameMode() for the actual JSON-tree walk.
    /// No-op (returns false) for EventRouter::kGlobalMode, an empty
    /// oldName/newName, oldName not currently in modes(), or newName
    /// already naming a *different* existing mode (rejected rather than
    /// silently merging two modes' bindings together).
    Q_INVOKABLE bool renameMode(const QString &oldName, const QString &newName);

    /// Sprint 5 (Familias de Modos): thin delegation to
    /// EventRouter::setModeParent()/getModeParent() - see that method's own
    /// docs for the actual cascade semantics (ModeManagerPopup.qml's
    /// "Inherits from" ComboBox is the only caller). Not validated any
    /// further here: the router already refuses to touch kGlobalMode's own
    /// parent and treats an empty/self parent as "no parent" (see
    /// EventRouter::setModeParent()).
    Q_INVOKABLE void setModeParent(const QString &mode, const QString &parent);
    Q_INVOKABLE QString getModeParent(const QString &mode) const;

    /// vJoy Auditor popup: occupancy snapshot for one vJoy device's 8 fixed
    /// axes (X, Y, Z, Rx, Ry, Rz, Slider, Dial) and 128 buttons, read fresh
    /// from m_router's *live* routing table every call - see
    /// ProfileManager::vjoyOccupancy() for exactly what counts as
    /// "occupied". Returns a QVariantMap with "axes" (8-entry bool
    /// QVariantList) and "buttons" (128-entry bool QVariantList); every
    /// slot is false for a targetOutputId outside vJoy's valid [1, 16]
    /// range.
    Q_INVOKABLE QVariantMap vjoyOccupancy(int targetOutputId) const;

signals:
    /// Fires when listenForInputs is true and a physical axis/button event
    /// arrives for a known device - devicePath is the source device's
    /// systemPath, inputName matches that same input's "name" field in its
    /// device row's "inputs" list (e.g. "Axis 0", "Button 5"), so QML can
    /// find and highlight the corresponding InputRow.
    void hardwareInputDetected(const QString &devicePath, const QString &inputName);
    void bindingUpdated(const QString &devicePath, const QString &inputName, bool hasBinding,
                         const QString &bindingLabel, const QString &actionNote = QString());

    /// quickSave() (Fase 14.1) has no m_currentFilePath to reuse yet this
    /// session - main.qml's global FileDialog should open and ask for one.
    void saveDialogRequested();

    void listenForInputsChanged();
    void modesChanged();
    void currentModeChanged();
    void currentProfileNameChanged();
    void curvesTargetDeviceRowChanged();
    void curvesTargetInputNameChanged();

    /// Fase SC-7.10: fires whenever copyAction() changes the clipboard - the
    /// NOTIFY signal for hasCopiedAxis/hasCopiedButton, so a Paste button's
    /// "visible" binding actually re-evaluates instead of staying frozen at
    /// whatever it first evaluated to.
    void clipboardChanged();

private slots:
    void onDeviceAdded(const DeviceInfo &device);
    void onDeviceRemoved(const QString &systemPath);

    /// Quick Bind detection (Fase 10.8) - re-emits as hardwareInputDetected()
    /// when listenForInputs is true, the event's magnitude clears a small
    /// "moved enough" threshold (so a barely-drifting axis at rest doesn't
    /// spam the UI), and systemPath/axisIndex resolve to a known device/input.
    void onAxisMovedForDetection(const QString &systemPath, int axisIndex, int value);

    /// Quick Bind detection (Fase 10.8) - re-emits as hardwareInputDetected()
    /// on press (not release) when listenForInputs is true and
    /// systemPath/buttonIndex resolve to a known device/input.
    void onButtonPressedForDetection(const QString &systemPath, int buttonIndex, bool pressed);

private:
    /// One row of the model: a device plus its flattened list of physical inputs.
    struct DeviceEntry
    {
        QString name;
        QString vendorProduct; ///< Pre-formatted "VID:.... PID:...." display string.
        QString systemPath;
        QVariantList inputs;
        bool isMock = false;

        /// Capability counts, needed to turn a raw DeviceManager
        /// axisMoved/buttonPressed axisIndex/buttonIndex back into the
        /// right input name (see onAxisMovedForDetection()/
        /// onButtonPressedForDetection(), makeInputEntry()'s "inputIndex"
        /// role, and buttonDisplayName()). Fase 16.7: a POV hat is reported
        /// as 4 synthetic buttons (not a fake axis) at the end of
        /// numButtons - numAxes plays no part in button-index math anymore.
        /// Left at 0 for the illustrative mock devices (see
        /// seedMockDevices()), which never receive real hardware events anyway.
        uint8_t numAxes = 0;
        uint8_t numHats = 0;
        uint8_t numButtons = 0;

        /// Fase 20.13: this axis's real HID logical range (index-aligned
        /// with the physical axes numAxes counts), copied from DeviceInfo -
        /// onAxisMovedForDetection() needs these to size its detection
        /// threshold as a percentage of what this specific axis can
        /// actually report, since a fixed raw-count threshold assumes every
        /// device reports at the same (16-bit) resolution a lower-bit
        /// device (e.g. a 12-bit stick topping out at 4095) can never reach.
        QVector<int> axisLogicalMin;
        QVector<int> axisLogicalMax;
    };

    /// @param inputIndex Routing-space index for this input - plain
    ///                    axisIndex for an "axis"-kind input, or plain
    ///                    buttonIndex (see buttonDisplayName() for how a POV
    ///                    hat's synthetic buttons fit into that space) for a
    ///                    "button"-kind one. This is what bindAction() looks
    ///                    up by matching "name", so it can hand EventRouter
    ///                    the exact index it needs without re-parsing the
    ///                    display name.
    /// Sprint QoL Part 2: note is the binding's optional "parameters.note"
    /// free-text field (see noteFromActionJson()) - "" if none - exposed to
    /// QML under the "actionNote" role for InputRow.qml's own info icon/
    /// ToolTip. Defaulted so every pre-existing call site that has no note
    /// to give (seedMockDevices()'s placeholders, create1to1Mapping()) keeps
    /// compiling unchanged.
    static QVariantMap makeInputEntry(const QString &name, const QString &kind, int inputIndex, bool hasBinding,
                                       const QString &bindingLabel, const QString &note = QString());

    /// Fase 19 bugfix: takes router (rather than just device) so it can
    /// consult EventRouter::allRoutes() and initialize hasBinding/
    /// bindingLabel from whatever is *already* routed for this device -
    /// previously every input started unbound regardless of routes that
    /// predated this row's construction (e.g. a profile loaded at startup),
    /// which is what made the Profiles screen show "Unbound" for real bindings.
    DeviceEntry makeDeviceEntry(const DeviceInfo &device, EventRouter &router);

    /// Appends the two illustrative placeholder devices (see class docs),
    /// with proper begin/endInsertRows - safe to call whenever the real
    /// device list becomes empty, not just from the constructor.
    void seedMockDevices();

    int indexOfSystemPath(const QString &systemPath) const;

    /// Sets hasBinding=true/bindingLabel=label/actionNote=note on the input
    /// named inputName within m_devices[deviceRow].inputs, and emits a
    /// targeted dataChanged() for that row - called by bindAction() on
    /// success so the InputRow's Badge/"Unbound" text (and, Sprint QoL Part
    /// 2, its note info icon/ToolTip) update live, the same targeted-
    /// notification approach CurveEditorViewModel uses for its points (see
    /// that class's docs) rather than a wholesale model reset. note defaults
    /// to "" for the unbind-style callers (unbindAction(), a route displaced
    /// by sameBindingDestination()) that pass an empty label too - an unbound
    /// input has no note to show either.
    void updateInputBindingLabel(int deviceRow, const QString &inputName, const QString &label,
                                  const QString &note = QString());

    /// Refreshes hasBinding/bindingLabel on every input of every device row
    /// from m_router's *current* routing table (Fase 20) - called after
    /// loadProfileFromPath() applies a profile, since that only populates
    /// m_router itself; without this, every row kept showing "Unbound" until
    /// the user happened to re-touch that exact input (updateInputBindingLabel()
    /// only ever updates the one input bindAction() just bound).
    void updateAllInputBindingLabels();

    /// Router whose routing table loadProfileFromPath() applies loaded
    /// profiles into - owned by main.cpp, outlives this ViewModel.
    EventRouter &m_router;

    /// Auto-Switch rule store (Fase 12) that addAutoSwitchRule()/
    /// removeAutoSwitchRule()/setAutoSwitchDefaultProfile() delegate to -
    /// owned by main.cpp (its profileSwitchRequested signal is what's
    /// already wired to this ViewModel's own loadProfileFromPath() there),
    /// outlives this ViewModel the same way m_router does.
    AutoSwitchManager &m_autoSwitch;

    /// PWA WebSocket/HTTP server (Fase 16) that openPwaEditor() reads
    /// serverIp/securityToken off of to build a working authenticated editor
    /// URL - owned by main.cpp, outlives this ViewModel the same way
    /// m_router/m_autoSwitch do.
    PwaServer &m_pwaServer;

    ProfileManager m_profileManager;

    /// Raw JSON of the most recently loaded profile - only "profileName" is
    /// still read out of this (by saveProfileToPath(), see its docs); the
    /// "bindings" it may contain are not used for anything once loaded,
    /// since m_router's live routing table (via
    /// ProfileManager::serializeProfile()) is now the source of truth for
    /// what gets saved. Starts with "profileName": "Untitled" so Save works
    /// even before any Load this session.
    QJsonObject m_currentProfileData;

    /// Local filesystem path quickSave() (Fase 14.1) reuses - set on every
    /// successful loadProfileFromPath()/saveProfileToPath() this session.
    /// Empty until the first one succeeds, which is quickSave()'s cue to
    /// ask for a path (via saveDialogRequested()) instead of guessing one.
    QString m_currentFilePath;

    bool m_listenForInputs = false;

    /// Fase 20.12: last known raw value per (systemPath, axisIndex) while
    /// Quick Bind is listening - onAxisMovedForDetection() compares each new
    /// reading against this instead of a fixed center (32767), since an axis
    /// that physically rests somewhere else entirely (e.g. a Throttle
    /// resting at 0) would otherwise sit permanently past
    /// kAxisDetectionThreshold and report electrical noise as constant motion.
    QHash<QString, QHash<int, int>> m_axisBaselines;

    /// vJoy button auto-suggest (bugfix): last vJoy button (0-based)
    /// actually assigned per vJoy device id, via recordVjoyButtonAssigned() -
    /// nextAvailableVjoyButton()'s search for a given device starts one past
    /// whichever value is recorded here, not from a historical maximum.
    /// Absent key ("this device id was never assigned this session") is
    /// read as -1 by nextAvailableVjoyButton() - search starts at button 0.
    QHash<int, int> m_lastAssignedVjoyButton;

    /// Mode names for the Modes dropdown - always has at least
    /// EventRouter::kGlobalMode as its first entry (see removeMode()).
    QStringList m_modes;
    QString m_currentMode;

    /// -1 until setCurrentDeviceForCurves()/setCurveEditorTarget() first
    /// records a target row.
    int m_curvesTargetDeviceRow = -1;

    /// "" until setCurveEditorTarget() first records a target axis name (or
    /// after a plain setCurrentDeviceForCurves() call, which clears it back
    /// to "" - see that method's own docs).
    QString m_curvesTargetInputName;

    QList<DeviceEntry> m_devices;

    /// Fase SC-7.7: single-slot app-wide Copy/Paste clipboard - m_clipboardJson
    /// is whatever getActionDataJson() returned for the most recently
    /// copyAction()-ed input (empty if nothing's been copied yet this
    /// session), m_clipboardKind is that input's "axis"/"button" kind so
    /// pasteAction()/hasCopiedAction() can refuse a kind-mismatched paste.
    QString m_clipboardJson;
    QString m_clipboardKind;
};
