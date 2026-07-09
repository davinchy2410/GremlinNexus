#pragma once

#include <QObject>

/// Direct, in-process integration with HidHide's own official control
/// device (\\.\HidHide + DeviceIoControl, documented in HidHide's
/// DEVELOPER.md) - NOT a HidHideCLI.exe subprocess. That distinction
/// matters: HidHideCLI.exe was confirmed (2026-07-09 investigation) to be
/// able to hang without exiting, which is exactly why GremblingNexus's old
/// CLI-based HidHide integration was removed. This class never spawns a
/// process at all - it's a synchronous CreateFile+DeviceIoControl call
/// pair, the same pattern queryProductName() already uses safely in
/// DeviceManager.cpp.
///
/// What this exists to fix: HidHide's whitelist only reliably grants a
/// process access to a cloaked device if that process's FIRST open of the
/// device happens while the cloak is inactive. A process that starts with
/// the cloak already active (the normal case - the user configures
/// cloaking once, persistently, via HidHide's own Configuration Client and
/// leaves it on) can get permanently denied access to that device until
/// its driver stack is rebuilt (disable/enable, or unplug/replug) - a
/// HidHide-side bug, reproduced and confirmed against the real driver
/// during the same investigation, not something in GremblingNexus's own
/// RawInput code. main.cpp calls deactivateCloak() before
/// DeviceManager::initialize() ever touches a device, then
/// reactivateCloak() once the startup device scan has had time to
/// complete, so every device this process ever opens is opened for the
/// first time while uncloaked - mirroring the manual recovery sequence
/// that was confirmed by hand to avoid the bug entirely.
class HidHideController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(CloakState cloakState READ cloakState NOTIFY cloakStateChanged)

public:
    /// Unknown: not yet queried this run.
    /// Unavailable: \\.\HidHide could not be opened at all (HidHide not
    ///     installed, or another handle is holding the control device -
    ///     see class docs on HidHide's "only one handle at a time" limit).
    /// Inactive: queried/set successfully - cloak is OFF, every device is
    ///     visible to every process on the system right now.
    /// Active: queried/set successfully - cloak is ON, blacklisted devices
    ///     are hidden from every non-whitelisted process as configured.
    enum class CloakState {
        Unknown,
        Unavailable,
        Inactive,
        Active,
    };
    Q_ENUM(CloakState)

    static HidHideController &instance();

    CloakState cloakState() const { return m_cloakState; }

    /// Issues IOCTL_SET_ACTIVE(FALSE). Safe to call even if HidHide isn't
    /// installed on this machine - just leaves/sets cloakState() to
    /// Unavailable and returns false, no side effects, nothing thrown.
    bool deactivateCloak();

    /// Issues IOCTL_SET_ACTIVE(TRUE), then re-queries IOCTL_GET_ACTIVE to
    /// confirm it actually took effect before trusting it - a fire-and-
    /// forget SET here isn't good enough, because leaving the cloak
    /// silently off for an entire session (device stays exposed to every
    /// other process) is exactly the failure mode this whole mechanism is
    /// meant to avoid causing. Retries once on a confirmed mismatch;
    /// cloakState() ends this call as either Active (confirmed) or
    /// Inactive (confirmed still off after the retry) so the UI can show
    /// the real state either way.
    bool reactivateCloak();

signals:
    void cloakStateChanged(CloakState state);

private:
    explicit HidHideController(QObject *parent = nullptr);

    /// Opens \\.\HidHide for the duration of a single IOCTL call and
    /// closes it immediately after - see class docs: HidHide's control
    /// device only accepts one open handle at a time system-wide, so
    /// holding it open any longer than one call needs would block the
    /// Configuration Client, HidHideWatchdog, or any other tool from
    /// touching it for as long as we held it.
    bool queryActive(bool &outActive) const;
    bool setActiveRaw(bool active) const;

    void setCloakState(CloakState state);

    CloakState m_cloakState = CloakState::Unknown;
};
