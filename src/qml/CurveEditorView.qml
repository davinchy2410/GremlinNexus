import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GremblingNexus

// "Curves" screen (Phase 10 Part 3): a two-column response-curve editor.
// Left is a Glass properties panel (Deadzone/Sensitivity sliders); right is
// an interactive Canvas + draggable CurveHandle points describing a
// multi-point spline. All state lives in curveEditorViewModel - this view
// is a thin, fully reactive projection of it.
//
// Fase (per-profile Curve wiring): Device/Axis selection now actually loads
// and saves the real CurveHandler bound to that axis (previously "visual
// only" - see git history), reusing the exact same
// profileEditorViewModel.getActionDataJson()/bindAction() pair
// ActionPickerPopup.qml already uses for every other action type - so
// curves round-trip through a profile file precisely like any other
// binding (a CurveHandler's own "parameters.curvePoints" already lived
// inside "bindings", it just had no UI writing to it until now).
// Deliberately does NOT expose its own vJoy target-device/axis picker or
// invert toggle - ActionPickerPopup's "vJoy Remap" tab already owns
// creating the binding and setting its routing/invert; this screen is
// purely the precision-shape editor for whatever CurveHandler already
// exists on the selected physical axis, mirroring the "advanced editor
// for the same binding" split the Architect and I agreed on.
Item {
    id: root

    /// Whichever device+axis loadCurveForSelection() last resolved a real,
    /// editable CurveHandler for - "" until a selection with one is made.
    property string currentDevicePath: ""
    property string currentInputName: ""

    /// True once loadCurveForSelection() found an actual CurveHandler (bare
    /// or SmoothingHandler-wrapped) bound to the current selection - gates
    /// the Save button so editing an axis with no vJoy Remap yet (nothing
    /// to save into) can't silently invent a routing-less binding.
    property bool hasEditableCurve: false

    /// The live CurveHandler action object (parsed JSON) currently loaded -
    /// mutated in place by saveCurveToProfile() so its own
    /// targetDeviceType/targetOutputId/targetAxis/invert (all owned by
    /// ActionPickerPopup, never edited here) round-trip untouched.
    property var existingCurveAction: null

    /// The outer SmoothingHandler wrapper object, if the loaded binding was
    /// wrapped (see ActionPickerPopup's own "Smoothing / Anti-Jitter"
    /// toggle) - null when the CurveHandler was top-level. Its own
    /// "parameters.smoothingFactor" (a *different* smoothing knob than
    /// CurveHandler's own inline EMA slider below) is preserved untouched.
    property var wrapperActionData: null

    /// Re-reads whatever's actually bound to the selected device+axis
    /// (mode: profileEditorViewModel.currentMode) and feeds it into
    /// curveEditorViewModel - called on selection/mode change, never on a
    /// timer, so an in-progress edit is never clobbered by anything but an
    /// explicit re-selection.
    function loadCurveForSelection() {
        const devicePath = profileEditorViewModel.systemPathForDevice(deviceCombo.currentIndex);
        const inputName = axisCombo.currentText;
        root.currentDevicePath = devicePath;
        root.currentInputName = inputName;
        root.existingCurveAction = null;
        root.wrapperActionData = null;

        if (!devicePath || !inputName) {
            root.hasEditableCurve = false;
            return;
        }

        let curveAction = null;
        const jsonStr = profileEditorViewModel.getActionDataJson(devicePath, inputName, profileEditorViewModel.currentMode);
        if (jsonStr !== "") {
            try {
                const actionData = JSON.parse(jsonStr);
                if (actionData.actionType === "CurveHandler") {
                    curveAction = actionData;
                } else if (actionData.actionType === "SmoothingHandler" && actionData.wrappedAction
                        && actionData.wrappedAction.actionType === "CurveHandler") {
                    curveAction = actionData.wrappedAction;
                    root.wrapperActionData = actionData;
                }
                if (curveAction) {
                    // Sprint QoL Part 2: read from actionData (the OUTERMOST
                    // object - either the same as curveAction, or the
                    // SmoothingHandler wrapper), matching exactly where
                    // saveCurveToProfile() below writes it back onto
                    // jsonToSave.
                    curveNoteField.text = (actionData.parameters && actionData.parameters.note) || "";
                }
            } catch (e) {
                console.warn("CurveEditorView: failed to parse existing binding JSON:", e);
            }
        }

        if (!curveAction) {
            // Nothing bound here yet (or it's some other actionType, e.g.
            // MergeAxisHandler/SplitAxisHandler on this same axis) - reset
            // to a neutral default so the canvas doesn't keep showing
            // whatever the previously-selected axis had, but leave Save
            // disabled since there's no CurveHandler to write into.
            root.hasEditableCurve = false;
            curveEditorViewModel.setPoints([{x: -1.0, y: -1.0}, {x: 1.0, y: 1.0}]);
            curveEditorViewModel.deadzone = 0.05;
            curveEditorViewModel.sensitivity = 1.0;
            curveEditorViewModel.smoothingFactor = 0.0;
            curveNoteField.text = "";
            return;
        }

        root.hasEditableCurve = true;
        root.existingCurveAction = curveAction;
        const params = curveAction.parameters || {};
        const pts = (Array.isArray(params.curvePoints) && params.curvePoints.length >= 2)
            ? params.curvePoints : [{x: -1.0, y: -1.0}, {x: 1.0, y: 1.0}];
        curveEditorViewModel.setPoints(pts);
        curveEditorViewModel.deadzone = params.deadzone !== undefined ? params.deadzone : 0.05;
        curveEditorViewModel.sensitivity = params.sensitivity !== undefined ? params.sensitivity : 1.0;
        curveEditorViewModel.smoothingFactor = params.smoothingFactor !== undefined ? params.smoothingFactor : 0.0;
    }

    /// Writes curveEditorViewModel's current deadzone/sensitivity/
    /// smoothingFactor/points back into existingCurveAction's "parameters"
    /// (everything else on that action object - target*/invert - stays
    /// exactly as loaded) and calls bindAction(), the same call
    /// ActionPickerPopup.qml's own Apply button makes.
    function saveCurveToProfile() {
        if (!root.hasEditableCurve || !root.existingCurveAction) {
            return;
        }
        const action = root.existingCurveAction;
        action.parameters = action.parameters || {};
        action.parameters.deadzone = curveEditorViewModel.deadzone;
        action.parameters.sensitivity = curveEditorViewModel.sensitivity;
        action.parameters.smoothingFactor = curveEditorViewModel.smoothingFactor;
        action.parameters.curvePoints = curveEditorViewModel.points();

        const jsonToSave = root.wrapperActionData ? root.wrapperActionData : action;

        // Sprint QoL Part 2: same "only touch parameters.note when there's
        // something to store, remove a stale one otherwise" convention as
        // ActionPickerPopup.qml's own Apply handler - written onto
        // jsonToSave (the OUTERMOST object actually passed to bindAction()
        // below), not always `action` itself, since a SmoothingHandler-
        // wrapped curve saves its own wrapper object instead (see
        // wrapperActionData's own docs above).
        const noteText = curveNoteField.text.trim();
        jsonToSave.parameters = jsonToSave.parameters || {};
        if (noteText.length > 0) {
            jsonToSave.parameters.note = noteText;
        } else {
            delete jsonToSave.parameters.note;
        }

        profileEditorViewModel.bindAction(root.currentDevicePath, root.currentInputName,
            profileEditorViewModel.currentMode, JSON.stringify(jsonToSave));
    }

    Component.onCompleted: loadCurveForSelection()

    /// Sprint Final (Curves deep-linking): selects whichever axis name
    /// matches profileEditorViewModel.curvesTargetInputName inside
    /// axisCombo's CURRENT model (that device's own axis list) - called
    /// deferred, one event-loop tick after deviceCombo.currentIndex was set
    /// (see onCurvesTargetDeviceRowChanged below), because axisCombo's model
    /// (bound to profileEditorViewModel.axisNamesForDevice(deviceCombo.
    /// currentIndex)) hasn't recomputed for the new device yet at the exact
    /// instant deviceCombo.currentIndex changes - the same hazard
    /// deviceCombo's own onCurrentIndexChanged below already works around
    /// with Qt.callLater. A no-op if curvesTargetInputName is empty (a plain
    /// setCurrentDeviceForCurves() device-only target, or InputRow wasn't
    /// the caller) or isn't found among this device's own axes (e.g. stale
    /// after a device's axis count changed) - either way, whatever axisCombo
    /// already has selected is left alone rather than forced to something
    /// invalid.
    function applyTargetInputName() {
        const name = profileEditorViewModel.curvesTargetInputName;
        if (!name) {
            return;
        }
        const idx = axisCombo.find(name);
        if (idx >= 0) {
            axisCombo.currentIndex = idx;
        }
    }

    Connections {
        target: profileEditorViewModel
        function onCurrentModeChanged() { loadCurveForSelection(); }
        // Fase (Curves nav rework), extended Sprint Final: DeviceCard's old
        // device-wide "Curves" button called setCurrentDeviceForCurves();
        // InputRow's new per-axis curve icon calls setCurveEditorTarget()
        // instead, which sets curvesTargetDeviceRow AND curvesTargetInputName
        // together in the same C++ call before emitting either signal - so by
        // the time this handler runs, curvesTargetInputName already holds
        // its final value too, and a single Connections handler is enough
        // (no separate onCurvesTargetInputNameChanged needed). Jumping
        // deviceCombo to that row triggers its own onCurrentIndexChanged
        // (Qt.callLater(loadCurveForSelection)) below; applyTargetInputName()
        // above is deferred the same way, for the same reason, so it reads
        // axisCombo's model only after it has actually settled on the new
        // device's own axis list.
        function onCurvesTargetDeviceRowChanged() {
            const row = profileEditorViewModel.curvesTargetDeviceRow;
            if (row >= 0) {
                deviceCombo.currentIndex = row;
            }
            Qt.callLater(root.applyTargetInputName);
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.base
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg

        // --- Routing header: which device/axis this curve applies to ----
        // Fully wired (see "Fase (per-profile Curve wiring)" at the top of
        // this file): selecting a device/axis here loads the real
        // CurveHandler already bound to it, and "Save to Profile" writes
        // straight back into that same live binding via bindAction() - this
        // is NOT a visual-only mockup.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            radius: Theme.radiusMedium
            color: Theme.surface0
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.05)

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: Theme.spacingLg

                ColumnLayout {
                    spacing: 2
                    Text { text: qsTr("Device"); color: Theme.subtext0; font.pixelSize: 11 }
                    AppComboBox {
                        id: deviceCombo
                        Layout.preferredWidth: 220
                        model: profileEditorViewModel
                        textRole: "deviceName"
                        // Qt.callLater: axisCombo's own model (below) is
                        // itself bound to deviceCombo.currentIndex, so its
                        // list of axis names hasn't been recomputed yet at
                        // the instant this fires - deferring one tick lets
                        // axisCombo settle on the new device's own axis 0
                        // first, so loadCurveForSelection() reads the right
                        // (device, axis) pair instead of the previous
                        // device's stale axis name.
                        onCurrentIndexChanged: Qt.callLater(root.loadCurveForSelection)
                    }
                }

                ColumnLayout {
                    spacing: 2
                    Text { text: qsTr("Axis"); color: Theme.subtext0; font.pixelSize: 11 }
                    AppComboBox {
                        id: axisCombo
                        Layout.preferredWidth: 180
                        model: profileEditorViewModel.axisNamesForDevice(deviceCombo.currentIndex)
                        onCurrentIndexChanged: Qt.callLater(root.loadCurveForSelection)
                    }
                }

                Item { Layout.fillWidth: true }

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.maximumWidth: 220
                    visible: !root.hasEditableCurve
                    text: qsTr("No vJoy Remap on this axis - assign one from Profiles first")
                    color: Theme.overlay0
                    font.pixelSize: 11
                    font.italic: true
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignRight
                }
                ToolButton {
                    Layout.alignment: Qt.AlignVCenter
                    label: qsTr("Save to Profile")
                    enabled: root.hasEditableCurve
                    opacity: enabled ? 1.0 : 0.5
                    onClicked: root.saveCurveToProfile()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingLg

            // --- Left: properties panel ---------------------------------
            Rectangle {
                Layout.preferredWidth: 260
                Layout.fillHeight: true
                radius: Theme.radiusMedium
                color: Theme.surface0
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.05)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    spacing: Theme.spacingLg

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: qsTr("Curve Editor"); color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
                        Text {
                            text: qsTr("Shape the axis response curve")
                            color: Theme.subtext0
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    // Sprint QoL Part 2: free-text, independent of the curve
                    // shape itself - saveCurveToProfile()/loadCurveForSelection()
                    // above read/write it as jsonToSave.parameters.note, the
                    // same "parameters.note" convention ActionPickerPopup.qml's
                    // own note field uses.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        TextField {
                            id: curveNoteField
                            Layout.fillWidth: true
                            implicitHeight: 30
                            enabled: root.hasEditableCurve
                            opacity: enabled ? 1.0 : 0.5
                            color: Theme.subtext0
                            font.pixelSize: 12
                            font.italic: true
                            placeholderText: qsTr("Note / Description (Optional)")
                            background: Rectangle {
                                color: Theme.surface1
                                radius: Theme.radiusSmall
                                border.width: 1
                                border.color: Qt.rgba(1, 1, 1, 0.06)
                            }
                        }
                    }

                    ValueSlider {
                        Layout.fillWidth: true
                        label: qsTr("Deadzone")
                        value: curveEditorViewModel.deadzone
                        from: 0.0
                        to: 0.9
                        onMoved: (v) => curveEditorViewModel.deadzone = v
                    }

                    ValueSlider {
                        Layout.fillWidth: true
                        label: qsTr("Sensitivity")
                        value: curveEditorViewModel.sensitivity
                        from: 0.1
                        to: 3.0
                        onMoved: (v) => curveEditorViewModel.sensitivity = v
                    }

                    ValueSlider {
                        Layout.fillWidth: true
                        label: qsTr("Anti-Noise Filter (EMA)")
                        value: curveEditorViewModel.smoothingFactor
                        from: 0.0
                        to: 0.99
                        onMoved: (v) => curveEditorViewModel.smoothingFactor = v
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingXs
                        height: 1
                        color: Qt.rgba(1, 1, 1, 0.1)
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Text { text: qsTr("Quick Tools"); color: Theme.subtext0; font.pixelSize: 12; font.weight: Font.DemiBold }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSm

                            ToolButton {
                                Layout.fillWidth: true
                                label: qsTr("Linear")
                                onClicked: curveEditorViewModel.applyPresetLinear()
                            }
                            ToolButton {
                                Layout.fillWidth: true
                                label: qsTr("S-Curve")
                                onClicked: curveEditorViewModel.applyPresetSCurve()
                            }
                        }

                        ToolButton {
                            Layout.fillWidth: true
                            label: qsTr("Invert")
                            onClicked: curveEditorViewModel.invertCurve()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingXs
                        height: 1
                        color: Qt.rgba(1, 1, 1, 0.1)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Text {
                            text: qsTr("Diagonal Symmetry")
                            color: Theme.text
                            font.pixelSize: 13
                            Layout.fillWidth: true
                        }
                        ToggleSwitch {
                            checked: curveEditorViewModel.diagonalSymmetry
                            onToggled: (v) => curveEditorViewModel.diagonalSymmetry = v
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Text { text: qsTr("Control Point"); color: Theme.subtext0; font.pixelSize: 12; font.weight: Font.DemiBold }

                        Text {
                            visible: curveEditorViewModel.selectedIndex < 0
                            text: qsTr("Click a point to edit its coordinates")
                            color: Theme.overlay0
                            font.pixelSize: 11
                            font.italic: true
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: curveEditorViewModel.selectedIndex >= 0
                            spacing: Theme.spacingSm

                            PointCoordinateField { Layout.fillWidth: true; label: qsTr("X"); axis: "x" }
                            PointCoordinateField { Layout.fillWidth: true; label: qsTr("Y"); axis: "y" }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // --- Right: interactive curve canvas ------------------------
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Rectangle {
                    id: graphBackground
                    anchors.fill: parent
                    radius: Theme.radiusMedium
                    color: Theme.surface0
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.05)
                }

                Item {
                    id: graphInner
                    anchors.fill: graphBackground
                    anchors.margins: Theme.spacingLg

                    Canvas {
                        id: curveCanvas
                        anchors.fill: parent
                        antialiasing: true

                        // Maps a bipolar [-1, 1] axis coordinate to a pixel
                        // position - x=-1/y=-1 sits at the bottom-left corner,
                        // x=0/y=0 (the physical joystick's resting position)
                        // sits dead-center, x=1/y=1 at the top-right.
                        function mapX(x) { return (x + 1.0) * 0.5 * width; }
                        function mapY(y) { return height - (y + 1.0) * 0.5 * height; }

                        // Deadzone, applied exactly like CurveHandler::processAxis
                        // (C++): collapses anything inside it to exactly 0.0 and
                        // rescales the region outside it back out to fill [-1, 1].
                        function deadzoneAdjust(x, deadzone) {
                            const sign = x < 0.0 ? -1.0 : 1.0;
                            const magnitude = Math.abs(x);
                            return magnitude > deadzone ? sign * (magnitude - deadzone) / (1.0 - deadzone) : 0.0;
                        }

                        // Evaluates the monotone cubic Hermite spline (Fritsch-
                        // Carlson tangents) through pts at an arbitrary x - the
                        // same math CurveHandler bakes into its LUT in C++
                        // (buildSplineLut/evaluateCurve), re-derived here per call
                        // instead of baked into a LUT since the canvas only needs
                        // a few hundred samples per repaint. Flat extrapolation
                        // outside pts' own X range, same as the C++ side.
                        function evaluateSpline(targetX, pts, tangents) {
                            const n = pts.length;
                            if (targetX <= pts[0].x) {
                                return pts[0].y;
                            }
                            if (targetX >= pts[n - 1].x) {
                                return pts[n - 1].y;
                            }
                            let segment = 0;
                            while (segment + 1 < n - 1 && pts[segment + 1].x < targetX) {
                                segment++;
                            }
                            const x0 = pts[segment].x;
                            const x1 = pts[segment + 1].x;
                            const y0 = pts[segment].y;
                            const y1 = pts[segment + 1].y;
                            const h = x1 - x0;
                            const t = h > 1e-9 ? (targetX - x0) / h : 0.0;
                            const t2 = t * t;
                            const t3 = t2 * t;
                            const h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
                            const h10 = t3 - 2.0 * t2 + t;
                            const h01 = -2.0 * t3 + 3.0 * t2;
                            const h11 = t3 - t2;
                            return h00 * y0 + h10 * h * tangents[segment] + h01 * y1 + h11 * h * tangents[segment + 1];
                        }

                        onPaint: {
                            const ctx = getContext("2d");
                            ctx.reset();
                            const w = width;
                            const h = height;
                            const centerX = mapX(0.0);
                            const centerY = mapY(0.0);

                            // Subtle background grid, 4 divisions per side so
                            // gridlines land on quarter-scale (0.5, 0) marks
                            // either side of the center.
                            ctx.strokeStyle = Theme.surface1;
                            ctx.lineWidth = 1;
                            const divisions = 8;
                            for (let i = 0; i <= divisions; i++) {
                                const gx = (w / divisions) * i;
                                const gy = (h / divisions) * i;
                                ctx.beginPath();
                                ctx.moveTo(gx, 0);
                                ctx.lineTo(gx, h);
                                ctx.stroke();
                                ctx.beginPath();
                                ctx.moveTo(0, gy);
                                ctx.lineTo(w, gy);
                                ctx.stroke();
                            }

                            // Bold center cross marking the X=0/Y=0 axes - the
                            // physical joystick's resting position - so the 4
                            // quadrants of the bipolar range read clearly.
                            ctx.strokeStyle = Theme.subtext0;
                            ctx.lineWidth = 1.5;
                            ctx.beginPath();
                            ctx.moveTo(centerX, 0);
                            ctx.lineTo(centerX, h);
                            ctx.stroke();
                            ctx.beginPath();
                            ctx.moveTo(0, centerY);
                            ctx.lineTo(w, centerY);
                            ctx.stroke();

                            // Neutral 1:1 linear reference, corner to corner.
                            ctx.strokeStyle = Theme.overlay0;
                            ctx.lineWidth = 1;
                            ctx.setLineDash([4, 4]);
                            ctx.beginPath();
                            ctx.moveTo(mapX(-1.0), mapY(-1.0));
                            ctx.lineTo(mapX(1.0), mapY(1.0));
                            ctx.stroke();
                            ctx.setLineDash([]);

                            // The actual multi-point response curve. Sampled at
                            // fixed X steps across the raw input's own [-1, 1]
                            // domain and pushed through the same deadzone math
                            // as CurveHandler::processAxis() before evaluating
                            // the spline - this is what makes the drawn line
                            // sit perfectly flat at Y=0 wherever the deadzone
                            // slider says the raw input should be silenced,
                            // instead of just showing the raw control-point
                            // shape with no deadzone applied at all.
                            const pts = curveEditorViewModel.points();
                            const deadzone = curveEditorViewModel.deadzone;
                            const n = pts.length;
                            const secants = [];
                            for (let i = 0; i < n - 1; i++) {
                                const dx = pts[i + 1].x - pts[i].x;
                                secants.push(dx > 1e-9 ? (pts[i + 1].y - pts[i].y) / dx : 0.0);
                            }

                            const tangents = new Array(n);
                            tangents[0] = secants[0];
                            tangents[n - 1] = secants[n - 2];
                            for (let i = 1; i < n - 1; i++) {
                                tangents[i] = (secants[i - 1] * secants[i] <= 0.0)
                                    ? 0.0
                                    : (secants[i - 1] + secants[i]) / 2.0;
                            }
                            for (let i = 0; i < n - 1; i++) {
                                if (secants[i] === 0.0) {
                                    tangents[i] = 0.0;
                                    tangents[i + 1] = 0.0;
                                    continue;
                                }
                                const alpha = tangents[i] / secants[i];
                                const beta = tangents[i + 1] / secants[i];
                                const magnitude = Math.sqrt(alpha * alpha + beta * beta);
                                if (magnitude > 3.0) {
                                    const tau = 3.0 / magnitude;
                                    tangents[i] = tau * alpha * secants[i];
                                    tangents[i + 1] = tau * beta * secants[i];
                                }
                            }

                            ctx.strokeStyle = Theme.accent;
                            ctx.lineWidth = 2.5;
                            ctx.lineJoin = "round";
                            ctx.lineCap = "round";
                            ctx.beginPath();
                            const steps = 200;
                            for (let s = 0; s <= steps; s++) {
                                const x = -1.0 + 2.0 * s / steps;
                                const y = evaluateSpline(deadzoneAdjust(x, deadzone), pts, tangents);
                                const px = mapX(x);
                                const py = mapY(y);
                                if (s === 0) {
                                    ctx.moveTo(px, py);
                                } else {
                                    ctx.lineTo(px, py);
                                }
                            }
                            ctx.stroke();
                        }

                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()

                        // pointsChanged covers drag/add/remove/preset/invert;
                        // deadzoneChanged is needed separately since moving
                        // the Deadzone slider mutates curveEditorViewModel's
                        // own deadzone property directly (ValueSlider's
                        // onMoved), never touching m_points at all - without
                        // this, the slider silently changes the deadzone
                        // used by processAxis()/onPaint's own sampling loop
                        // below, but the canvas never redraws to show it.
                        Connections {
                            target: curveEditorViewModel
                            function onPointsChanged() { curveCanvas.requestPaint(); }
                            function onDeadzoneChanged() { curveCanvas.requestPaint(); }
                        }
                    }

                    // Double-click empty canvas space to add a point there.
                    // Sits below the CurveHandle Repeater in paint/input
                    // order (declared first), so a double-click directly on
                    // an existing handle is consumed by that handle's own
                    // MouseArea (delete) instead of reaching this one.
                    MouseArea {
                        id: addPointArea
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        onDoubleClicked: (mouse) => {
                            if (curveCanvas.width <= 0 || curveCanvas.height <= 0) {
                                return;
                            }
                            const nx = (mouse.x / curveCanvas.width) * 2.0 - 1.0;
                            const ny = (1.0 - mouse.y / curveCanvas.height) * 2.0 - 1.0;

                            // Same reverse-deadzone math as CurveHandle's own
                            // drag handler: nx is the raw stick position the
                            // user clicked, but addPoint()/m_points store the
                            // post-deadzone spline-domain x, so a click inside
                            // the deadzone's flat region becomes exactly 0
                            // instead of a point CurveHandler could never
                            // actually reach.
                            const dz = curveEditorViewModel.deadzone;
                            let adjX = 0.0;
                            if (nx > dz) {
                                adjX = (nx - dz) / (1.0 - dz);
                            } else if (nx < -dz) {
                                adjX = (nx + dz) / (1.0 - dz);
                            }

                            curveEditorViewModel.addPoint(adjX, ny);
                        }
                    }

                    // curveEditorViewModel is itself the points model (see
                    // its class docs for why: a real QAbstractListModel
                    // with targeted dataChanged() keeps these delegates -
                    // and any in-progress mouse grab on one of them - alive
                    // across a drag, instead of a plain QVariantList
                    // getting Repeater to destroy/recreate every delegate
                    // on every single drag-move event.
                    Repeater {
                        model: curveEditorViewModel
                        delegate: CurveHandle {
                            pointIndex: index
                            pointX: model.pointX
                            pointY: model.pointY
                            graphWidth: curveCanvas.width
                            graphHeight: curveCanvas.height
                            selected: curveEditorViewModel.selectedIndex === index
                        }
                    }
                }
            }
        }
    }
}
