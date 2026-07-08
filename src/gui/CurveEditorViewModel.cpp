#include "CurveEditorViewModel.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kMinGap = 0.01; // Minimum x-distance kept between neighbouring points.
}

CurveEditorViewModel::CurveEditorViewModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_deadzone(0.0)
    , m_sensitivity(1.0)
    , m_smoothingFactor(0.0)
    , m_selectedIndex(-1)
    , m_diagonalSymmetry(false)
{
    // Three simple default points spanning the bipolar range: full negative,
    // center, full positive - a neutral linear response until the user
    // drags something.
    m_points = {QPointF(-1.0, -1.0), QPointF(0.0, 0.0), QPointF(1.0, 1.0)};
}

int CurveEditorViewModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_points.size();
}

QVariant CurveEditorViewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_points.size()) {
        return {};
    }

    const QPointF &point = m_points.at(index.row());
    switch (role) {
    case PointXRole: return point.x();
    case PointYRole: return point.y();
    default: return {};
    }
}

QHash<int, QByteArray> CurveEditorViewModel::roleNames() const
{
    return {
        {PointXRole, "pointX"},
        {PointYRole, "pointY"},
    };
}

double CurveEditorViewModel::deadzone() const
{
    return m_deadzone;
}

void CurveEditorViewModel::setDeadzone(double value)
{
    const double clamped = std::clamp(value, 0.0, 0.9);
    if (qFuzzyCompare(m_deadzone + 1.0, clamped + 1.0)) {
        return;
    }
    m_deadzone = clamped;
    emit deadzoneChanged();
}

double CurveEditorViewModel::sensitivity() const
{
    return m_sensitivity;
}

void CurveEditorViewModel::setSensitivity(double value)
{
    const double clamped = std::clamp(value, 0.1, 3.0);
    if (!qFuzzyCompare(m_sensitivity, clamped)) {
        m_sensitivity = clamped;
        emit sensitivityChanged();
    }
}

double CurveEditorViewModel::smoothingFactor() const
{
    return m_smoothingFactor;
}

void CurveEditorViewModel::setSmoothingFactor(double value)
{
    const double clamped = std::clamp(value, 0.0, 1.0);
    if (!qFuzzyCompare(m_smoothingFactor, clamped)) {
        m_smoothingFactor = clamped;
        emit smoothingFactorChanged();
    }
}

QVariantList CurveEditorViewModel::points() const
{
    QVariantList list;
    list.reserve(m_points.size());
    for (const QPointF &point : m_points) {
        list.append(makePoint(point.x(), point.y()));
    }
    return list;
}

void CurveEditorViewModel::setPoints(const QVariantList &points)
{
    beginResetModel();
    m_points.clear();
    m_points.reserve(points.size());
    for (const QVariant &pointVariant : points) {
        const QVariantMap point = pointVariant.toMap();
        const double x = std::clamp(point.value(QStringLiteral("x")).toDouble(), -1.0, 1.0);
        const double y = std::clamp(point.value(QStringLiteral("y")).toDouble(), -1.0, 1.0);
        m_points.append(QPointF(x, y));
    }
    if (m_points.size() < 2) {
        m_points = {QPointF(-1.0, -1.0), QPointF(1.0, 1.0)};
    }
    endResetModel();
    setSelectedIndex(-1);
    emit pointsChanged();
}

int CurveEditorViewModel::selectedIndex() const
{
    return m_selectedIndex;
}

void CurveEditorViewModel::setSelectedIndex(int index)
{
    const int coerced = (index >= 0 && index < m_points.size()) ? index : -1;
    if (coerced == m_selectedIndex) {
        return;
    }
    m_selectedIndex = coerced;
    emit selectedIndexChanged();
}

bool CurveEditorViewModel::diagonalSymmetry() const
{
    return m_diagonalSymmetry;
}

void CurveEditorViewModel::setDiagonalSymmetry(bool value)
{
    if (m_diagonalSymmetry == value) {
        return;
    }
    m_diagonalSymmetry = value;
    emit diagonalSymmetryChanged();
}

