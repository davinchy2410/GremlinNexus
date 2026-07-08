#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

/**
 * @brief Singleton wrapper around Nefarius' HidHideCLI.exe (Fase 11).
 *
 * HidHide is an external, separately-installed Windows driver/utility that
 * can hide ("cloak") a physical HID device from every application except
 * the ones on its whitelist - used here so a device GremblingEx has
 * remapped to a vJoy output can be hidden from games that would otherwise
 * see both the raw physical device AND the vJoy one. Deliberately shells
 * out to HidHideCLI.exe (QProcess) rather than talking to the HidHide
 * kernel driver's IOCTL interface directly - the CLI is the officially
 * supported integration surface and each invocation is a short-lived,
 * synchronous call (registering/hiding one device), not a hot path, so the
 * process-spawn overhead is a non-issue.
 *
 * Every method is a no-op (silently) when isInstalled() is false, so
 * callers never need to guard each call themselves - only the one-time
 * "should I show HidHide UI at all" check does.
 *
 * Follows the same Meyer's-singleton pattern as DeviceManager: use
 * HidHideManager::instance() to obtain the single shared instance.
 */
class HidHideManager : public QObject
{
    Q_OBJECT

public:
    static HidHideManager &instance();

    HidHideManager(const HidHideManager &) = delete;
    HidHideManager &operator=(const HidHideManager &) = delete;
    HidHideManager(HidHideManager &&) = delete;
    HidHideManager &operator=(HidHideManager &&) = delete;

    /// Whether HidHideCLI.exe is present at its known install path.
    bool isInstalled() const;

    /// Registers this application's own executable path on HidHide's
    /// application whitelist, so a device this process itself cloaks does
    /// not go blind to its own DeviceManager/RawInput reads. Call once at
    /// startup (see main.cpp), after QCoreApplication::applicationFilePath()
    /// is valid.
    void whitelistApplication();

    /// Turns HidHide's cloaking behavior on/off globally. cloakDevice()
    /// already calls this with true, so callers normally don't need to call
    /// it directly except to mass-disable cloaking.
    void setGlobalCloak(bool enabled);

    /// Hides instanceId (a device's systemPath, in the same
    /// "HID\VID_xxxx&PID_xxxx\..." form DeviceManager already uses) from
    /// every non-whitelisted application, and ensures global cloaking is on
    /// so the hide actually takes effect.
    void cloakDevice(const QString &instanceId);

    /// Reveals instanceId again.
    void uncloakDevice(const QString &instanceId);

    /// Currently-hidden device instance IDs, parsed from `--dev-list`'s
    /// stdout. Returns an empty list if HidHide isn't installed or the CLI
    /// call fails - callers should treat that the same as "nothing hidden"
    /// rather than an error state.
    QStringList getCloakedDevices() const;

    /// Comprueba si un dispositivo está oculto traduciendo su rawSystemPath
    bool isDeviceCloaked(const QString &rawSystemPath) const;

private:
    explicit HidHideManager(QObject *parent = nullptr);

    /// Runs HidHideCLI.exe with arguments and waits (briefly) for it to
    /// exit. When stdOut is non-null, captures the process' stdout into it.
    /// Returns whether the process launched and exited normally (a non-zero
    /// exit code from the CLI itself is only logged, not treated as a hard
    /// failure here - callers with no lasting state to protect just want
    /// "did the command run").
    bool runCli(const QStringList &arguments, QString *stdOut = nullptr) const;
    
    mutable QStringList m_cloakedCache;
    mutable bool m_cacheValid = false;
};
