#include "VJoyDevice.h"
#include "ShutdownTrace.h"

#include <QDateTime>
#include <QDebug>
#include <QLoggingCategory>
#include <QString>

Q_LOGGING_CATEGORY(lcVJoy, "grembling.output.vjoy")

std::function<void(unsigned int, int, bool)> VJoyDevice::s_telemetryCallback;

void VJoyDevice::setTelemetryCallback(std::function<void(unsigned int, int, bool)> callback)
{
    s_telemetryCallback = std::move(callback);
}

void VJoyDevice::clearTelemetryCallback()
{
    s_telemetryCallback = nullptr;
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
    // Diagnostic-only, same as GetVJDContPovNumber above - not required for
    // acquire()/update() to function, only for logUpdateFailureReason() to
    // explain *why* a later UpdateVJD() call was rejected.
    m_getVJDStatus = reinterpret_cast<GetVJDStatus_t>(m_library.resolve("GetVJDStatus"));

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

    // Fase (bugfix): ResetVJD() only resets the DRIVER's own state, not
    // m_state (this instance's in-memory report) - m_state's axis fields
    // default to plain 0 (JoystickPositionV2's own in-class initializers),
    // which vJoy's [0, 32767] unsigned axis range reads as the extreme
    // minimum, not centered. Every freshly-acquired device therefore sat at
    // one corner (top-left in most games/testers) until the user's first
    // physical stick movement happened to overwrite it. Centering here
    // (16383 - see TrimHandler.h's own "vJoy's own axis center" convention,
    // kept consistent rather than introducing a second, off-by-one
    // definition of "center") and pushing it immediately via update() below
    // means a freshly-acquired device already reads centered before any
    // physical input arrives - on both a first-ever acquire() and a
    // re-acquire() after a prior relinquish() (m_state is never reset
    // anywhere else, so a stale non-centered value would otherwise survive
    // a relinquish/re-acquire cycle too).
    static constexpr int32_t kVJoyAxisCenter = 16383;
    m_state.wAxisX = kVJoyAxisCenter;
    m_state.wAxisY = kVJoyAxisCenter;
    m_state.wAxisZ = kVJoyAxisCenter;
    m_state.wAxisXRot = kVJoyAxisCenter;
    m_state.wAxisYRot = kVJoyAxisCenter;
    m_state.wAxisZRot = kVJoyAxisCenter;
    m_state.wSlider = kVJoyAxisCenter;
    m_state.wDial = kVJoyAxisCenter;

    m_acquired = true;
    // The centering writes above went straight to m_state's fields, not
    // through setAxis() - the only place that normally sets m_dirty - so it
    // has to be forced here or update() below (now dirty-gated) would
    // silently skip pushing the just-centered state, resurrecting the
    // "sits in one corner until the first physical movement" bug this same
    // centering exists to fix.
    m_dirty = true;
    update();
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
    int32_t *field = nullptr;
    switch (axisIndex) {
    case 0: field = &m_state.wAxisX; break;
    case 1: field = &m_state.wAxisY; break;
    case 2: field = &m_state.wAxisZ; break;
    case 3: field = &m_state.wAxisXRot; break;
    case 4: field = &m_state.wAxisYRot; break;
    case 5: field = &m_state.wAxisZRot; break;
    case 6: field = &m_state.wSlider; break;
    case 7: field = &m_state.wDial; break;
    default:
        qWarning() << "VJoyDevice: axisIndex" << axisIndex << "out of range [0," << kMaxAxes << ")";
        return;
    }
    if (*field != value) {
        *field = value;
        m_dirty = true;
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
    // Same flip check now also gates m_dirty (see its own docs).
    if (wasPressed != pressed) {
        m_dirty = true;
        if (s_telemetryCallback) {
            s_telemetryCallback(m_deviceId, buttonIndex, pressed);
        }
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

        const uint32_t newBHats = (m_state.bHats & mask) | (nibble << shift);
        if (m_state.bHats != newBHats) {
            m_state.bHats = newBHats;
            m_dirty = true;
        }
    } else {
        // Continuous hats use one full DWORD per hat; -1 (neutral) must be
        // written as 0xFFFFFFFF, not passed through as-is.
        const uint32_t finalValue = (value == -1) ? 0xFFFFFFFF : static_cast<uint32_t>(value);

        uint32_t *field = nullptr;
        switch (hatIndex) {
        case 0: field = &m_state.bHats; break;
        case 1: field = &m_state.bHatsEx1; break;
        case 2: field = &m_state.bHatsEx2; break;
        case 3: field = &m_state.bHatsEx3; break;
        default: break;
        }
        if (field && *field != finalValue) {
            *field = finalValue;
            m_dirty = true;
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

void VJoyDevice::logUpdateFailureReason() const
{
    // Also routed through logShutdownTrace() (plain-text file under
    // %TEMP%, same one DeviceManager/HidHideController already use), not
    // just qCCritical: this bug takes ~2h of active play to surface, so
    // whoever hits it is watching the game, not this app's in-memory Log
    // Console (QStringListModel, capped at LogModel::kMaxLines=2000 lines -
    // long gone from a live console's scrollback and from memory outright
    // the moment the process exits) - a durable, after-the-fact-checkable
    // record is the whole point here.
    QString reason;
    if (!m_getVJDStatus) {
        reason = QStringLiteral("and GetVJDStatus is unavailable (vJoyInterface.dll export missing)"
                                 " - cannot determine why the driver rejected the report");
    } else {
        const int status = m_getVJDStatus(m_deviceId);
        switch (status) {
        case VJD_STAT_OWN:
            reason = QStringLiteral("despite VJD_STAT_OWN (still owned by us) - driver rejected the"
                                     " report itself, not a handle/ownership loss");
            break;
        case VJD_STAT_FREE:
            reason = QStringLiteral("- status is VJD_STAT_FREE, we lost ownership silently"
                                     " (device was released without relinquish() being called)");
            break;
        case VJD_STAT_BUSY:
            reason = QStringLiteral("- status is VJD_STAT_BUSY, another application has taken"
                                     " ownership of this device ID");
            break;
        case VJD_STAT_MISS:
            reason = QStringLiteral("- status is VJD_STAT_MISS, the device is no longer installed"
                                     " or has been disabled in vJoy's configuration");
            break;
        default:
            reason = QStringLiteral("- GetVJDStatus returned unrecognized/unknown status %1").arg(status);
            break;
        }
    }

    const QString message = QStringLiteral("VJoyDevice: UpdateVJD failed for device %1 %2").arg(m_deviceId).arg(reason);
    qCCritical(lcVJoy).noquote() << message;
    logShutdownTrace(message);
}

bool VJoyDevice::update()
{
    if (!m_updateVJD) {
        return false;
    }

    if (!m_acquired) {
        // Ownership was lost (see this file's own docs on the sleep/resume
        // bug that motivated this) - retry acquire() periodically instead
        // of just returning false forever. Rate-limited (kReacquireIntervalMs)
        // rather than every 5ms tick, since a still-unavailable driver would
        // otherwise spam AcquireVJD calls just as badly as the original bug
        // spammed UpdateVJD ones.
        const int64_t now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastReacquireAttemptMs < kReacquireIntervalMs) {
            return false;
        }
        m_lastReacquireAttemptMs = now;
        if (!acquire()) {
            return false;
        }
        qCInfo(lcVJoy) << "VJoyDevice: re-acquired device" << m_deviceId << "after losing ownership";
        logShutdownTrace(QStringLiteral("VJoyDevice: re-acquired device %1 after losing ownership").arg(m_deviceId));
        return true; // acquire() already pushed one fresh update() internally.
    }

    // Skip the driver call entirely on a tick where m_state hasn't actually
    // changed since the last successful push - setAxis()/setButton()/
    // setHat() only set m_dirty on a real flip (see its own docs), so an
    // idle device (or one only staging values a handler already staged
    // last tick) no longer calls UpdateVJD() 200 times/sec with
    // byte-identical data. Purely a CPU/driver-call-count saving; NOT
    // related to the silent-disconnect (VJD_STAT_FREE) bug below, which
    // this file's own history traces to the driver not surviving a PC
    // sleep/resume cycle, not to call frequency.
    if (!m_dirty) {
        return true;
    }

    // Fase (bugfix): the silent-disconnect bug - vJoy quietly stops
    // accepting reports after prolonged use, with no crash and no visible
    // symptom on the RawInput/EventRouter side (this is purely the outbound
    // half) - was invisible because UpdateVJD()'s boolean return was never
    // checked. GetVJDStatus() is only queried on failure (not every tick) so
    // the healthy path stays as cheap as before.
    const bool ok = m_updateVJD(m_deviceId, &m_state);
    if (ok) {
        // Cleared only on success - a failed push leaves m_dirty set so the
        // very next tick retries pushing this same state instead of the
        // change silently being dropped.
        m_dirty = false;
    }
    if (!ok) {
        ++m_failuresSinceLastLog;
        const int64_t now = QDateTime::currentMSecsSinceEpoch();
        const bool firstFailureInStreak = !m_isCurrentlyFailing;
        m_isCurrentlyFailing = true;

        // Rate-limited: this used to log every single failed tick (200/sec)
        // unconditionally - during the sleep/resume bug this produced a
        // 5.7GB log file over one ~9h unattended session. Still logs
        // immediately on the first failure of a new streak (so a brief
        // one-off blip is never silent), then at most once per
        // kFailureLogIntervalMs while it persists.
        if (firstFailureInStreak || now - m_lastFailureLogMs >= kFailureLogIntervalMs) {
            const QString message = QStringLiteral("VJoyDevice: UpdateVJD rejected the report for device %1 (%2 failure(s) since last log)")
                                          .arg(m_deviceId)
                                          .arg(m_failuresSinceLastLog);
            qCCritical(lcVJoy).noquote() << message;
            logShutdownTrace(message);
            logUpdateFailureReason();
            m_lastFailureLogMs = now;
            m_failuresSinceLastLog = 0;
        }

        // VJD_STAT_FREE specifically means the driver no longer considers
        // us the owner (see this file's docs) - flip m_acquired so the next
        // tick takes the re-acquire path above instead of continuing to
        // call an UpdateVJD that's already known to keep failing.
        if (m_getVJDStatus && m_getVJDStatus(m_deviceId) == VJD_STAT_FREE) {
            m_acquired = false;
        }
    } else if (m_isCurrentlyFailing) {
        m_isCurrentlyFailing = false;
        m_failuresSinceLastLog = 0;
        qCInfo(lcVJoy) << "VJoyDevice: device" << m_deviceId << "resumed accepting updates";
        logShutdownTrace(QStringLiteral("VJoyDevice: device %1 resumed accepting updates").arg(m_deviceId));
    }
    return ok;
}
