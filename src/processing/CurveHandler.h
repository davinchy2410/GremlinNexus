#pragma once

#include <memory>
#include <vector>

#include <QPointF>

#include "IActionHandler.h"
#include "IVirtualOutputDevice.h"

/**
 * @brief Axis-only transform: deadzone + response curve + sensitivity.
 *
 * Pipeline, per processAxis() call:
 *  1. Normalize the raw input value to bipolar [-1, 1].
 *  2. Deadzone: rescale the region outside it back to [-1, 1] (magnitude
 *     only) so there's no jump at the deadzone boundary — identical to the
 *     original Phase 4 behavior.
 *  3. Cubic Bezier response curve, applied to the deadzone-adjusted
 *     *magnitude* (i.e. |value| in [0, 1], sign re-applied after), via a
 *     precomputed lookup table (LUT) — see "Performance" below.
 *  4. Linear sensitivity multiplier, then clamp back to [-1, 1].
 *  5. Map into the target device's output range and stage it via
 *     IVirtualOutputDevice::setAxis().
 *
 * The curve is a standard cubic Bezier with fixed endpoints P0=(0,0),
 * P3=(1,1) and caller-supplied intermediate control points P1=(x1,y1),
 * P2=(x2,y2) — the same shape used by response-curve editors in flight-sim
 * tools: extra precision near center, extra speed toward the extremes, or
 * whatever shape the two control points describe.
 *
 * Default control points (1/3, 1/3) and (2/3, 2/3) make the curve the
 * *exact* identity line y=x (this is a standard Bezier identity: control
 * points at 1/3 and 2/3 along the diagonal reproduce a constant-speed
 * straight line), so a CurveHandler constructed without specifying x1/y1/
 * x2/y2 behaves byte-for-byte like the original linear-only Phase 4/5
 * CurveHandler — existing call sites (ProfileManager's CurveHandler
 * bindings) keep working unchanged.
 *
 * Multi-point mode: if curvePoints is non-empty, it *replaces* the shape of
 * step 3 above - the curve is a monotone cubic Hermite spline (Fritsch-
 * Carlson) through curvePoints (sorted by X) instead of the cubic Bezier
 * from x1/y1/x2/y2 (which are then ignored). Passes exactly through every
 * control point, with tangents chosen specifically to never overshoot past
 * a point's own Y value between it and its neighbors - unlike a plain
 * Catmull-Rom spline, this can't ring/bulge past [0,1] on a sharp S-curve.
 * Steps 1, 2, 4 and 5 - deadzone and sensitivity - are untouched either
 * way; only which curve gets baked into the LUT changes. Querying outside
 * curvePoints' own X range holds flat at the nearest endpoint's Y rather
 * than extrapolating or going out of bounds.
 *
 * Performance: evaluating either curve shape exactly on every axis event
 * (a parametric Bezier's implicit cubic root-find, or re-walking a
 * variable-length list of spline segments) is too expensive to do at 200 Hz.
 * Instead, whichever shape was requested is densely evaluated once, in the
 * constructor, and baked into a fixed-size LUT indexed by X; processAxis()
 * only ever does an O(1) array lookup plus one linear interpolation
 * between neighboring LUT entries — no trigonometry, no root-finding, no
 * segment search, no per-event allocation.
 *
 * Button events carry no meaning for a curve and are ignored.
 */
class CurveHandler : public IActionHandler
{
public:
    /// Number of entries in the precomputed response-curve LUT. 1024
    /// entries means each bucket covers ~0.1% of the input range — far
    /// finer than any real joystick's HID logical resolution — while
    /// staying a trivial 8 KiB (double) per handler instance.
    static constexpr int kLutSize = 1024;

