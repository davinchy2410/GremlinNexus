#include "CurveEditorViewModel.h"

#include <algorithm>

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
    // Three simple default points: origin, midpoint, full-scale - a neutral
    // linear response until the user drags something.
    m_points = {QPointF(0.0, 0.0), QPointF(0.5, 0.5), QPointF(1.0, 1.0)};
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
        const double x = std::clamp(point.value(QStringLiteral("x")).toDouble(), 0.0, 1.0);
        const double y = std::clamp(point.value(QStringLiteral("y")).toDouble(), 0.0, 1.0);
        m_points.append(QPointF(x, y));
    }
    if (m_points.size() < 2) {
        m_points = {QPointF(0.0, 0.0), QPointF(1.0, 1.0)};
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

    const double clampedY = std::clamp(y, 0.0, 1.0);
    double clampedX = std::clamp(x, 0.0, 1.0);

    if (index == 0) {
        clampedX = 0.0;
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
            // Mirrors the *requested* (x, y), not the already-clamped
            // result - the mirror point has its own neighbours/endpoint
            // rules and goes through the exact same clamping in
            // setPointClamped() above.
            setPointClamped(mirrorIndex, 1.0 - x, 1.0 - y);
            const QModelIndex mirrorModelIndex = this->index(mirrorIndex);
            emit dataChanged(mirrorModelIndex, mirrorModelIndex, {PointXRole, PointYRole});
        }
    }

    emit pointsChanged();
}

void CurveEditorViewModel::addPoint(double x, double y)
{
    const double clampedX = std::clamp(x, 0.0, 1.0);
    const double clampedY = std::clamp(y, 0.0, 1.0);

    int insertAt = 0;
    while (insertAt < m_points.size() && m_points.at(insertAt).x() < clampedX) {
        ++insertAt;
    }

    // Need room on both sides so the new point doesn't collapse a segment
    // to (near-)zero width against either neighbour.
    if (insertAt > 0 && clampedX - m_points.at(insertAt - 1).x() < kMinGap) {
        return;
    }
    if (insertAt < m_points.size() && m_points.at(insertAt).x() - clampedX < kMinGap) {
        return;
    }

    beginInsertRows(QModelIndex(), insertAt, insertAt);
    m_points.insert(insertAt, QPointF(clampedX, clampedY));
    endInsertRows();

    m_selectedIndex = insertAt;
    emit selectedIndexChanged();
    emit pointsChanged();
}

void CurveEditorViewModel::removePoint(int index)
{
    // The two curve endpoints anchor the domain [0,1] and are never
    // removable - CurveHandler's flat-extrapolation-outside-the-range
    // behavior depends on there always being a first and last point.
    if (index <= 0 || index >= m_points.size() - 1) {
        return;
    }

    beginRemoveRows(QModelIndex(), index, index);
    m_points.removeAt(index);
    endRemoveRows();

    if (m_selectedIndex == index) {
        m_selectedIndex = -1;
        emit selectedIndexChanged();
    } else if (m_selectedIndex > index) {
        --m_selectedIndex;
        emit selectedIndexChanged();
    }

    emit pointsChanged();
}

void CurveEditorViewModel::applyPresetLinear()
{
    beginResetModel();
    m_points = {QPointF(0.0, 0.0), QPointF(0.5, 0.5), QPointF(1.0, 1.0)};
    endResetModel();
    setSelectedIndex(-1);
    emit pointsChanged();
}

void CurveEditorViewModel::applyPresetSCurve()
{
    // Flattens the response near the extremes and steepens it through the
    // centre - the classic "gentle near center, precise off-center" feel
    // sim racers/flight-sim users expect from an S-Curve.
    beginResetModel();
    m_points = {
        QPointF(0.0, 0.0),
        QPointF(0.25, 0.15),
        QPointF(0.5, 0.5),
        QPointF(0.75, 0.85),
        QPointF(1.0, 1.0),
    };
    endResetModel();
    setSelectedIndex(-1);
    emit pointsChanged();
}

void CurveEditorViewModel::invertCurve()
{
    for (QPointF &point : m_points) {
        point.setY(1.0 - point.y());
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
