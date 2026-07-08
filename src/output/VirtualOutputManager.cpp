#include "VirtualOutputManager.h"

#include <QMutexLocker>

VirtualOutputManager &VirtualOutputManager::instance()
{
    static VirtualOutputManager s_instance;
    return s_instance;
}

std::shared_ptr<IVirtualOutputDevice> VirtualOutputManager::getVJoyDevice(uint id)
{
    QMutexLocker locker(&m_mutex);

    const auto it = m_vjoyDevices.find(id);
    if (it != m_vjoyDevices.end()) {
        return it->second;
    }

    auto device = std::make_shared<VJoyDevice>(id);
    m_vjoyDevices.emplace(id, device);
    return device;
}

std::shared_ptr<IVirtualOutputDevice> VirtualOutputManager::getViGEmDevice(uint id)
{
    QMutexLocker locker(&m_mutex);

    const auto it = m_vigemDevices.find(id);
    if (it != m_vigemDevices.end()) {
        return it->second;
    }

    auto device = std::make_shared<ViGEmDevice>(static_cast<int>(id));
    m_vigemDevices.emplace(id, device);
    return device;
}
