#include "CurveHandler.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QJsonArray>
#include <QLatin1String>

namespace {
/// Density of the one-time parametric sampling pass used to bake the LUT.
/// Oversamples kLutSize by 4x so re-bucketing (nearest-neighbor-pair lerp
/// by X) stays accurate even where the curve's local slope is steep.
constexpr int kBezierSampleCount = 4096;
} // namespace

CurveHandler::CurveHandler(std::shared_ptr<IVirtualOutputDevice> target,
                            int targetAxis,
                            int inputMin,
                            int inputMax,
                            int outputMin,
                            int outputMax,
                            double deadzone,
                            double sensitivity,
                            double smoothingFactor,
                            double x1,
                            double y1,
                            double x2,
                            double y2,
                            const std::vector<QPointF> &curvePoints,
                            bool invert)
    : m_target(std::move(target))
    , m_targetAxis(targetAxis)
    , m_inputMin(inputMin)
    , m_inputMax(inputMax)
    , m_outputMin(outputMin)
    , m_outputMax(outputMax)
    , m_deadzone(std::clamp(deadzone, 0.0, 0.99))
    , m_sensitivity(sensitivity)
    , m_smoothingFactor(std::clamp(smoothingFactor, 0.0, 0.99))
    , m_lastRawFiltered(0.0)
    , m_firstEvent(true)
    , m_curvePoints(curvePoints)
    , m_invert(invert)
    , m_lut(curvePoints.empty() ? buildBezierLut(x1, y1, x2, y2) : buildSplineLut(curvePoints))
{
}

std::vector<double> CurveHandler::buildBezierLut(double x1, double y1, double x2, double y2)
{
    // Dense parametric sampling of the cubic Bezier B(t), t in [0, 1]:
    //   B(t) = (1-t)^3 P0 + 3(1-t)^2 t P1 + 3(1-t) t^2 P2 + t^3 P3
    // with P0=(0,0) and P3=(1,1), so the P0 term drops out and the P3
    // term is just t^3 (since P3=(1,1)).
    std::vector<double> sampleX(kBezierSampleCount);
    std::vector<double> sampleY(kBezierSampleCount);
    for (int i = 0; i < kBezierSampleCount; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kBezierSampleCount - 1);
        const double mt = 1.0 - t;
        const double coeffP1 = 3.0 * mt * mt * t;
        const double coeffP2 = 3.0 * mt * t * t;
        const double coeffP3 = t * t * t;

        sampleX[i] = coeffP1 * x1 + coeffP2 * x2 + coeffP3;
        sampleY[i] = coeffP1 * y1 + coeffP2 * y2 + coeffP3;
    }

    // Re-bucket the dense (t-indexed) samples into a LUT uniformly indexed
    // over the bipolar domain [-1, 1]. The default control points describe
    // a magnitude-only response (P0=(0,0), P3=(1,1)); mirroring it as an
    // odd function (f(-x) = -f(x)) is what makes a joystick pushed forward
    // or back respond identically by default, absent any curvePoints
    // telling the LUT otherwise. Because magnitude runs from 1 down to 0
    // and back up to 1 as j sweeps the full [-1, 1] domain (not monotonic
    // the way it was over plain [0, 1]), each entry looks its own bracket
    // up via binary search instead of the previous incremental scan - this
    // only runs once, in the constructor, so the extra log(n) is free.
    std::vector<double> lut(CurveHandler::kLutSize);
    for (int j = 0; j < CurveHandler::kLutSize; ++j) {
        const double targetX = -1.0 + 2.0 * static_cast<double>(j) / static_cast<double>(CurveHandler::kLutSize - 1);
        const double sign = targetX < 0.0 ? -1.0 : 1.0;
        const double magnitude = std::abs(targetX);

        const auto upper = std::lower_bound(sampleX.begin(), sampleX.end(), magnitude);
        int hi = static_cast<int>(std::min<std::ptrdiff_t>(upper - sampleX.begin(), kBezierSampleCount - 1));
        if (hi == 0) {
            hi = 1;
        }
        const int lo = hi - 1;

        const double bracketX0 = sampleX[lo];
        const double bracketX1 = sampleX[hi];
        const double bracketY0 = sampleY[lo];
        const double bracketY1 = sampleY[hi];

        const double span = bracketX1 - bracketX0;
        const double frac = (span > 1e-12) ? (magnitude - bracketX0) / span : 0.0;
        const double magnitudeY = bracketY0 + frac * (bracketY1 - bracketY0);
        lut[j] = sign * magnitudeY;
    }

    return lut;
}

