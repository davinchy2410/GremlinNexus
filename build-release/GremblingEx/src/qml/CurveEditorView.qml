import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GremblingEx

// "Curves" screen (Phase 10 Part 3): a two-column response-curve editor.
// Left is a Glass properties panel (Deadzone/Sensitivity sliders); right is
// an interactive Canvas + draggable CurveHandle points describing a
// multi-point spline. All state lives in curveEditorViewModel - this view
// is a thin, fully reactive projection of it.
Item {
    id: root

    Rectangle {
        anchors.fill: parent
        color: Theme.base
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg

        // --- Routing header: which device/axis this curve applies to ----
        // Visual-only for now (Phase 10 Part 6) - selecting here doesn't
        // yet reconfigure the real CurveHandler bound to that axis; the
        // point is to resolve the flow ("what am I editing?") before
        // wiring the actual routing.
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
                    Text { text: "Device"; color: Theme.subtext0; font.pixelSize: 11 }
                    AppComboBox {
                        id: deviceCombo
                        Layout.preferredWidth: 220
                        model: profileEditorViewModel
                        textRole: "deviceName"
                    }
                }

                ColumnLayout {
                    spacing: 2
                    Text { text: "Axis"; color: Theme.subtext0; font.pixelSize: 11 }
                    AppComboBox {
                        id: axisCombo
                        Layout.preferredWidth: 180
                        model: profileEditorViewModel.axisNamesForDevice(deviceCombo.currentIndex)
                    }
                }

                Item { Layout.fillWidth: true }
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
                        Text { text: "Curve Editor"; color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
                        Text {
                            text: "Shape the axis response curve"
                            color: Theme.subtext0
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    ValueSlider {
                        Layout.fillWidth: true
                        label: "Deadzone"
                        value: curveEditorViewModel.deadzone
                        from: 0.0
                        to: 0.9
                        onMoved: (v) => curveEditorViewModel.deadzone = v
                    }

                    ValueSlider {
                        Layout.fillWidth: true
                        label: "Sensitivity"
                        value: curveEditorViewModel.sensitivity
                        from: 0.1
                        to: 3.0
                        onMoved: (v) => curveEditorViewModel.sensitivity = v
                    }

                    ValueSlider {
                        Layout.fillWidth: true
                        label: "Anti-Noise Filter (EMA)"
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

                        Text { text: "Quick Tools"; color: Theme.subtext0; font.pixelSize: 12; font.weight: Font.DemiBold }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSm

                            ToolButton {
                                Layout.fillWidth: true
                                label: "Linear"
                                onClicked: curveEditorViewModel.applyPresetLinear()
                            }
                            ToolButton {
                                Layout.fillWidth: true
                                label: "S-Curve"
                                onClicked: curveEditorViewModel.applyPresetSCurve()
                            }
                        }

                        ToolButton {
                            Layout.fillWidth: true
                            label: "Invert"
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
                            text: "Diagonal Symmetry"
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

                        Text { text: "Control Point"; color: Theme.subtext0; font.pixelSize: 12; font.weight: Font.DemiBold }

                        Text {
                            visible: curveEditorViewModel.selectedIndex < 0
                            text: "Click a point to edit its coordinates"
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

                            PointCoordinateField { Layout.fillWidth: true; label: "X"; axis: "x" }
                            PointCoordinateField { Layout.fillWidth: true; label: "Y"; axis: "y" }
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

                        onPaint: {
                            const ctx = getContext("2d");
                            ctx.reset();
                            const w = width;
                            const h = height;

                            // Subtle background grid.
                            ctx.strokeStyle = Theme.surface1;
                            ctx.lineWidth = 1;
                            const divisions = 10;
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

                            // Neutral 1:1 linear reference.
                            ctx.strokeStyle = Theme.overlay0;
                            ctx.lineWidth = 1;
                            ctx.setLineDash([4, 4]);
                            ctx.beginPath();
                            ctx.moveTo(0, h);
                            ctx.lineTo(w, 0);
                            ctx.stroke();
                            ctx.setLineDash([]);

                            // The actual multi-point response curve, drawn as a
                            // monotone cubic Hermite spline (Fritsch-Carlson
                            // tangents) - the exact same math CurveHandler bakes
                            // into its LUT in C++ (buildSplineLut). Each segment
                            // is converted to its algebraically equivalent cubic
                            // Bezier (P0, P0+m0/3, P1-m1/3, P1 - a standard
                            // Hermite-to-Bezier identity, not an approximation)
                            // so Canvas draws one smooth, rounded stroke through
                            // every control point instead of a lineTo() zigzag.
                            const pts = curveEditorViewModel.points();
                            if (pts.length === 2) {
                                ctx.strokeStyle = Theme.accent;
                                ctx.lineWidth = 2.5;
                                ctx.lineCap = "round";
                                ctx.beginPath();
                                ctx.moveTo(pts[0].x * w, h - pts[0].y * h);
                                ctx.lineTo(pts[1].x * w, h - pts[1].y * h);
                                ctx.stroke();
                            } else if (pts.length > 2) {
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
                                ctx.moveTo(pts[0].x * w, h - pts[0].y * h);
                                for (let i = 0; i < n - 1; i++) {
                                    const segDx = pts[i + 1].x - pts[i].x;
                                    const cp1x = pts[i].x + segDx / 3.0;
                                    const cp1y = pts[i].y + (segDx * tangents[i]) / 3.0;
                                    const cp2x = pts[i + 1].x - segDx / 3.0;
                                    const cp2y = pts[i + 1].y - (segDx * tangents[i + 1]) / 3.0;

                                    ctx.bezierCurveTo(
                                        cp1x * w, h - cp1y * h,
                                        cp2x * w, h - cp2y * h,
                                        pts[i + 1].x * w, h - pts[i + 1].y * h);
                                }
                                ctx.stroke();
                            }
                        }

                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()

                        Connections {
                            target: curveEditorViewModel
                            function onPointsChanged() { curveCanvas.requestPaint(); }
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
                            const nx = mouse.x / curveCanvas.width;
                            const ny = 1.0 - mouse.y / curveCanvas.height;
                            curveEditorViewModel.addPoint(nx, ny);
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
