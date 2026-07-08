#pragma once

#include <QList>
#include <QObject>
#include <QReadWriteLock>

#include "DeviceInfo.h"

class QThread;
class DeviceMonitorWorker;

/**
 * @brief Central, thread-safe catalogue of physical input devices (joysticks/gamepads).
 *
 * DeviceManager owns the authoritative list of currently connected devices
 * and exposes it to the rest of the application via a synchronous snapshot
 * (getConnectedDevices()) and asynchronous notifications (deviceAdded /
 * deviceRemoved signals). Hardware monitoring runs on a dedicated QThread
 * (see DeviceMonitorWorker in DeviceManager.cpp) so that OS notification
 * handling never blocks the application's main/UI thread.
 *
 * This class follows a Meyer's-singleton pattern: use DeviceManager::instance()
 * to obtain the single shared instance.
 */
class DeviceManager : public QObject
{
    Q_OBJECT

public:
    /// Returns the single, lazily-constructed instance of DeviceManager.
    static DeviceManager &instance();

    DeviceManager(const DeviceManager &) = delete;
    DeviceManager &operator=(const DeviceManager &) = delete;
    DeviceManager(DeviceManager &&) = delete;
    DeviceManager &operator=(DeviceManager &&) = delete;

    /**
     * @brief Starts device discovery and monitoring.
     *
     * Spins up the background monitoring thread, which performs the initial
     * catalogue scan and (from Phase 2 onward) registers for Windows device
     * arrival/removal notifications. Safe to call multiple times; calls
     * after the first successful initialization are ignored.
     */
    void initialize();

    /**
     * @brief Returns a thread-safe snapshot of all currently catalogued devices.
     *
     * The returned list is a copy, so callers may freely read it without
     * holding any lock or racing with the monitoring thread.
     */
    QList<DeviceInfo> getConnectedDevices() const;

public slots:
    /// Thread-safe insert/update of a device in the catalogue; emits
    /// deviceAdded. Public (Fase 16.5) so PwaServer's own connected-client
    /// bookkeeping (see main.cpp) can register an authenticated PWA client
    /// as a synthetic device, exactly like a real one - m_devicesLock makes
    /// this safe to call from any thread, not just the monitoring one.
    void addOrUpdateDevice(const DeviceInfo &device);

    /// Thread-safe removal of a device from the catalogue; emits
    /// deviceRemoved. Public for the same reason as addOrUpdateDevice()
    /// above - lets main.cpp drop a PWA client's synthetic device on
    /// disconnect.
    void removeDevice(const QString &systemPath);

    /// Injects a synthetic button transition as if it came from real
    /// hardware, by simply emitting buttonPressed() (PWA input routing fix -
    /// see main.cpp's PwaServer::remoteInputReceived wiring). Routing PWA
    /// taps through here, instead of feeding EventRouter directly, means
    /// every listener of DeviceManager::buttonPressed - DeviceTesterViewModel,
    /// ProfileEditorViewModel's Quick Bind, EventRouter itself - sees a PWA
    /// button exactly like a physical one, instead of only EventRouter
    /// finding out.
    void injectButtonPress(const QString &systemPath, int buttonIndex, bool pressed);

signals:
    /// Emitted when a new device is discovered (initial scan or hot-plug),
    /// or when a catalogued device's metadata is refreshed.
    void deviceAdded(const DeviceInfo &device);

    /// Emitted when a previously catalogued device is disconnected.
    /// @param systemPath Unique OS path/identifier of the removed device.
    void deviceRemoved(const QString &systemPath);

    /// Emitted when an analog axis reading changes.
    /// @param systemPath Unique OS path/identifier of the source device.
    /// @param axisIndex  Index into [0, numAxes) - true analog axes only.
    /// @param value      Raw HID logical value, as reported by the device.
    void axisMoved(const QString &systemPath, int axisIndex, int value);

    /// Emitted once per HID report with all buttons that changed state in that exact millisecond.
    void buttonsChanged(const QVector<ButtonEvent> &events);
    
    // Legacy single-button signal (kept for UI bindings that don't need simultaneous resolution)
    void buttonPressed(const QString &systemPath, int buttonIndex, bool pressed);

private slots:
    /// Relays a worker-thread axis reading out as DeviceManager::axisMoved.
    void onAxisMoved(const QString &systemPath, int axisIndex, int value);

    /// Relays a worker-thread HID-report button batch out as
    /// DeviceManager::buttonsChanged (Fase 20.32) - the batched counterpart
    /// to onButtonPressed() below, letting EventRouter see every button that
    /// changed in the same USB report together instead of one at a time.
    void onButtonsChanged(const QVector<ButtonEvent> &events);

    /// Relays a worker-thread button transition out as DeviceManager::buttonPressed.
    void onButtonPressed(const QString &systemPath, int buttonIndex, bool pressed);

private:
    explicit DeviceManager(QObject *parent = nullptr);
    ~DeviceManager() override;

    /// Protects m_devices against concurrent access from the monitoring
    /// thread and whichever thread(s) call getConnectedDevices().
    mutable QReadWriteLock m_devicesLock;

    /// Authoritative device catalogue. Guarded by m_devicesLock.
    QList<DeviceInfo> m_devices;

    /// Dedicated thread hosting the monitor worker so that hardware
    /// notification handling never blocks the caller of initialize().
    QThread *m_monitorThread = nullptr;

    /// Worker object performing the actual OS-level monitoring; lives in m_monitorThread.
    DeviceMonitorWorker *m_monitorWorker = nullptr;

    bool m_initialized = false;
};
