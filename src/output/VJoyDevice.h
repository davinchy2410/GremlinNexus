#pragma once

#include <cstdint>
#include <functional>

#include <QLibrary>

#include "IVirtualOutputDevice.h"

/**
 * @brief vJoy-backed virtual joystick.
 *
 * Talks to vJoyInterface.dll, resolved dynamically at runtime via QLibrary
 * so GremblingEx has no link-time dependency on the vJoy SDK being present.
 * If the DLL (or its exports, or the driver/device itself) is unavailable,
 * acquire() logs a warning and returns false instead of crashing; every
 * other call is then a safe no-op until a later acquire() succeeds.
 *
 * Supports vJoy's 8 primary axes (X, Y, Z, Rx, Ry, Rz, Slider, Dial), up to
 * 128 buttons and up to 4 POV hats, matching vJoy's JOYSTICK_POSITION_V2
 * report layout used by UpdateVJD.
 */
class VJoyDevice : public IVirtualOutputDevice
{
public:
    static constexpr int kMaxAxes = 8;
    static constexpr int kMaxButtons = 128;
    static constexpr int kMaxHats = 4;

    explicit VJoyDevice(unsigned int deviceId);
    ~VJoyDevice() override;

    VJoyDevice(const VJoyDevice &) = delete;
    VJoyDevice &operator=(const VJoyDevice &) = delete;

    bool acquire() override;
    void relinquish() override;
    void setAxis(int axisIndex, int value) override;
    void setButton(int buttonIndex, bool pressed) override;
    void setHat(int hatIndex, int value) override;
    void setHatDirection(int hatIndex, int direction, bool pressed) override;
    bool update() override;
    int deviceId() const override { return static_cast<int>(m_deviceId); }
    bool isViGEmDevice() const override { return false; }

    /// Registers a callback invoked every time setButton() actually flips a
    /// button's staged state (Fase 16 telemetry) - buttonIndex is 0-based,
    /// pressed is the new state. Shared across every VJoyDevice instance
    /// (there is one per distinct vJoy device ID, all cached by
    /// VirtualOutputManager) rather than per-instance, so main.cpp can wire
    /// this once to broadcast over PwaServer regardless of how many vJoy
    /// devices a profile ends up using. This class deliberately stays
    /// Qt-signal-free and network-agnostic - a plain std::function callback
    /// keeps the output layer from depending on Qt networking/PwaServer.
    static void setTelemetryCallback(std::function<void(unsigned int deviceId, int buttonIndex, bool pressed)> callback);

private:
    // vJoy C API function signatures (vJoyInterface.dll / public.h). vJoy's
    // API is declared __stdcall; on x64 this has no ABI effect but is kept
    // for documentation/source fidelity.
    using AcquireVJD_t = bool(__stdcall *)(unsigned int);
    using RelinquishVJD_t = void(__stdcall *)(unsigned int);
    using UpdateVJD_t = bool(__stdcall *)(unsigned int, void *);
    using ResetVJD_t = bool(__stdcall *)(unsigned int);

    /// Fase 20.3: how many of this device's POV hats are configured as
    /// *continuous* (angle-based) in vJoy's own config tool - the rest are
    /// discrete (ordinal 0-3/-1). Needed because a discrete hat rejects the
    /// angle values setHatDirection() would otherwise send it (see that
    /// method's own docs).
    using GetVJDContPovNumber_t = int(__stdcall *)(unsigned int);

    /// Binary layout of a vJoy report, matching the vJoy SDK's
    /// JOYSTICK_POSITION_V2 struct field-for-field: this is wire ABI shared
    /// with the driver via UpdateVJD, not just an API shape, so field
    /// order/types/count must not be changed without matching the SDK.
    struct JoystickPositionV2
    {
        uint8_t bDevice = 0; // 1-based device index.
        int32_t wThrottle = 0;
        int32_t wRudder = 0;
        int32_t wAileron = 0;
        int32_t wAxisX = 0;
        int32_t wAxisY = 0;
        int32_t wAxisZ = 0;
        int32_t wAxisXRot = 0;
        int32_t wAxisYRot = 0;
        int32_t wAxisZRot = 0;
        int32_t wSlider = 0;
        int32_t wDial = 0;
        int32_t wWheel = 0;
        int32_t wAxisVX = 0;
        int32_t wAxisVY = 0;
        int32_t wAxisVZ = 0;
        int32_t wAxisVBRX = 0;
        int32_t wAxisVBRY = 0;
        int32_t wAxisVBRZ = 0;
        int32_t lButtons = 0;    // Buttons 1-32, bit N-1 == button N.
        // Fase 20.8: 0xFFFFFFFF (neutral), not 0 - vJoy's own convention is
        // that 0 means "Up" (both continuous 0 degrees and discrete North),
        // so initializing these to 0 held every hat pressed Up from the
        // moment a device was acquired, until something else touched it.
        uint32_t bHats = 0xFFFFFFFF;      // Hat 0.
        uint32_t bHatsEx1 = 0xFFFFFFFF;   // Hat 1.
        uint32_t bHatsEx2 = 0xFFFFFFFF;   // Hat 2.
        uint32_t bHatsEx3 = 0xFFFFFFFF;   // Hat 3.
        int32_t lButtonsEx1 = 0; // Buttons 33-64.
        int32_t lButtonsEx2 = 0; // Buttons 65-96.
        int32_t lButtonsEx3 = 0; // Buttons 97-128.
    };

    /// Loads vJoyInterface.dll and resolves the functions this class needs,
    /// the first time it's called. Returns false (and logs via qWarning)
    /// without throwing if the DLL or any required export is missing.
    bool ensureLibraryLoaded();

    unsigned int m_deviceId;
    bool m_acquired = false;

    QLibrary m_library;
    AcquireVJD_t m_acquireVJD = nullptr;
    RelinquishVJD_t m_relinquishVJD = nullptr;
    UpdateVJD_t m_updateVJD = nullptr;
    ResetVJD_t m_resetVJD = nullptr;
    GetVJDContPovNumber_t m_getVJDContPovNumber = nullptr;

    JoystickPositionV2 m_state{};

    /// Per-hat bitmask of currently-held discrete directions (bit N == 1<<N
    /// for direction N, 0=Up/1=Right/2=Down/3=Left) - see setHatDirection().
    /// Kept separate from m_state's own bHats/bHatsEx*/... fields (which
    /// store the *combined angle* those functions feed into the report)
    /// since recovering "is Up currently held" from an angle alone is lossy
    /// (e.g. 4500 could be Up+Right release-ordering ambiguity) - this is
    /// the actual input state; m_state's angle fields are a derived value.
    uint8_t m_hatDirectionStates[4]{};

    static std::function<void(unsigned int, int, bool)> s_telemetryCallback;
};