std::vector<double> CurveHandler::buildSplineLut(std::vector<QPointF> points)
{
    std::sort(points.begin(), points.end(),
              [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });

    std::vector<double> lut(CurveHandler::kLutSize);

    if (points.size() < 2) {
        std::fill(lut.begin(), lut.end(), points.empty() ? 0.0 : points.front().y());
        return lut;
    }

    const std::size_t n = points.size();

    // Fritsch-Carlson monotone cubic Hermite spline. Tangents start as the
    // average of the two secant slopes flanking each point, then get
    // rescaled (per Fritsch-Carlson) against those same secants - that
    // rescaling is what guarantees the spline never overshoots past a
    // point's own Y on its way to a neighbor, unlike a plain Catmull-Rom
    // spline, which can ring past [0,1] on a sharp S-curve.
    std::vector<double> secants(n - 1);
    for (std::size_t i = 0; i < n - 1; ++i) {
        const double dx = points[i + 1].x() - points[i].x();
        secants[i] = (dx > 1e-12) ? (points[i + 1].y() - points[i].y()) / dx : 0.0;
    }

    std::vector<double> tangents(n);
    tangents[0] = secants[0];
    tangents[n - 1] = secants[n - 2];
    for (std::size_t i = 1; i < n - 1; ++i) {
        // A sign change (or flat run) between the two flanking secants
        // means this point is a local extremum - zeroing the tangent here
        // is what keeps the spline from bulging past it.
        tangents[i] = (secants[i - 1] * secants[i] <= 0.0) ? 0.0 : (secants[i - 1] + secants[i]) / 2.0;
    }

    for (std::size_t i = 0; i < n - 1; ++i) {
        if (secants[i] == 0.0) {
            tangents[i] = 0.0;
            tangents[i + 1] = 0.0;
            continue;
        }
        const double alpha = tangents[i] / secants[i];
        const double beta = tangents[i + 1] / secants[i];
        const double magnitude = std::hypot(alpha, beta);
        if (magnitude > 3.0) {
            const double tau = 3.0 / magnitude;
            tangents[i] = tau * alpha * secants[i];
            tangents[i + 1] = tau * beta * secants[i];
        }
    }

    std::size_t segment = 0;
    for (int j = 0; j < CurveHandler::kLutSize; ++j) {
        const double targetX = -1.0 + 2.0 * static_cast<double>(j) / static_cast<double>(CurveHandler::kLutSize - 1);

        // Flat extrapolation outside the caller-supplied points' own X range.
        if (targetX <= points.front().x()) {
            lut[j] = points.front().y();
            continue;
        }
        if (targetX >= points.back().x()) {
            lut[j] = points.back().y();
            continue;
        }

        while (segment + 1 < n && points[segment + 1].x() < targetX) {
            ++segment;
        }

        const double x0 = points[segment].x();
        const double x1 = points[segment + 1].x();
        const double y0 = points[segment].y();
        const double y1 = points[segment + 1].y();
        const double h = x1 - x0;
        const double t = (h > 1e-12) ? (targetX - x0) / h : 0.0;

        // Cubic Hermite basis functions.
        const double t2 = t * t;
        const double t3 = t2 * t;
        const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
        const double h10 = t3 - 2.0 * t2 + t;
        const double h01 = -2.0 * t3 + 3.0 * t2;
        const double h11 = t3 - t2;

        lut[j] = h00 * y0 + h10 * h * tangents[segment] + h01 * y1 + h11 * h * tangents[segment + 1];
    }

    return lut;
}

double CurveHandler::evaluateCurve(double x) const
{
    const double clampedX = std::clamp(x, -1.0, 1.0);
    const double idx = (clampedX + 1.0) * 0.5 * static_cast<double>(m_lut.size() - 1);

    const int i0 = static_cast<int>(idx);
    const int i1 = std::min(i0 + 1, static_cast<int>(m_lut.size()) - 1);
    const double frac = idx - static_cast<double>(i0);

    return m_lut[i0] + frac * (m_lut[i1] - m_lut[i0]);
}

