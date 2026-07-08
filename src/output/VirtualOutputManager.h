#pragma once

#include <map>
#include <memory>

#include <QMutex>
#include <QtGlobal>

#include "VJoyDevice.h"
#include "ViGEmDevice.h"

/**
 * @brief Owns and caches virtual output device instances so a given vJoy
 *        device ID is never wrapped by more than one VJoyDevice.
 *
 * Meyer's-singleton, mirroring DeviceManager's pattern. The device cache is
 * mutex-protected since callers may query it from a routing/processing
 * thread distinct from whichever thread first created a given device.
 */
class VirtualOutputManager
{
public:
    static VirtualOutputManager &instance();

    VirtualOutputManager(const VirtualOutputManager &) = delete;
    VirtualOutputManager &operator=(const VirtualOutputManager &) = delete;

    /**
     * @brief Returns the cached VJoyDevice for the given vJoy device ID
     *        [1, 16], creating it on first request.
     *
     * Does not call acquire() — that step (and its failure handling) is the
     * caller's responsibility, since only the caller knows when it actually
     * needs exclusive control of the device.
     */
    std::shared_ptr<IVirtualOutputDevice> getVJoyDevice(uint id);

    /**
     * @brief Returns the cached ViGEmDevice for the given ad-hoc Xbox 360
     *        controller slot id [1, 4], creating it on first request.
     *
     * id is a purely logical cache key (ViGEmDevice itself has no
     * constructor parameter and no persistent target id the way vJoy does -
     * see ViGEmDevice::deviceId()'s own docs) - it just keeps "Xbox 360
     * Controller 2" resolving to the same instance on every call rather
     * than allocating a fresh ad-hoc pad each time. Same lazy-init/mutex
     * pattern as getVJoyDevice(); does not call acquire() either.
     */
    std::shared_ptr<IVirtualOutputDevice> getViGEmDevice(uint id);

private:
    VirtualOutputManager() = default;

    QMutex m_mutex;
    std::map<uint, std::shared_ptr<VJoyDevice>> m_vjoyDevices;
    std::map<uint, std::shared_ptr<ViGEmDevice>> m_vigemDevices;
};