void CurveEditorViewModel::setPointClamped(int index, double x, double y)
{
    if (index < 0 || index >= m_points.size()) {
        return;
    }

    const double clampedY = std::clamp(y, -1.0, 1.0);
    double clampedX = std::clamp(x, -1.0, 1.0);

    if (index == 0) {
        clampedX = -1.0;
    } else if (index == m_points.size() - 1) {
        clampedX = 1.0;
    } else {
        const double lowerBound = m_points.at(index - 1).x() + kMinGap;
        const double upperBound = m_points.at(index + 1).x() - kMinGap;
        clampedX = std::clamp(clampedX, lowerBound, upperBound);
    }

    m_points[index] = QPointF(clampedX, clampedY);
}

void CurveEditorViewModel::updatePoint(int index, double x, double y)
{
    if (index < 0 || index >= m_points.size()) {
        return;
    }

    setPointClamped(index, x, y);
    const QModelIndex changedIndex = this->index(index);
    emit dataChanged(changedIndex, changedIndex, {PointXRole, PointYRole});

    if (m_diagonalSymmetry) {
        const int mirrorIndex = m_points.size() - 1 - index;
        if (mirrorIndex != index) {
            // Mirrors the *requested* (x, y) through the bipolar origin
            // (0, 0), not the already-clamped result - the mirror point has
            // its own neighbours/endpoint rules and goes through the exact
            // same clamping in setPointClamped() above.
            setPointClamped(mirrorIndex, -x, -y);
            const QModelIndex mirrorModelIndex = this->index(mirrorIndex);
            emit dataChanged(mirrorModelIndex, mirrorModelIndex, {PointXRole, PointYRole});
        }
    }

    emit pointsChanged();
}

void CurveEditorViewModel::addPoint(double x, double y)
{
    const double clampedX = std::clamp(x, -1.0, 1.0);
    const double clampedY = std::clamp(y, -1.0, 1.0);

    // Inserts (px, py) at its sorted-by-x position, subject to the same
    // kMinGap-from-both-neighbours rule the single-point path always
    // enforced. Returns whether it landed successfully; m_points is left
    // untouched on failure.
    auto insertSorted = [this](double px, double py) -> bool {
        int insertAt = 0;
        while (insertAt < m_points.size() && m_points.at(insertAt).x() < px) {
            ++insertAt;
        }
        if (insertAt > 0 && px - m_points.at(insertAt - 1).x() < kMinGap) {
            return false;
        }
        if (insertAt < m_points.size() && m_points.at(insertAt).x() - px < kMinGap) {
            return false;
        }
        beginInsertRows(QModelIndex(), insertAt, insertAt);
        m_points.insert(insertAt, QPointF(px, py));
        endInsertRows();
        return true;
    };

    // With diagonalSymmetry on, a lone point breaks the array's parity: the
    // *next* drag's by-index mirror (updatePoint()'s own size()-1-index)
    // would then land on some pre-existing point instead of a genuine
    // partner, silently relocating it and - since that mirror target's own
    // x can end up clamped against the wrong neighbours - potentially
    // breaking the sorted-by-x invariant the spline evaluation (both here
    // and in CurveHandler/the QML canvas) assumes, which is what made the
    // curve vanish. Inserting the reflection (-x, -y) alongside the
    // requested point up front keeps every future by-index mirror a real,
    // intentional pair. abs(x) > 1e-3 skips mirroring a click dead-center,
    // where the reflection would just be the same point again.
    const bool mirrored = m_diagonalSymmetry && std::abs(clampedX) > 1e-3;

    if (!mirrored) {
        if (!insertSorted(clampedX, clampedY)) {
            return;
        }
    } else {
        const double mirrorX = -clampedX;
        const double mirrorY = -clampedY;
        // Insert whichever of the pair sorts first by X first, so the
        // second insertion's own neighbour-gap check already sees the
        // first as a real neighbour - this is what catches the pair being
        // too close to *each other* (e.g. a click just barely off-centre),
        // not just too close to pre-existing points.
        const bool primaryFirst = clampedX <= mirrorX;
        if (!insertSorted(primaryFirst ? clampedX : mirrorX, primaryFirst ? clampedY : mirrorY)) {
            return;
        }
        if (!insertSorted(primaryFirst ? mirrorX : clampedX, primaryFirst ? mirrorY : clampedY)) {
            // Roll back - inserting only half of a symmetric pair would
            // leave the curve lopsided and right back in the broken-parity
            // state this whole path exists to avoid.
            const int stray = m_points.indexOf(QPointF(primaryFirst ? clampedX : mirrorX, primaryFirst ? clampedY : mirrorY));
            beginRemoveRows(QModelIndex(), stray, stray);
            m_points.removeAt(stray);
            endRemoveRows();
            return;
        }
    }

    // Both values were inserted verbatim (no rounding in between), so an
    // exact match reliably finds wherever the *originally requested* point
    // - not its mirror - ended up landing, regardless of which of the pair
    // was inserted first.
    m_selectedIndex = m_points.indexOf(QPointF(clampedX, clampedY));
    emit selectedIndexChanged();
    emit pointsChanged();
}