void CurveHandler::processAxis(const AxisEvent &evt)
{
    const double inputRange = static_cast<double>(m_inputMax - m_inputMin);
    if (inputRange <= 0.0 || !m_target) {
        return;
    }

    int value = evt.value;

    // EMA (exponential moving average) low-pass filter (Fase 13): smooths
    // out a worn physical joystick's electrical jitter before anything else
    // touches the raw value - deadzone/curve/sensitivity below all operate
    // on this already-filtered value, same as if the hardware itself were
    // less noisy. m_firstEvent seeds the filter from the very first sample
    // directly rather than blending it against an arbitrary 0 starting
    // point, which would otherwise make the first reported value jump
    // toward 0 before settling.
    if (m_smoothingFactor > 0.0) {
        if (m_firstEvent) {
            m_lastRawFiltered = static_cast<double>(value);
            m_firstEvent = false;
        } else {
            const double alpha = 1.0 - m_smoothingFactor; // 1.0 (no smoothing) to 0.01 (max).
            m_lastRawFiltered = (alpha * static_cast<double>(value)) + ((1.0 - alpha) * m_lastRawFiltered);
        }
        value = static_cast<int>(std::round(m_lastRawFiltered));
    }

    // Normalize to bipolar [-1, 1] around the input range's midpoint.
    const double normalized = ((static_cast<double>(value) - m_inputMin) / inputRange) * 2.0 - 1.0;

    // Deadzone, applied directly over the bipolar [-1, 1] range: any value
    // whose magnitude falls inside the deadzone collapses to exactly 0.0,
    // and the region outside it is rescaled back out to fill [-1, 1]
    // cleanly, with no jump at the boundary. Sign is tracked separately
    // only to rebuild the rescaled magnitude - the curve below is now
    // evaluated on this signed value directly, over its own bipolar
    // domain, so it can be asymmetric (shaped differently pushed forward
    // vs. back) instead of always mirroring a magnitude-only response.
    const double sign = normalized < 0.0 ? -1.0 : 1.0;
    const double magnitude = std::abs(normalized);
    const double deadzoneAdjusted = (magnitude > m_deadzone)
        ? sign * (magnitude - m_deadzone) / (1.0 - m_deadzone)
        : 0.0;

    // O(1) LUT lookup: the expensive part (baking the curve) already
    // happened once, in the constructor.
    const double curved = evaluateCurve(deadzoneAdjusted);

    // Sensitivity, then clamp back to [-1, 1] (a curve with Y values
    // outside [-1, 1] can overshoot past the endpoints on purpose;
    // sensitivity can push it further; this is where both get reined back
    // in before mapping to the output range).
    double adjusted = std::clamp(curved * m_sensitivity, -1.0, 1.0);

    // Physical inversion: flips output polarity outright, distinct from
    // "inverting" a curve/points in the graphical editor (which only
    // reshapes the response into a "V" on a bipolar axis instead of
    // actually reversing which physical direction drives which output
    // direction).
    if (m_invert) {
        adjusted = -adjusted;
    }

    // Map back to the target device's output range.
    const double outputRange = static_cast<double>(m_outputMax - m_outputMin);
    const int outputValue = m_outputMin + static_cast<int>(std::lround((adjusted + 1.0) * 0.5 * outputRange));

    m_target->setAxis(m_targetAxis, outputValue);
}

void CurveHandler::processButton(const ButtonEvent & /*evt*/)
{
    // Axis-only handler: button events have no meaning for a curve/deadzone transform.
}

QJsonObject CurveHandler::toJson() const
{
    QJsonObject binding;
    binding[QLatin1String("actionType")] = QStringLiteral("CurveHandler");
    binding[QLatin1String("targetOutputId")] = m_target ? m_target->deviceId() : 0;
    if (m_target && m_target->isViGEmDevice()) {
        binding[QLatin1String("targetDeviceType")] = QStringLiteral("vigem");
    }
    binding[QLatin1String("targetAxis")] = m_targetAxis;

    QJsonObject parameters;
    parameters[QLatin1String("inputMin")] = m_inputMin;
    parameters[QLatin1String("inputMax")] = m_inputMax;
    parameters[QLatin1String("outputMin")] = m_outputMin;
    parameters[QLatin1String("outputMax")] = m_outputMax;
    parameters[QLatin1String("deadzone")] = m_deadzone;
    parameters[QLatin1String("sensitivity")] = m_sensitivity;
    if (m_smoothingFactor > 0.0) {
        parameters[QLatin1String("smoothingFactor")] = m_smoothingFactor;
    }
    if (m_invert) {
        parameters[QLatin1String("invert")] = m_invert;
    }

    if (!m_curvePoints.empty()) {
        QJsonArray curvePointsArray;
        for (const QPointF &point : m_curvePoints) {
            QJsonObject pointObject;
            pointObject[QLatin1String("x")] = point.x();
            pointObject[QLatin1String("y")] = point.y();
            curvePointsArray.append(pointObject);
        }
        parameters[QLatin1String("curvePoints")] = curvePointsArray;
    }

    binding[QLatin1String("parameters")] = parameters;
    return binding;
}
