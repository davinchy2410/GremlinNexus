#pragma once

#include <cstdint>

#include <QMetaType>
#include <QString>
#include <QVector>

/**
 * @brief Lightweight, copyable descriptor for a physical input device
 *        (joystick / gamepad) catalogued by DeviceManager.
 *
 * Instances are passed by value across threads (worker -> GUI thread via
 * queued signal/slot connections), so this type intentionally holds no
 * pointers or references to shared state.
 */
struct DeviceInfo
{
    /// USB Vendor ID, formatted as a 4-digit hex string (e.g. "045E").
    QString vendorId;

    /// USB Product ID, formatted as a 4-digit hex string (e.g. "028E").
    QString productId;

    /// Human-readable device name as reported by the OS/driver.
    QString deviceName;

    /// OS-level device path/handle identifier, used as the unique key
    /// for tracking a device across add/remove notifications.
    QString systemPath;

    /// Whether the device is currently connected.
    bool isConnected = false;

    /// Number of analog axes exposed by the device.
    uint8_t numAxes = 0;

    /// Number of digital buttons exposed by the device, INCLUDING 4
    /// synthetic ones (Up/Right/Down/Left) per POV hat, appended after the
    /// device's real physical buttons (Fase 16.7 - see
    /// DeviceManager::parseHidReport()). A hat is no longer reported as a
    /// fake analog axis, so numButtons - numHats*4 is the real physical
    /// button count.
    uint8_t numButtons = 0;

    /// Number of POV hats exposed by the device. Each one contributes 4
    /// entries at the end of numButtons (see numButtons's own docs above) -
    /// not an axis, despite reading from the same HID value-caps as true
    /// axes do.
    uint8_t numHats = 0;

    /// Per-axis raw HID logical range (Fase 16.6), sized numAxes (true axes
    /// only - see numButtons's docs for where hats live instead) and
    /// indexed the same way as AxisEvent::axisIndex - straight from the
    /// device's own HIDP_VALUE_CAPS, since that varies per device (a 10-bit
    /// joystick reports very differently from a 16-bit one) and nothing
    /// upstream should have to guess it. Empty on a device this wasn't
    /// captured for (e.g. a mock/synthetic entry with no real HID
    /// descriptor) - callers should fall back to a sane default range
    /// rather than indexing into an empty vector.
    QVector<int> axisLogicalMin;
    QVector<int> axisLogicalMax;

    friend bool operator==(const DeviceInfo &lhs, const DeviceInfo &rhs)
    {
        return lhs.systemPath == rhs.systemPath;
    }

    friend bool operator!=(const DeviceInfo &lhs, const DeviceInfo &rhs)
    {
        return !(lhs == rhs);
    }
};

Q_DECLARE_METATYPE(DeviceInfo)

/**
 * @brief One analog axis value change for a given device.
 *
 * axisIndex is in [0, numAxes) - true analog axes only (Fase 16.7: a POV hat
 * is decoded into 4 synthetic ButtonEvents instead, see
 * DeviceManager::parseHidReport()), in HID value-caps order.
 */
struct AxisEvent
{
    QString systemPath;
    int axisIndex = 0;
    int value = 0;
};

/**
 * @brief One digital button state transition for a given device.
 */
struct ButtonEvent
{
    QString systemPath;
    int buttonIndex = 0;
    bool pressed = false;
};

Q_DECLARE_METATYPE(AxisEvent)
Q_DECLARE_METATYPE(ButtonEvent)
