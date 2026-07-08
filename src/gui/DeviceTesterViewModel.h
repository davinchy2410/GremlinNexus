#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

/**
 * @brief ViewModel for the "Device Tester" screen (Fase 10.5).
 *
 * A Virpil/VKB-style live hardware monitor: mirrors whichever device is
 * selected (currentSystemPath) from DeviceManager's axisMoved/buttonPressed
 * signals into two flat, QML-friendly arrays - axisValues (raw HID values,
 * clamped to [0, 65535], one entry per analog axis) and buttonStates (one
 * bool per digital button). QML polls these every animation frame to drive
 * the oscilloscope/button-matrix visuals; this class's own job is just to
 * keep them current with O(1) work per incoming signal, not to do any
 * drawing or history-buffering itself.
 *
 * Sized for up to kNumAxes analog axes and kNumButtons buttons - generous
 * upper bounds for any real joystick/HOTAS/pedal set. Events for indices
 * beyond that (or for a device other than currentSystemPath) are ignored
 * rather than grown into, since the QML grid/legend are laid out for a
 * fixed size.
 */
class DeviceTesterViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentSystemPath READ currentSystemPath WRITE setCurrentSystemPath NOTIFY currentSystemPathChanged)
    Q_PROPERTY(QVariantList axisValues READ axisValues NOTIFY axisValuesChanged)
    Q_PROPERTY(QVariantList buttonStates READ buttonStates NOTIFY buttonStatesChanged)
    Q_PROPERTY(QVariantList axisLogicalMin READ axisLogicalMin NOTIFY currentSystemPathChanged)
    Q_PROPERTY(QVariantList axisLogicalMax READ axisLogicalMax NOTIFY currentSystemPathChanged)
    Q_PROPERTY(int currentDeviceNumButtons READ currentDeviceNumButtons NOTIFY currentSystemPathChanged)
    Q_PROPERTY(int currentDeviceNumHats READ currentDeviceNumHats NOTIFY currentSystemPathChanged)
    Q_PROPERTY(QString currentDeviceLabel READ currentDeviceLabel NOTIFY currentSystemPathChanged)

public:
    explicit DeviceTesterViewModel(QObject *parent = nullptr);

    /// systemPath of the device currently being monitored - empty means
    /// "no device selected", in which case incoming axis/button signals
    /// never match and the arrays just stay at their last/zeroed values.
    QString currentSystemPath() const;
    void setCurrentSystemPath(const QString &systemPath);

    /// Raw HID axis values, one per axis in [0, kNumAxes), each clamped to
    /// [0, 65535]. Reset to all-zero whenever currentSystemPath changes.
    QVariantList axisValues() const;

    /// Digital button pressed-states, one per button in [0, kNumButtons).
    /// Reset to all-false whenever currentSystemPath changes.
    QVariantList buttonStates() const;

    /// Per-axis raw HID logical range (Fase 16.6) for whichever device
    /// currentSystemPath names, copied from its DeviceInfo::axisLogicalMin/
    /// Max (see DeviceManager::registerHidDevice()) the moment
    /// currentSystemPath changes - a HID device's native range varies (a
    /// 10-bit stick vs. a 16-bit one), so QML normalizes axisValues against
    /// *these*, not a hardcoded [0, 65535]. Falls back to [0, 65535] per
    /// axis for a device with no captured HID caps (a mock/synthetic entry).
    QVariantList axisLogicalMin() const;
    QVariantList axisLogicalMax() const;

    /// Total button count (including the 4 synthetic per-hat entries - see
    /// DeviceInfo::numButtons's own docs) and hat count of whichever device
    /// currentSystemPath names, captured the moment currentSystemPath
    /// changes. Both 0 for a device DeviceManager doesn't currently list
    /// (e.g. unplugged) or when no device is selected.
    int currentDeviceNumButtons() const;
    int currentDeviceNumHats() const;

    /// Sprint 2: human-readable identifier for whichever device
    /// currentSystemPath names - "vJoy N Output" for a vJoy virtual joystick
    /// (Windows/HidD_GetProductString reports every vJoy slot with the exact
    /// same product name, "vJoy Device", so plain deviceName alone can't
    /// tell slot 1 from slot 16 apart - N here is this device's 1-based
    /// ordinal position among just the vjoy-matching entries in
    /// DeviceManager::getConnectedDevices(), the same enumeration order
    /// ProfileEditorViewModel/OutputDeviceCombo already rely on being
    /// stable), or the physical device's own deviceName otherwise. Lets the
    /// Tester header badge make it unambiguous whether you're watching real
    /// hardware or confirming a ButtonRemapHandler/CurveHandler binding
    /// actually reaches its vJoy target. Empty currentSystemPath or one that
    /// no longer resolves to a connected device both read as a plain
    /// placeholder string rather than an empty badge.
    QString currentDeviceLabel() const;

signals:
    void currentSystemPathChanged();
    void axisValuesChanged();
    void buttonStatesChanged();

private slots:
    /// Relayed from DeviceManager::axisMoved; updates a single element of
    /// m_axisValues (O(1)) and re-emits axisValuesChanged when the event's
    /// systemPath matches currentSystemPath and axisIndex is in range.
    void onAxisMoved(const QString &systemPath, int axisIndex, int value);

    /// Relayed from DeviceManager::buttonPressed; same O(1) single-element
    /// update/notify shape as onAxisMoved().
    void onButtonPressed(const QString &systemPath, int buttonIndex, bool pressed);

private:
    static constexpr int kNumAxes = 8;
    static constexpr int kNumButtons = 256;

    QString m_currentSystemPath;
    QVariantList m_axisValues;
    QVariantList m_buttonStates;
    QVariantList m_axisLogicalMin;
    QVariantList m_axisLogicalMax;
    int m_currentDeviceNumButtons = 0;
    int m_currentDeviceNumHats = 0;
};
