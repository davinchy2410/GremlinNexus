#pragma once

/**
 * @brief Abstract interface for a virtual joystick/gamepad output backend.
 *
 * Implementations translate staged axis/button/hat state into whatever the
 * underlying virtual-HID driver (vJoy, ViGEm, ...) expects and commit it to
 * the OS in a single batched call via update(), so that the hot path for a
 * processing/routing thread is: setAxis/setButton/setHat (cheap, in-memory)
 * followed by one update() per frame (the only call that talks to the driver).
 */
class IVirtualOutputDevice
{
public:
    virtual ~IVirtualOutputDevice() = default;

    /// Takes exclusive control of the underlying virtual device. Must
    /// succeed before setAxis/setButton/setHat/update have any effect.
    virtual bool acquire() = 0;

    /// Releases control of the underlying virtual device.
    virtual void relinquish() = 0;

    /// Stages an analog axis value; only applied to the driver on the next update().
    virtual void setAxis(int axisIndex, int value) = 0;

    /// Stages a digital button state; only applied to the driver on the next update().
    virtual void setButton(int buttonIndex, bool pressed) = 0;

    /// Stages a POV hat value; only applied to the driver on the next update().
    virtual void setHat(int hatIndex, int value) = 0;

    /// Stages one discrete direction of a POV hat (Fase 19) - a higher-level
    /// counterpart to setHat() for handlers driven by discrete physical
    /// button events (e.g. a real hat's own 4 synthetic Up/Right/Down/Left
    /// buttons - see HatRemapHandler) rather than a single continuous angle.
    /// hatIndex is [0, 4); direction is 0=Up, 1=Right, 2=Down, 3=Left.
    /// Implementations combine whichever directions are currently pressed
    /// into the equivalent 8-way (or centered) angle internally; only
    /// applied to the driver on the next update().
    virtual void setHatDirection(int hatIndex, int direction, bool pressed) = 0;

    /// Flushes all staged axis/button/hat state to the OS driver as a single
    /// packaged report. Returns false if the underlying driver call fails
    /// (including "not acquired").
    virtual bool update() = 0;

    /// Numeric identifier of this device within its backend, e.g. vJoy's
    /// [1, 16] device ID (see VJoyDevice) - the same value a profile JSON's
    /// "targetOutputId" binding field names. Backends with no equivalent
    /// concept (e.g. ViGEmDevice, which only ever creates a single target
    /// per instance) return 0. Exists so a live IActionHandler can describe
    /// which output device it targets when serializing itself back to JSON
    /// (see IActionHandler::toJson(), Fase 10.8) without needing to store
    /// its own redundant copy of an ID the device already knows.
    virtual int deviceId() const = 0;

    /// True if this device is a ViGEmBus-backed virtual Xbox 360 controller
    /// (see ViGEmDevice) rather than a vJoy device (see VJoyDevice). Lets a
    /// live IActionHandler's toJson() write the correct profile JSON
    /// "targetDeviceType" ("vjoy" vs "vigem") alongside deviceId() without
    /// hardcoding a per-backend type-check into every handler.
    virtual bool isViGEmDevice() const = 0;
};