    /**
     * @param target      Virtual output device that receives the transformed value.
     * @param targetAxis  Axis index on target (see VJoyDevice's axis ordering).
     * @param inputMin    Raw HID logical minimum of the source axis.
     * @param inputMax    Raw HID logical maximum of the source axis.
     * @param outputMin   Raw logical minimum expected by the target axis
     *                    (vJoy's default axis range is [0, 32767]).
     * @param outputMax   Raw logical maximum expected by the target axis.
     * @param deadzone    Fraction of the normalized [-1, 1] range around
     *                    center treated as zero; clamped to [0, 0.99].
     * @param sensitivity Linear multiplier applied after the curve.
     * @param smoothingFactor EMA (exponential moving average) low-pass
     *                    filter strength applied to the raw HID value before
     *                    anything else (Fase 13) - stabilizes a worn
     *                    physical joystick's electrical jitter. 0.0 (the
     *                    default) disables it entirely; up to 0.99 for
     *                    maximum smoothing (heavier lag). Clamping is the
     *                    caller's responsibility (see ProfileManager) - this
     *                    constructor stores it as given.
     * @param x1          First Bezier control point's X, in [0, 1].
     * @param y1          First Bezier control point's Y (unclamped: values
     *                    outside [0, 1] give a curve that overshoots past
     *                    the endpoints, which is a legitimate response
     *                    curve shape, not an error).
     * @param x2          Second Bezier control point's X, in [0, 1].
     * @param y2          Second Bezier control point's Y (see y1).
     * @param curvePoints Optional multi-point (piecewise-linear) curve,
     *                    (x, y) pairs in the same [0, 1]-ish domain as the
     *                    Bezier control points above. Non-empty overrides
     *                    x1/y1/x2/y2 entirely (see class docs). Points need
     *                    not be pre-sorted by X - the constructor sorts them.
     * @param invert      Physically flips the axis' output polarity - unlike
     *                    "inverting" a curve/points in the graphical editor
     *                    (which only reshapes the response into a "V" on a
     *                    bipolar axis), this negates the deadzone/curve-
     *                    adjusted value itself before sensitivity/clamping,
     *                    so a push in one physical direction now drives the
     *                    output the other way. false (the default) matches
     *                    every CurveHandler binding created before this
     *                    parameter existed.
     */
    explicit CurveHandler(std::shared_ptr<IVirtualOutputDevice> target,
                           int targetAxis,
                           int inputMin = 0,
                           int inputMax = 65535,
                           int outputMin = 0,
                           int outputMax = 32767,
                           double deadzone = 0.05,
                           double sensitivity = 1.0,
                           double smoothingFactor = 0.0,
                           double x1 = 1.0 / 3.0,
                           double y1 = 1.0 / 3.0,
                           double x2 = 2.0 / 3.0,
                           double y2 = 2.0 / 3.0,
                           const std::vector<QPointF> &curvePoints = {},
                           bool invert = false);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    /// Rebuilds this handler's "CurveHandler" binding JSON (Fase 10.8),
    /// matching ProfileManager::instantiateCurveHandler()'s schema exactly
    /// so the result loads back byte-for-byte equivalent. Only curvePoints
    /// round-trips faithfully - not x1/y1/x2/y2 - because the JSON schema
    /// itself has no field for custom Bezier control points (see
    /// ProfileManager's class docs); m_lut is baked/one-way, so this reads
    /// from the retained m_curvePoints copy instead of trying to recover
    /// control points from the LUT.
    QJsonObject toJson() const override;

    /// EMA smoothing strength this handler was built with (Fase 13) - lets
    /// ProfileManager::serializeCurveHandler() (via toJson()) and any future
    /// QML config surface read back what's currently in effect.
    double smoothingFactor() const { return m_smoothingFactor; }

private:
    /// Densely samples the cubic Bezier B(t) (P0=(0,0), P1=(x1,y1),
    /// P2=(x2,y2), P3=(1,1)) and re-buckets it into a kLutSize-entry LUT
    /// uniformly indexed by X. Called once, from the constructor.
    static std::vector<double> buildBezierLut(double x1, double y1, double x2, double y2);

    /// Builds a kLutSize-entry LUT uniformly indexed by X via a monotone
    /// cubic Hermite spline (Fritsch-Carlson tangents) through points
    /// (sorted by X first) - passes exactly through every point with no
    /// overshoot past a point's own Y between it and its neighbors. Flat
    /// extrapolation for X outside [points.front().x(), points.back().x()].
    /// Called once, from the constructor, only when points is non-empty.
    static std::vector<double> buildSplineLut(std::vector<QPointF> points);

    /// O(1) LUT lookup with linear interpolation between the two nearest entries.
    double evaluateCurve(double x) const;

    std::shared_ptr<IVirtualOutputDevice> m_target;
    int m_targetAxis;
    int m_inputMin;
    int m_inputMax;
    int m_outputMin;
    int m_outputMax;
    double m_deadzone;
    double m_sensitivity;

    /// EMA low-pass filter strength (Fase 13), in [0.0, 0.99] - 0.0 disables
    /// it; see processAxis()'s own comments for the filter itself. Applied
    /// to the raw HID value before normalization/deadzone/curve/sensitivity,
    /// so it smooths out a worn joystick's electrical jitter rather than
    /// smoothing an already-shaped output.
    double m_smoothingFactor;

    /// Last EMA-filtered raw value, in the same units as AxisEvent::value
    /// (not yet normalized) - carried across processAxis() calls so each
    /// new sample blends against the filter's own running state, not the
    /// previous *raw* sample.
    double m_lastRawFiltered;

    /// True until the first processAxis() call, so that call can seed
    /// m_lastRawFiltered from the raw value directly instead of blending it
    /// against an arbitrary 0.0 starting point (which would otherwise make
    /// the very first reported value jump toward 0 before settling).
    bool m_firstEvent;

    /// Retained verbatim from the constructor's curvePoints argument, purely
    /// for toJson() - m_lut (below) is a baked, one-way sampling of it and
    /// cannot itself be turned back into control points. Empty when this
    /// handler was built from the Bezier x1/y1/x2/y2 path instead.
    std::vector<QPointF> m_curvePoints;

    /// Physically flips the axis' output polarity - see the constructor's
    /// own docs for how this differs from "inverting" a curve/points in the
    /// graphical editor.
    bool m_invert;

    std::vector<double> m_lut;
};
