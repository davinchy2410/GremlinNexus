#include "VJoyDevice.h"

#include <QDebug>

std::function<void(unsigned int, int, bool)> VJoyDevice::s_telemetryCallback;

void VJoyDevice::setTelemetryCallback(std::function<void(unsigned int, int, bool)> callback)
{
    s_telemetryCallback = std::move(callback);
}

VJoyDevice::VJoyDevice(unsigned int deviceId)
    : m_deviceId(deviceId)
    , m_library(QStringLiteral("vJoyInterface"))
{
    if (deviceId < 1 || deviceId > 16) {
        qWarning() << "VJoyDevice: id" << deviceId << "is outside vJoy's valid range [1, 16]";
    }
    m_state.bDevice = static_cast<uint8_t>(deviceId);
}

VJoyDevice::~VJoyDevice()
{
    relinquish();
}

bool VJoyDevice::ensureLibraryLoaded()
{
    if (m_library.isLoaded()) {
        return true;
    }

    if (!m_library.load()) {
        qWarning() << "VJoyDevice: failed to load vJoyInterface.dll:" << m_library.errorString();
        return false;
    }

    m_acquireVJD = reinterpret_cast<AcquireVJD_t>(m_library.resolve("AcquireVJD"));
    m_relinquishVJD = reinterpret_cast<RelinquishVJD_t>(m_library.resolve("RelinquishVJD"));
    m_updateVJD = reinterpret_cast<UpdateVJD_t>(m_library.resolve("UpdateVJD"));
    m_resetVJD = reinterpret_cast<ResetVJD_t>(m_library.resolve("ResetVJD"));
    // Fase 20.3/20.8: optional - not in the required-exports check below,
    // since older/lite vJoyInterface.dll builds that lack it just fall back
    // to treating every hat as continuous, vJoy's modern default (see
    // setHatDirection()).
    m_getVJDContPovNumber = reinterpret_cast<GetVJDContPovNumber_t>(m_library.resolve("GetVJDContPovNumber"));

    if (!m_acquireVJD || !m_relinquishVJD || !m_updateVJD || !m_resetVJD) {
        qWarning() << "VJoyDevice: vJoyInterface.dll is missing one or more required exports"
                       " (AcquireVJD/RelinquishVJD/UpdateVJD/ResetVJD)";
        m_library.unload();
        m_acquireVJD = nullptr;
        m_relinquishVJD = nullptr;
        m_updateVJD = nullptr;
        m_resetVJD = nullptr;
        m_getVJDContPovNumber = nullptr;
        return false;
    }

    return true;
}

bool VJoyDevice::acquire()
{
    if (m_acquired) {
        return true;
    }

    if (!ensureLibraryLoaded()) {
        return false;
    }

    if (!m_acquireVJD(m_deviceId)) {
        qWarning() << "VJoyDevice: AcquireVJD failed for device" << m_deviceId
                   << "(vJoy driver not running, device not enabled, or already owned elsewhere?)";
        return false;
    }

    m_resetVJD(m_deviceId);
    m_acquired = true;
    return true;
}

void VJoyDevice::relinquish()
{
    if (m_acquired && m_relinquishVJD) {
        m_relinquishVJD(m_deviceId);
    }
    m_acquired = false;
}

void VJoyDevice::setAxis(int axisIndex, int value)
{
    switch (axisIndex) {
    case 0: m_state.wAxisX = value; break;
    case 1: m_state.wAxisY = value; break;
    case 2: m_state.wAxisZ = value; break;
    case 3: m_state.wAxisXRot = value; break;
    case 4: m_state.wAxisYRot = value; break;
    case 5: m_state.wAxisZRot = value; break;
    case 6: m_state.wSlider = value; break;
    case 7: m_state.wDial = value; break;
    default:
        qWarning() << "VJoyDevice: axisIndex" << axisIndex << "out of range [0," << kMaxAxes << ")";
        break;
    }
}

void VJoyDevice::setButton(int buttonIndex, bool pressed)
{
    if (buttonIndex < 0 || buttonIndex >= kMaxButtons) {
        qWarning() << "VJoyDevice: buttonIndex" << buttonIndex << "out of range [0," << kMaxButtons << ")";
        return;
    }

    int32_t *field = nullptr;
    int bit = buttonIndex;
    if (buttonIndex < 32) {
        field = &m_state.lButtons;
    } else if (buttonIndex < 64) {
        field = &m_state.lButtonsEx1;
        bit -= 32;
    } else if (buttonIndex < 96) {
        field = &m_state.lButtonsEx2;
        bit -= 64;
    } else {
        field = &m_state.lButtonsEx3;
        bit -= 96;
    }

    const int32_t mask = static_cast<int32_t>(1u << bit);
    const bool wasPressed = (*field & mask) != 0;
    if (pressed) {
        *field |= mask;
    } else {
        *field &= ~mask;
    }

    // Fase 16: only broadcasts on an actual flip, not every call (handlers
    // may re-set the same state repeatedly, e.g. a held momentary button
    // across several ticks) - so a PWA `indicator` control's own toggle
    // fires exactly once per physical press/release, not once per tick.
    if (wasPressed != pressed && s_telemetryCallback) {
        s_telemetryCallback(m_deviceId, buttonIndex, pressed);
    }
}

