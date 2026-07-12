#include "HidHideController.h"
#include "ShutdownTrace.h"

#include <windows.h>
#include <winioctl.h>

namespace {

// ---------------------------------------------------------------------------
// HidHide control device IOCTLs, hand-defined (no public C++ header is
// shipped for these - HidHide's own DEVELOPER.md only documents the CTL_CODE
// formula, not a ready-to-include header). Cross-checked 2026-07-09 against
// the official managed wrapper (github.com/nefarius/Nefarius.Drivers.HidHide,
// src/HidHideControlService.cs) to confirm both the function numbers and
// that the buffer is a single BYTE (0/1), not a 4-byte BOOL - getting that
// buffer size wrong would have made every call below fail silently.
//
// CTL_CODE(DeviceType, Function, Method, Access):
//   DeviceType = 32769, Method = METHOD_BUFFERED (0), Access = FILE_READ_ACCESS (1)
//   for the entire IOCTL range HidHide defines (2048-2057), GET and SET alike.
// ---------------------------------------------------------------------------
constexpr DWORD kHidHideDeviceType = 32769;

constexpr DWORD ctlCode(DWORD function)
{
    return (kHidHideDeviceType << 16) | (FILE_READ_ACCESS << 14) | (function << 2) | METHOD_BUFFERED;
}

constexpr DWORD kIoctlGetActive = ctlCode(2052);
constexpr DWORD kIoctlSetActive = ctlCode(2053);

// Added to the driver in nefarius/HidHide PR #73 (merged 2022-05-31) -
// cross-checked 2026-07-12 against the same official managed wrapper
// (HidHideControlService.cs's IoctlGetWlInverse/IoctlSetWlInverse
// constants and its "IsAppListInverted" property, which confirms the
// 1-byte buffer convention here too) since no public C++ header ships
// these either, same situation as kIoctlGetActive/kIoctlSetActive above.
constexpr DWORD kIoctlGetWlInverse = ctlCode(2054);

constexpr wchar_t kControlDevicePath[] = L"\\\\.\\HidHide";

} // namespace

HidHideController &HidHideController::instance()
{
    static HidHideController s_instance;
    return s_instance;
}

HidHideController::HidHideController(QObject *parent)
    : QObject(parent)
{
}

void HidHideController::setCloakState(CloakState state)
{
    if (m_cloakState == state) {
        return;
    }
    m_cloakState = state;
    emit cloakStateChanged(state);
}

bool HidHideController::queryActive(bool &outActive) const
{
    HANDLE handle = CreateFileW(kControlDevicePath, GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        logShutdownTrace(QStringLiteral("HidHideController::queryActive: CreateFileW(%1) FAILED - GetLastError=%2")
                              .arg(QString::fromWCharArray(kControlDevicePath)).arg(GetLastError()));
        return false;
    }

    BYTE value = 0;
    DWORD bytesReturned = 0;
    const BOOL ok = DeviceIoControl(handle, kIoctlGetActive, nullptr, 0, &value, sizeof(value), &bytesReturned,
                                     nullptr);
    const DWORD lastError = ok ? 0 : GetLastError();
    CloseHandle(handle);

    if (!ok) {
        logShutdownTrace(QStringLiteral("HidHideController::queryActive: IOCTL_GET_ACTIVE FAILED - GetLastError=%1")
                              .arg(lastError));
        return false;
    }

    outActive = (value != 0);
    return true;
}

bool HidHideController::setActiveRaw(bool active) const
{
    HANDLE handle = CreateFileW(kControlDevicePath, GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        logShutdownTrace(QStringLiteral("HidHideController::setActiveRaw(%1): CreateFileW FAILED - GetLastError=%2")
                              .arg(active ? QStringLiteral("true") : QStringLiteral("false"))
                              .arg(GetLastError()));
        return false;
    }

    BYTE value = active ? 1 : 0;
    DWORD bytesReturned = 0;
    const BOOL ok = DeviceIoControl(handle, kIoctlSetActive, &value, sizeof(value), nullptr, 0, &bytesReturned,
                                     nullptr);
    const DWORD lastError = ok ? 0 : GetLastError();
    CloseHandle(handle);

    if (!ok) {
        logShutdownTrace(QStringLiteral("HidHideController::setActiveRaw(%1): IOCTL_SET_ACTIVE FAILED - GetLastError=%2")
                              .arg(active ? QStringLiteral("true") : QStringLiteral("false"))
                              .arg(lastError));
        return false;
    }
    return true;
}

bool HidHideController::isWhitelistInverseEnabled() const
{
    HANDLE handle = CreateFileW(kControlDevicePath, GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        logShutdownTrace(
            QStringLiteral("HidHideController::isWhitelistInverseEnabled: CreateFileW(%1) FAILED - GetLastError=%2")
                .arg(QString::fromWCharArray(kControlDevicePath))
                .arg(GetLastError()));
        return false;
    }

    BYTE value = 0;
    DWORD bytesReturned = 0;
    const BOOL ok = DeviceIoControl(handle, kIoctlGetWlInverse, nullptr, 0, &value, sizeof(value), &bytesReturned,
                                     nullptr);
    const DWORD lastError = ok ? 0 : GetLastError();
    CloseHandle(handle);

    if (!ok) {
        // Older HidHide driver builds predating PR #73 don't recognize this
        // IOCTL at all - failing closed (false, "not inverse") here is the
        // safe choice, since it just means main.cpp falls back to running
        // the normal deactivate/reactivate dance, exactly as it always did
        // before this method existed.
        logShutdownTrace(
            QStringLiteral("HidHideController::isWhitelistInverseEnabled: IOCTL_GET_WLINVERSE FAILED - GetLastError=%1")
                .arg(lastError));
        return false;
    }

    return value != 0;
}

bool HidHideController::deactivateCloak()
{
    if (!setActiveRaw(false)) {
        setCloakState(CloakState::Unavailable);
        return false;
    }

    bool active = true;
    if (!queryActive(active)) {
        // The SET above reported success but we can't confirm it - treat as
        // unknown rather than claiming Inactive we didn't actually verify.
        setCloakState(CloakState::Unknown);
        return false;
    }

    setCloakState(active ? CloakState::Active : CloakState::Inactive);
    return !active;
}

bool HidHideController::reactivateCloak()
{
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!setActiveRaw(true)) {
            setCloakState(CloakState::Unavailable);
            return false;
        }

        bool active = false;
        if (queryActive(active) && active) {
            setCloakState(CloakState::Active);
            return true;
        }

        logShutdownTrace(QStringLiteral(
            "HidHideController::reactivateCloak: attempt %1 - SET_ACTIVE(true) did not confirm as active")
                              .arg(attempt + 1));
    }

    // Both attempts failed to confirm - leave the state honestly reflecting
    // reality (devices are still exposed) instead of claiming success.
    bool active = false;
    setCloakState(queryActive(active) && active ? CloakState::Active : CloakState::Inactive);
    return m_cloakState == CloakState::Active;
}
