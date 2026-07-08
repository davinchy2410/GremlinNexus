#pragma once

#include <array>

#include <QObject>
#include <QString>

class ProfileManager;

/**
 * @brief Backs the "Calibration Wizard Popup" (Fase 10.9's mock-up, wired to
 *        real hardware data in Fase 11).
 *
 * While calibrating, listens to DeviceManager::axisMoved for m_currentSystemPath
 * and tracks the raw min/max ever observed per axis (std::min/std::max
 * against whatever was seen already - so a value only ever widens the
 * range, matching how a physical stick's full range of motion is meant to
 * be captured by moving it through full circles per axis, not by reading a
 * single sample). commitCalibration() then writes each moved axis' captured
 * range into ProfileManager via setAxisCalibration() - only an axis whose
 * min/max both got at least one real observed value (i.e. narrowed at all
 * from their [kUnobservedMin, kUnobservedMax] starting point) is written,
 * so an axis the user never touched during this session doesn't get a
 * bogus [65535, 0]-derived calibration.
 */
class CalibrationViewModel : public QObject
{
    Q_OBJECT

public:
    explicit CalibrationViewModel(ProfileManager &profileManager, QObject *parent = nullptr);

    /// Starts (or restarts) capturing raw axis samples for systemPath -
    /// resets every axis' observed min/max back to their unobserved extremes
    /// (kUnobservedMin/kUnobservedMax) first, so a previous device's/run's
    /// readings never leak into this one.
    Q_INVOKABLE void startCalibration(const QString &systemPath);

    /// Writes every axis that was actually moved this session (see class
    /// docs) into m_profileManager via setAxisCalibration(), then stops
    /// calibrating. A no-op on ProfileManager for a device on which nothing
    /// moved - calibration only ever narrows what setAxisCalibration()
    /// records, never resets it to a bogus full-scale range.
    Q_INVOKABLE void commitCalibration();

    /// Stops capturing without writing anything to ProfileManager - the
    /// observed min/max arrays are simply discarded on the next
    /// startCalibration() call.
    Q_INVOKABLE void cancelCalibration();

private slots:
    /// Relayed from DeviceManager::axisMoved - widens m_minObserved[axisIndex]/
    /// m_maxObserved[axisIndex] via std::min/std::max when m_isCalibrating is
    /// true and systemPath matches m_currentSystemPath.
    void onAxisMoved(const QString &systemPath, int axisIndex, int value);

private:
    static constexpr int kNumAxes = 8;
    static constexpr int kUnobservedMin = 65535;
    static constexpr int kUnobservedMax = 0;

    /// Owned by main.cpp (via ProfileEditorViewModel::profileManager()) -
    /// outlives this ViewModel.
    ProfileManager &m_profileManager;

    bool m_isCalibrating = false;
    QString m_currentSystemPath;

    std::array<int, kNumAxes> m_minObserved{};
    std::array<int, kNumAxes> m_maxObserved{};
};