void VJoyDevice::setHat(int hatIndex, int value)
{
    if (hatIndex < 0 || hatIndex >= kMaxHats) {
        qWarning() << "VJoyDevice: hatIndex" << hatIndex << "out of range [0," << kMaxHats << ")";
        return;
    }

    // Fase 20.14: Fase 20.8 tried to infer discrete-vs-continuous from
    // value's own range (small values 0-3/-1 = discrete, large angles =
    // continuous), but that's not actually decidable from the value alone -
    // a continuous hat's "Up" is 0 and its "centered" is -1, both squarely
    // inside the "discrete" range that guess assumed, so a continuous hat's
    // Up/neutral got packed into the wrong bits (a nibble = 0x...F) instead
    // of written as a full DWORD, leaving it stuck. Ask vJoy directly - same
    // GetVJDContPovNumber query setHatDirection() already uses - instead of
    // guessing from the number itself.
    bool isContinuous = true;
    if (m_getVJDContPovNumber) {
        if (m_getVJDContPovNumber(m_deviceId) == 0) {
            isContinuous = false;
        }
    }

    if (!isContinuous) {
        // Discrete hats pack all 4 into nibbles within this single bHats
        // field (POV1 = bits 0-3, POV2 = bits 4-7, ...); bHatsEx1/2/3 are
        // ignored entirely for a discrete device.
        const uint32_t nibble = (value == -1) ? 0xFu : static_cast<uint32_t>(value);
        const int shift = hatIndex * 4;
        const uint32_t mask = ~(0xFu << shift);

        m_state.bHats = (m_state.bHats & mask) | (nibble << shift);
    } else {
        // Continuous hats use one full DWORD per hat; -1 (neutral) must be
        // written as 0xFFFFFFFF, not passed through as-is.
        const uint32_t finalValue = (value == -1) ? 0xFFFFFFFF : static_cast<uint32_t>(value);

        switch (hatIndex) {
        case 0: m_state.bHats = finalValue; break;
        case 1: m_state.bHatsEx1 = finalValue; break;
        case 2: m_state.bHatsEx2 = finalValue; break;
        case 3: m_state.bHatsEx3 = finalValue; break;
        default: break;
        }
    }
}

void VJoyDevice::setHatDirection(int hatIndex, int direction, bool pressed)
{
    if (hatIndex < 0 || hatIndex >= kMaxHats) {
        qWarning() << "VJoyDevice: hatIndex" << hatIndex << "out of range [0," << kMaxHats << ")";
        return;
    }
    if (direction < 0 || direction > 3) {
        qWarning() << "VJoyDevice: direction" << direction << "out of range [0, 3] (0=Up, 1=Right, 2=Down, 3=Left)";
        return;
    }

    const uint8_t bit = static_cast<uint8_t>(1u << direction);
    if (pressed) {
        m_hatDirectionStates[hatIndex] |= bit;
    } else {
        m_hatDirectionStates[hatIndex] &= static_cast<uint8_t>(~bit);
    }

    // Combines whichever directions are currently held into vJoy's own
    // 1/100-degree POV angle. Any bitmask not listed below - both bits of
    // an opposite pair held (Up+Down, Left+Right), 3+ directions at once,
    // or none - is not a real physical hat position, so it's treated the
    // same as centered.
    int angle;
    switch (m_hatDirectionStates[hatIndex]) {
    case 0b0001: angle = 0;     break; // Up
    case 0b0011: angle = 4500;  break; // Up+Right
    case 0b0010: angle = 9000;  break; // Right
    case 0b0110: angle = 13500; break; // Right+Down
    case 0b0100: angle = 18000; break; // Down
    case 0b1100: angle = 22500; break; // Down+Left
    case 0b1000: angle = 27000; break; // Left
    case 0b1001: angle = 31500; break; // Up+Left
    default:      angle = -1;    break; // Centered
    }

    // Fase 20.8: assume continuous by default - GetVJDContPovNumber missing
    // (older vJoyInterface.dll) used to fall back to discrete, but a truly
    // continuous device fed discrete values 1/2/3 (0.01/0.02/0.03 degrees),
    // which every game reads as "Up" - the same symptom as a discrete device
    // fed continuous angles. Continuous is vJoy's modern default, so only
    // treat a hat as discrete when GetVJDContPovNumber positively confirms 0.
    bool isContinuous = true;
    if (m_getVJDContPovNumber) {
        if (m_getVJDContPovNumber(m_deviceId) == 0) {
            isContinuous = false;
        }
    }

    if (!isContinuous) {
        // Discrete mapping - only the 4 cardinal directions or centered are
        // representable at all.
        switch (angle) {
        case 0:     angle = 0; break; // Up
        case 9000:  angle = 1; break; // Right
        case 18000: angle = 2; break; // Down
        case 27000: angle = 3; break; // Left
        default:    angle = -1; break; // Diagonals and centered both become released in discrete mode.
        }
    }
    setHat(hatIndex, angle);
}

bool VJoyDevice::update()
{
    if (!m_acquired || !m_updateVJD) {
        return false;
    }
    return m_updateVJD(m_deviceId, &m_state);
}
