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

    /// Queries IOCTL_GET_WLINVERSE - true if HidHide's whitelist is
    /// currently configured as "Inverse" (the Configuration Client's own
    /// "Inverse application cloak" checkbox: the list becomes a blacklist -
    /// every process EXCEPT the ones listed can see a blacklisted device -
    /// instead of the default "only listed processes can see it"
    /// whitelist). Read-only, unlike deactivateCloak()/reactivateCloak() -
    /// never changes driver state, and doesn't touch cloakState()/
    /// cloakStateChanged() (an orthogonal setting, not a snapshot of the
    /// same "is cloaking on right now" question those track). Returns false
    /// if HidHide isn't installed or the query otherwise fails - same
    /// fail-open convention as every other query here.
    ///
    /// Why this matters: main.cpp's whole deactivateCloak()/reactivateCloak()
    /// dance around startup only exists because HidHide's *default* whitelist
    /// mode can permanently deny a process access to a device if that
    /// process's first-ever open of it happens while the cloak is already
    /// active (see class docs above). In Inverse mode that race structurally
    /// can't happen - the internal Windows "System" process (and this app,
    /// since it's never blacklisted) always gets through regardless of
    /// cloak timing - so a user who's switched to Inverse mode pays for a
    /// startup dance that protects against a bug their own configuration
    /// already prevents. main.cpp queries this once at startup to skip that
    /// dance automatically when it's confirmed unnecessary.
    bool isWhitelistInverseEnabled() const;

    /// Read-only status check - unlike deactivateCloak()/reactivateCloak(),
    /// never issues IOCTL_SET_ACTIVE, so it's safe to call anytime (e.g.
    /// from the Settings screen's Diagnostics panel) without side effects
    /// on a cloak state the user may have deliberately left as-is.
    /// Necessary because cloakState() is otherwise only ever populated by
    /// main.cpp's one-time startup dance - which main.cpp skips entirely
    /// when hidHideAutoCloakEnabled is off (see SettingsViewModel's own
    /// docs), leaving cloakState() stuck at Unknown for the rest of the
    /// session with nothing else to resolve it. Sets cloakState() to
    /// Unavailable (not left at Unknown) if the query itself fails - same
    /// fail-open convention as isWhitelistInverseEnabled().
    Q_INVOKABLE bool queryCloakStateOnly();

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
