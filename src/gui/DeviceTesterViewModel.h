#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include "DeviceInfo.h"

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
    /// Relayed from DeviceManager::deviceAdded - fires both for a brand new
    /// device AND for a re-registration of an already-known one (e.g. the
    /// startup warmup rescans, or any later rescan that resolves a name/
    /// button-count DeviceManager only got partially right the first time
    /// around - see DeviceManager::addOrUpdateDevice()). Refreshes this
    /// Tester's own cached numButtons/numHats/axisLogicalMin/axisLogicalMax/
    /// label snapshot when it's the CURRENTLY selected device, since
    /// setCurrentSystemPath() below only ever captures that snapshot once,
    /// at selection time, and has no other way of finding out it went
    /// stale. Confirmed as the real cause (2026-07-20) of "device shows as
    /// Unknown/0 buttons until you reselect it" - reselecting only "fixed"
    /// it by accident, by re-running setCurrentSystemPath()'s own capture
    /// against DeviceManager's by-then-corrected data.
    void onDeviceInfoUpdated(const DeviceInfo &device);

    /// Relayed from DeviceManager::axisMoved; updates a single element of
    /// m_axisValues (O(1)) and sets ONLY m_axesDirty for the next
    /// m_uiThrottleTimer tick to notify - see that timer's own docs for why
    /// this no longer emits axisValuesChanged() directly, and why axes and
    /// buttons are tracked as two independent flags rather than one.
    void onAxisMoved(const QString &systemPath, int axisIndex, int value);

    /// Relayed from DeviceManager::buttonPressed; same O(1) single-element
    /// update shape as onAxisMoved(), but sets ONLY m_buttonsDirty.
    void onButtonPressed(const QString &systemPath, int buttonIndex, bool pressed);

    /// m_uiThrottleTimer's own slot (see its docs) - emits axisValuesChanged/
    /// buttonStatesChanged at most once per tick, only for whichever array
    /// actually changed since the last one.
    void flushPendingUiUpdates();

private:
    /// Shared by setCurrentSystemPath() and onDeviceInfoUpdated() - looks
    /// m_currentSystemPath up in DeviceManager::getConnectedDevices() and
    /// (re)captures m_axisLogicalMin/Max/m_currentDeviceNumButtons/Hats from
    /// whatever it finds (or the [0,65535]/0/0 defaults if not found at
    /// all). Does not touch m_axisValues/m_buttonStates or emit any
    /// signals itself - callers decide what else needs resetting/notifying
    /// around it (a fresh selection clears live readings too; a metadata-
    /// only refresh must not, or it would stomp readings that arrived
    /// between the stale registration and this one correcting it).
    void refreshDeviceMetadataFromManager();

    static constexpr int kNumAxes = 8;
    static constexpr int kNumButtons = 256;

    QString m_currentSystemPath;
    QVariantList m_axisValues;
    QVariantList m_buttonStates;
    QVariantList m_axisLogicalMin;
    QVariantList m_axisLogicalMax;
    int m_currentDeviceNumButtons = 0;
    int m_currentDeviceNumHats = 0;

    /// Coalesces onAxisMoved()/onButtonPressed() into at most one
    /// axisValuesChanged()/buttonStatesChanged() emission per tick (4 ms /
    /// 250 Hz, matching DeviceMonitorWorker's own axisMoved() flush cadence
    /// - see kAxisFlushIntervalMs's docs - so a 240 Hz+ monitor never sees a
    /// visibly stepped radar dot) instead of one per relayed hardware
    /// event. Buttons are deliberately unthrottled at the DeviceManager
    /// layer (real transitions, not a flood) - but every relayed event
    /// still made this ViewModel force the whole Tester UI (radar dot, axis
    /// bars, 256-button grid) to re-read and re-render its full
    /// QVariantList. m_axesDirty/m_buttonsDirty are tracked independently
    /// (not one combined flag) specifically so moving the stick alone never
    /// forces the O(256) button GridView to re-scan buttonStates, and
    /// vice versa. This does not affect routing/output latency at all -
    /// only how often the *Tester screen's own display* repaints.
    QTimer *m_uiThrottleTimer = nullptr;
    bool m_axesDirty = false;
    bool m_buttonsDirty = false;
};
