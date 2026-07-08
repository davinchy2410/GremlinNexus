#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QPointF>
#include <QVariantList>
#include <QVariantMap>

/**
 * @brief ViewModel for the "Curves" screen (Phase 10 Part 3).
 *
 * Exposes the scalar shaping parameters (deadzone/sensitivity/
 * smoothingFactor) that CurveHandler already understands, plus an ordered
 * list of bipolar [-1,1]x[-1,1] control points describing a multi-point
 * response spline, with the origin (0,0) at the physical joystick's resting
 * position. This class itself only holds the *draft* being edited -
 * it has no notion of which physical device/axis it's for, or of
 * ProfileManager/EventRouter at all. The actual per-binding read/write
 * happens one layer up, in CurveEditorView.qml's loadCurveForSelection()/
 * saveCurveToProfile() (see that file's own "Fase (per-profile Curve
 * wiring)" comment): those reuse the exact same
 * profileEditorViewModel.getActionDataJson()/bindAction() pair every other
 * action type's editor already uses, so a value changed here and saved
 * really does replace the live CurveHandler bound to that axis in
 * EventRouter's routing table - confirmed round-tripping correctly (deadzone
 * survives a save + reselect-the-axis reload) as of Sprint 3's audit. This
 * class does NOT go stale/disconnected on its own; if a future change here
 * ever needs to, update this comment alongside it.
 *
 * Points are a real QAbstractListModel (roles "pointX"/"pointY"), NOT a
 * QVariantList replaced wholesale on every change - that was Part 3-5's
 * original design, and it turned out to be a real bug (Part 6): QML's
 * Repeater destroys and recreates every delegate whenever the *object
 * identity* of its `model` changes, which a fresh QVariantList does on
 * every single updatePoint() call. During a real click-and-hold drag, that
 * meant the CurveHandle (and its MouseArea, mid-mouse-grab) being dragged
 * was destroyed out from under the user's own held-down mouse button after
 * the very first move event - dragging looked fine in scripted/synthetic
 * test input (which doesn't hold a real OS-level mouse grab the same way)
 * but was actually broken for a real user. Emitting targeted dataChanged()
 * for the touched row(s) instead keeps delegate instances - and the mouse
 * grab - alive across a drag. points() is kept as a plain invokable
 * snapshot (same {"x":..,"y":..} shape as before) purely for the Canvas's
 * onPaint(), which always wants the whole list at once to draw the spline.
 */
class CurveEditorViewModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(double deadzone READ deadzone WRITE setDeadzone NOTIFY deadzoneChanged)
    Q_PROPERTY(double sensitivity READ sensitivity WRITE setSensitivity NOTIFY sensitivityChanged)
    Q_PROPERTY(double smoothingFactor READ smoothingFactor WRITE setSmoothingFactor NOTIFY smoothingFactorChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY selectedIndexChanged)
    Q_PROPERTY(bool diagonalSymmetry READ diagonalSymmetry WRITE setDiagonalSymmetry NOTIFY diagonalSymmetryChanged)