void CurveEditorViewModel::removePoint(int index)
{
    // The two curve endpoints anchor the bipolar domain [-1,1] and are
    // never removable - CurveHandler's flat-extrapolation-outside-the-range
    // behavior depends on there always being a first and last point.
    if (index <= 0 || index >= m_points.size() - 1) {
        return;
    }

    const int previousSelected = m_selectedIndex;

    auto removeRow = [this](int rowIndex) {
        beginRemoveRows(QModelIndex(), rowIndex, rowIndex);
        m_points.removeAt(rowIndex);
        endRemoveRows();

        if (m_selectedIndex == rowIndex) {
            m_selectedIndex = -1;
        } else if (m_selectedIndex > rowIndex) {
            --m_selectedIndex;
        }
    };

    // With diagonalSymmetry on, index's own reflection (size()-1-index) is
    // a real, independent point the user shaped as a pair with it (see
    // addPoint()) - removing only one half would leave a stray, unpaired
    // point behind and break the parity every future drag's by-index
    // mirror relies on. Computed against the *current* size, before either
    // removal happens.
    int mirrorIndex = -1;
    if (m_diagonalSymmetry) {
        const int candidate = m_points.size() - 1 - index;
        if (candidate > 0 && candidate < m_points.size() - 1 && candidate != index) {
            mirrorIndex = candidate;
        }
    }

    if (mirrorIndex < 0) {
        removeRow(index);
    } else if (mirrorIndex > index) {
        // Higher index first, so removing it doesn't shift `index` out
        // from under the second removeRow() call.
        removeRow(mirrorIndex);
        removeRow(index);
    } else {
        removeRow(index);
        removeRow(mirrorIndex);
    }

    if (m_selectedIndex != previousSelected) {
        emit selectedIndexChanged();
    }
    emit pointsChanged();
}

void CurveEditorViewModel::applyPresetLinear()
{
    beginResetModel();
    m_points = {QPointF(-1.0, -1.0), QPointF(0.0, 0.0), QPointF(1.0, 1.0)};
    endResetModel();
    setSelectedIndex(-1);
    emit pointsChanged();
}

void CurveEditorViewModel::applyPresetSCurve()
{
    // Flattens the response near the extremes and steepens it through the
    // centre - the classic "gentle near center, precise off-center" feel
    // sim racers/flight-sim users expect from an S-Curve. Same relative
    // shape as the original unipolar preset, affinely remapped onto the
    // bipolar domain (x' = 2x-1, y' = 2y-1) so it stays centered on the
    // origin instead of the old (0.5, 0.5) midpoint.
    beginResetModel();
    m_points = {
        QPointF(-1.0, -1.0),
        QPointF(-0.5, -0.7),
        QPointF(0.0, 0.0),
        QPointF(0.5, 0.7),
        QPointF(1.0, 1.0),
    };
    endResetModel();
    setSelectedIndex(-1);
    emit pointsChanged();
}

void CurveEditorViewModel::invertCurve()
{
    // Inverts around the bipolar origin (0.0), not the old unipolar
    // midpoint (0.5) - a push in either physical direction still ends up
    // pulling the output the opposite way.
    for (QPointF &point : m_points) {
        point.setY(-point.y());
    }
    if (!m_points.isEmpty()) {
        emit dataChanged(index(0), index(m_points.size() - 1), {PointYRole});
    }
    emit pointsChanged();
}

QVariantMap CurveEditorViewModel::makePoint(double x, double y)
{
    QVariantMap point;
    point[QStringLiteral("x")] = x;
    point[QStringLiteral("y")] = y;
    return point;
}
