#include "CalibrationViewModel.h"

#include <algorithm>

#include "DeviceManager.h"
#include "ProfileManager.h"

CalibrationViewModel::CalibrationViewModel(ProfileManager &profileManager, QObject *parent)
    : QObject(parent)
    , m_profileManager(profileManager)
{
    m_minObserved.fill(kUnobservedMin);
    m_maxObserved.fill(kUnobservedMax);

    connect(&DeviceManager::instance(), &DeviceManager::axisMoved, this, &CalibrationViewModel::onAxisMoved);
}

void CalibrationViewModel::startCalibration(const QString &systemPath)
{
    m_currentSystemPath = systemPath;
    m_minObserved.fill(kUnobservedMin);
    m_maxObserved.fill(kUnobservedMax);
    m_isCalibrating = true;
}

void CalibrationViewModel::commitCalibration()
{
    for (int i = 0; i < kNumAxes; ++i) {
        if (m_minObserved[i] < m_maxObserved[i]) {
            m_profileManager.setAxisCalibration(m_currentSystemPath, i, m_minObserved[i], m_maxObserved[i]);
        }
    }
    m_isCalibrating = false;
}

void CalibrationViewModel::cancelCalibration()
{
    m_isCalibrating = false;
}

void CalibrationViewModel::onAxisMoved(const QString &systemPath, int axisIndex, int value)
{
    if (!m_isCalibrating || systemPath != m_currentSystemPath) {
        return;
    }
    if (axisIndex < 0 || axisIndex >= kNumAxes) {
        return;
    }

    m_minObserved[axisIndex] = std::min(m_minObserved[axisIndex], value);
    m_maxObserved[axisIndex] = std::max(m_maxObserved[axisIndex], value);
}