public:
    enum Role
    {
        PointXRole = Qt::UserRole + 1,
        PointYRole,
    };

    explicit CurveEditorViewModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    double deadzone() const;
    void setDeadzone(double value);

    double sensitivity() const;
    void setSensitivity(double value);

    double smoothingFactor() const;
    void setSmoothingFactor(double value);

    /// Snapshot of every point as a plain QVariantList of {"x":.., "y":..}
    /// maps - for the Canvas's onPaint(), which needs the whole curve at
    /// once to draw it. Not a Q_PROPERTY (see class docs): call it
    /// explicitly as points() from QML, and re-call it on the pointsChanged
    /// signal rather than relying on property-binding re-evaluation.
    Q_INVOKABLE QVariantList points() const;

    /// Wholesale-replaces every point - used by CurveEditorView.qml's
    /// loadCurveForSelection() to pull an existing CurveHandler binding's
    /// "parameters.curvePoints" in for editing (Fase: per-profile Curve
    /// wiring - see that class' own file-level comment). Falls back to a
    /// neutral 2-point linear curve if points has fewer than 2 entries -
    /// CurveHandler always needs a first/last point to anchor [-1,1] (see
    /// removePoint()'s own docs on why the endpoints are never removable).
    Q_INVOKABLE void setPoints(const QVariantList &points);

    /// Index into the model of the control point currently selected for
    /// precision editing (see the "Control Point" X/Y fields in
    /// CurveEditorView.qml) - -1 means nothing selected. Out-of-range
    /// writes are coerced to -1 rather than rejected, since the common
    /// case that produces one (a preset shrinking the point count out from
    /// under a stale selection) should degrade to "nothing selected", not
    /// silently keep the old index.
    int selectedIndex() const;
    void setSelectedIndex(int index);

    /// When true, updatePoint() also moves the point's reflection through
    /// the bipolar origin (0, 0) to (-x, -y) - lets the user shape one half
    /// of a symmetric curve (e.g. an S-Curve) and get the other half for
    /// free.
    bool diagonalSymmetry() const;
    void setDiagonalSymmetry(bool value);

    /// Called from QML while a control point is being dragged. Coordinates
    /// are bipolar [-1,1]x[-1,1] canvas-space values, already computed by
    /// the view from raw pixel positions. Clamped here (not trusted from
    /// QML) to [-1,1] on y, and - for the interior points - to stay
    /// strictly between its immediate neighbours on x so the spline can
    /// never fold back on itself. The first and last points are the
    /// curve's fixed endpoints and always keep x = -1 / x = 1 respectively;
    /// only their y (output) is draggable. If diagonalSymmetry is on, the
    /// mirrored point (at size()-1-index) is updated too, target (-x, -y)
    /// through the bipolar origin, through the same per-point clamping
    /// rules. Emits dataChanged() only for the row(s) actually touched (see
    /// class docs).
    Q_INVOKABLE void updatePoint(int index, double x, double y);

    /// Inserts a new point at (x, y) - both clamped to [-1,1] - keeping
    /// m_points sorted by x. Silently does nothing if there isn't at least
    /// kMinGap of room between the two existing points it would land
    /// between (e.g. a double-click too close to an existing point) rather
    /// than producing a degenerate zero-width segment. On success, the new
    /// point becomes the selection, so the precision X/Y fields immediately
    /// reflect what was just added. If diagonalSymmetry is on and (x,y)
    /// isn't already dead-center, the reflection (-x,-y) is inserted too
    /// (all-or-nothing - if either half doesn't fit, neither is added) so
    /// the array's parity always matches what updatePoint()'s own by-index
    /// mirroring (at size()-1-index) expects; leaving a point unpaired here
    /// is what let a later drag's mirror silently land on the wrong point
    /// and break the sorted-by-x invariant the spline evaluation relies on.
    Q_INVOKABLE void addPoint(double x, double y);

    /// Removes the point at index, except the two curve endpoints (index 0
    /// and size()-1), which are never removable - out-of-range or endpoint
    /// indices are silently ignored. If diagonalSymmetry is on and index's
    /// own reflection (size()-1-index) is itself a removable, distinct
    /// point, both are removed together (same pairing addPoint() maintains)
    /// rather than leaving the other half stranded.
    Q_INVOKABLE void removePoint(int index);

    /// Presets and quick tools (Phase 10 Part 4): applyPresetLinear/SCurve
    /// reset the model wholesale (their point *count* changes, so there's
    /// nothing an incremental update could preserve anyway) and clear the
    /// selection, since the point the user had selected may no longer
    /// exist at that index afterward. invertCurve() keeps the same points
    /// and only flips Y, so it uses a single ranged dataChanged() instead.
    Q_INVOKABLE void applyPresetLinear();
    Q_INVOKABLE void applyPresetSCurve();
    Q_INVOKABLE void invertCurve();

signals:
    void deadzoneChanged();
    void sensitivityChanged();
    void smoothingFactorChanged();
    /// Fired after any point mutation (drag, add, remove, preset, invert) -
    /// the Canvas listens to this to know when to requestPaint(). Separate
    /// from the model's own dataChanged/rowsInserted/rowsRemoved/
    /// modelReset signals, which drive the Repeater instead.
    void pointsChanged();
    void selectedIndexChanged();
    void diagonalSymmetryChanged();

private:
    static QVariantMap makePoint(double x, double y);

    /// Shared clamping rules for a single point write - factored out of
    /// updatePoint() so the diagonal-symmetry mirror point can go through
    /// exactly the same endpoint/neighbour rules as the point the user
    /// actually dragged, instead of being written unclamped. Does not emit
    /// any change notification itself - callers do that for whichever
    /// row(s) they actually touched.
    void setPointClamped(int index, double x, double y);

    double m_deadzone;
    double m_sensitivity;
    double m_smoothingFactor;
    int m_selectedIndex;
    bool m_diagonalSymmetry;
    QList<QPointF> m_points;
};
