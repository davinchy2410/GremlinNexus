#include "EventRouter.h"

#include <chrono>
#include <utility>

#include <QDebug>
#include <QMutexLocker>
#include <QSet>
#include <QTimer>

#include "DeviceManager.h"
#include "ModeSwitchHandler.h"
#include "MouseWorkerThread.h"
#include "TemporaryModeSwitchHandler.h"

namespace {
constexpr int kTickIntervalMs = 5; // 200 Hz output poll rate.
}

const QString EventRouter::kGlobalMode = QStringLiteral("Global");

EventRouter::EventRouter(QObject *parent)
    : QObject(parent)
    , m_mouseWorker(std::make_shared<MouseWorkerThread>())
    , m_currentMode(kGlobalMode)
{
}

EventRouter::~EventRouter()
{
    if (m_tickTimer) {
        m_tickTimer->stop();
    }
    for (const auto &device : m_outputDevices) {
        device->relinquish();
    }
}

void EventRouter::start()
{
    if (m_started) {
        return;
    }
    m_started = true;

    connect(&DeviceManager::instance(), &DeviceManager::axisMoved, this, &EventRouter::onAxisMoved);
    connect(&DeviceManager::instance(), &DeviceManager::buttonsChanged, this, &EventRouter::onButtonsChanged);
    connect(&DeviceManager::instance(), &DeviceManager::deviceRemoved, this, &EventRouter::onDeviceRemoved);

    if (!m_tickTimer) {
        m_tickTimer = new QTimer(this);
        connect(m_tickTimer, &QTimer::timeout, this, &EventRouter::tick);
    }
    m_tickTimer->start(kTickIntervalMs);

    m_mouseWorker->start();
}

void EventRouter::stop()
{
    if (!m_started) {
        return;
    }
    m_started = false;

    disconnect(&DeviceManager::instance(), &DeviceManager::axisMoved, this, &EventRouter::onAxisMoved);
    disconnect(&DeviceManager::instance(), &DeviceManager::buttonsChanged, this, &EventRouter::onButtonsChanged);
    disconnect(&DeviceManager::instance(), &DeviceManager::deviceRemoved, this, &EventRouter::onDeviceRemoved);

    if (m_tickTimer) {
        m_tickTimer->stop();
    }

    m_mouseWorker->stop();
}

bool EventRouter::isRunning() const
{
    return m_started;
}

MouseWorkerThread &EventRouter::mouseWorker() const
{
    return *m_mouseWorker;
}

void EventRouter::clearRoutes()
{
    m_axisRoutesByMode.clear();
    m_buttonRoutesByMode.clear();
    m_modeParents.clear();

    for (const auto &device : m_outputDevices) {
        device->relinquish();
    }
    m_outputDevices.clear();
}

void EventRouter::addAxisRoute(const QString &systemPath, int axisIndex, std::shared_ptr<IActionHandler> handler,
                                const QString &mode)
{
    m_axisRoutesByMode[mode][RouteKey{systemPath, axisIndex}] = std::move(handler);
}

void EventRouter::addButtonRoute(const QString &systemPath, int buttonIndex, std::shared_ptr<IActionHandler> handler,
                                  const QString &mode)
{
    m_buttonRoutesByMode[mode][RouteKey{systemPath, buttonIndex}] = std::move(handler);
}

void EventRouter::registerOutputDevice(std::shared_ptr<IVirtualOutputDevice> device)
{
    if (!device) {
        return;
    }
    for (const auto &existing : m_outputDevices) {
        if (existing == device) {
            return;
        }
    }
    m_outputDevices.push_back(std::move(device));
}

void EventRouter::setMode(const QString &modeName)
{
    QMutexLocker locker(&m_modeMutex);
    if (m_currentMode != modeName) {
        m_currentMode = modeName;
        emit modeChanged(modeName);
    }
}

QString EventRouter::currentMode() const
{
    QMutexLocker locker(&m_modeMutex);
    return m_currentMode;
}

void EventRouter::setModeParent(const QString &child, const QString &parent)
{
    if (child.isEmpty() || child == kGlobalMode) {
        return; // Global is always the tree's root - never gets a parent.
    }
    if (parent.isEmpty() || parent == child) {
        m_modeParents.remove(child); // No parent / self-reference -> child becomes its own root.
        return;
    }
    m_modeParents[child] = parent;
}

QString EventRouter::getModeParent(const QString &mode) const
{
    return m_modeParents.value(mode);
}

QHash<QString, QString> EventRouter::allModeParents() const
{
    return m_modeParents;
}

void EventRouter::reevaluateHeldState(const QString &excludeSystemPath, int excludeButtonIndex)
{
    const RouteKey excludeKey{excludeSystemPath, excludeButtonIndex};
    const QString activeMode = currentMode();

    for (const auto &[key, value] : m_axisStates) {
        if (key == excludeKey) continue;
        const AxisEvent evt{key.first, key.second, value};
        if (const auto handler = resolveHandlerWithFallback(m_axisRoutesByMode, activeMode, key.first, key.second)) {
            handler->processAxis(evt);
        }
    }

    for (const auto &[key, pressed] : m_buttonStates) {
        if (!pressed || key == excludeKey) continue;

        auto it = m_activeButtonHandlers.find(key);
        if (it != m_activeButtonHandlers.end()) {
            const ButtonEvent releaseEvt{key.first, key.second, false};
            for (const auto &handler : it->second) {
                handler->processButton(releaseEvt);
            }
            m_activeButtonHandlers.erase(it);
        }

        const ButtonEvent pressEvt{key.first, key.second, true};
        std::vector<std::shared_ptr<IActionHandler>> handlers;
        if (const auto handler = resolveHandlerWithFallback(m_buttonRoutesByMode, activeMode, key.first, key.second)) {
            handler->processButton(pressEvt);
            handlers.push_back(handler);
        }
        if (!handlers.empty()) {
            m_activeButtonHandlers[key] = std::move(handlers);
        }
    }
}

bool EventRouter::isButtonPressed(const QString &systemPath, int buttonIndex) const
{
    if (systemPath.isEmpty()) {
        // Wildcard: true if any device currently has this button held,
        // mirroring the "" == any-device convention resolveHandler() uses.
        for (const auto &entry : m_buttonStates) {
            if (entry.first.second == buttonIndex && entry.second) {
                return true;
            }
        }
        return false;
    }

    const auto it = m_buttonStates.find(RouteKey{systemPath, buttonIndex});
    return it != m_buttonStates.end() && it->second;
}

int EventRouter::getAxisValue(const QString &systemPath, int axisIndex) const
{
    const auto it = m_axisStates.find(RouteKey{systemPath, axisIndex});
    return it != m_axisStates.end() ? it->second : 32767;
}

void EventRouter::onAxisMoved(const QString &systemPath, int axisIndex, int value)
{
    m_axisStates[RouteKey{systemPath, axisIndex}] = value;

    const AxisEvent evt{systemPath, axisIndex, value};
    const QString activeMode = currentMode();

    if (const auto handler = resolveHandlerWithFallback(m_axisRoutesByMode, activeMode, systemPath, axisIndex)) {
        // --- TEMPORARY DIAGNOSTIC (perf investigation - remove once done) ---
        // Measures a single handler's processAxis() cost on the hot path -
        // this runs once per axisMoved(), which DeviceManager already caps
        // at 250 Hz (see DeviceMonitorWorker's own docs), so even a "slow"
        // handler here is unlikely to be the UI-lag source by itself; this
        // is to rule it in/out with real numbers rather than guessing.
        const auto perfStart = std::chrono::high_resolution_clock::now();
        handler->processAxis(evt);
        const auto perfEnd = std::chrono::high_resolution_clock::now();
        const auto perfMicros = std::chrono::duration_cast<std::chrono::microseconds>(perfEnd - perfStart).count();
        if (perfMicros > 500) {
            qWarning() << "[EventRouter] processAxis for" << systemPath << "axis" << axisIndex
                       << "took" << perfMicros << "us (>500us threshold)";
        }
        // --- END TEMPORARY DIAGNOSTIC ---
    }
}

void EventRouter::onButtonsChanged(const QVector<ButtonEvent> &events)
{
    QVector<bool> processed(events.size(), false);

    // Pass 1: Prioritize Mode Switches
    for (int i = 0; i < events.size(); ++i) {
        const ButtonEvent &evt = events[i];
        bool isModeSwitch = false;

        const QString activeMode = currentMode();
        
        if (evt.pressed) {
            auto handler = resolveHandlerWithFallback(m_buttonRoutesByMode, activeMode, evt.systemPath, evt.buttonIndex);
            if (handler && (std::dynamic_pointer_cast<TemporaryModeSwitchHandler>(handler) ||
                            std::dynamic_pointer_cast<ModeSwitchHandler>(handler))) {
                isModeSwitch = true;
            }
        } else {
            auto it = m_activeButtonHandlers.find(RouteKey{evt.systemPath, evt.buttonIndex});
            if (it != m_activeButtonHandlers.end()) {
                for (const auto &handler : it->second) {
                    if (std::dynamic_pointer_cast<TemporaryModeSwitchHandler>(handler) ||
                        std::dynamic_pointer_cast<ModeSwitchHandler>(handler)) {
                        isModeSwitch = true;
                        break;
                    }
                }
            } else {
                auto handler = resolveHandlerWithFallback(m_buttonRoutesByMode, activeMode, evt.systemPath, evt.buttonIndex);
                if (handler && (std::dynamic_pointer_cast<TemporaryModeSwitchHandler>(handler) ||
                                std::dynamic_pointer_cast<ModeSwitchHandler>(handler))) {
                    isModeSwitch = true;
                }
            }
        }

        if (isModeSwitch) {
            onButtonPressed(evt.systemPath, evt.buttonIndex, evt.pressed);
            processed[i] = true;
        }
    }

    // Pass 2: Process the rest
    for (int i = 0; i < events.size(); ++i) {
        if (!processed[i]) {
            onButtonPressed(events[i].systemPath, events[i].buttonIndex, events[i].pressed);
        }
    }
}

void EventRouter::onButtonPressed(const QString &systemPath, int buttonIndex, bool pressed)
{
    const RouteKey key{systemPath, buttonIndex};
    m_buttonStates[key] = pressed;

    const ButtonEvent evt{systemPath, buttonIndex, pressed};
    const QString activeMode = currentMode();

    if (pressed) {
        std::vector<std::shared_ptr<IActionHandler>> handlers;
        if (const auto handler = resolveHandlerWithFallback(m_buttonRoutesByMode, activeMode, systemPath, buttonIndex)) {
            qInfo() << "EventRouter: Button" << buttonIndex << "pressed. Executing handler resolved via mode cascade from:" << activeMode;
            handler->processButton(evt);
            handlers.push_back(handler);
        } else {
            qInfo() << "EventRouter: Button" << buttonIndex << "pressed. NO handler found anywhere in the mode cascade from" << activeMode;
        }
        if (!handlers.empty()) {
            m_activeButtonHandlers[key] = std::move(handlers);
        }
    } else {
        auto it = m_activeButtonHandlers.find(key);
        if (it != m_activeButtonHandlers.end()) {
            qInfo() << "EventRouter: Button" << buttonIndex << "released. Found" << it->second.size() << "tracked handler(s) from previous press.";
            for (const auto &handler : it->second) {
                handler->processButton(evt);
            }
            m_activeButtonHandlers.erase(it);
        } else {
            // Fallback for buttons already held before the router or profile loaded
            if (const auto handler = resolveHandlerWithFallback(m_buttonRoutesByMode, activeMode, systemPath, buttonIndex)) {
                qInfo() << "EventRouter: Button" << buttonIndex << "released (untracked). Executing handler resolved via mode cascade from:" << activeMode;
                handler->processButton(evt);
            }
        }
    }
}

QString EventRouter::pwaSystemPath(const QString &deviceName)
{
    return QStringLiteral("pwa:") + deviceName;
}

void EventRouter::tick()
{
    for (const auto &device : m_outputDevices) {
        device->update();
    }
}

std::vector<EventRouter::RouteDescriptor> EventRouter::allRoutes() const
{
    std::vector<RouteDescriptor> routes;

    const auto collect = [&routes](const ModeRouteTables &routesByMode, bool isAxis) {
        for (const auto &[mode, table] : routesByMode) {
            for (const auto &[key, handler] : table) {
                routes.push_back(RouteDescriptor{mode, key.first, key.second, isAxis, handler});
            }
        }
    };
    collect(m_axisRoutesByMode, true);
    collect(m_buttonRoutesByMode, false);

    return routes;
}

void EventRouter::swapDeviceSystemPaths(const QString &fromPath, const QString &toPath)
{
    if (fromPath == toPath) {
        return;
    }

    for (auto &modeEntry : m_axisRoutesByMode) {
        swapSystemPath(modeEntry.second, fromPath, toPath);
    }
    for (auto &modeEntry : m_buttonRoutesByMode) {
        swapSystemPath(modeEntry.second, fromPath, toPath);
    }
    swapSystemPath(m_buttonStates, fromPath, toPath);
    swapSystemPath(m_axisStates, fromPath, toPath);
    swapSystemPath(m_activeButtonHandlers, fromPath, toPath);

    qInfo() << "EventRouter: swapped device bindings from" << fromPath << "to" << toPath;
}

void EventRouter::onDeviceRemoved(const QString &systemPath)
{
    if (systemPath.isEmpty()) {
        return; // Never purge the wildcard entry itself.
    }

    for (auto &modeEntry : m_axisRoutesByMode) {
        erasePath(modeEntry.second, systemPath);
    }
    for (auto &modeEntry : m_buttonRoutesByMode) {
        erasePath(modeEntry.second, systemPath);
    }
    erasePath(m_buttonStates, systemPath);
    erasePath(m_axisStates, systemPath);
    erasePath(m_activeButtonHandlers, systemPath);

    qInfo() << "EventRouter: purged routes/state for disconnected device" << systemPath;
}

std::shared_ptr<IActionHandler> EventRouter::resolveHandler(const RouteTable &table, const QString &systemPath,
                                                              int index) const
{
    auto it = table.find(RouteKey{systemPath, index});
    if (it != table.end()) {
        return it->second;
    }

    it = table.find(RouteKey{QString(), index}); // wildcard: any device
    if (it != table.end()) {
        return it->second;
    }

    return nullptr;
}

std::shared_ptr<IActionHandler> EventRouter::resolveHandlerForMode(const ModeRouteTables &routesByMode,
                                                                     const QString &mode, const QString &systemPath,
                                                                     int index) const
{
    const auto modeIt = routesByMode.find(mode);
    if (modeIt == routesByMode.end()) {
        return nullptr;
    }
    return resolveHandler(modeIt->second, systemPath, index);
}

std::shared_ptr<IActionHandler> EventRouter::resolveHandlerWithFallback(const ModeRouteTables &routesByMode,
                                                                          const QString &activeMode,
                                                                          const QString &systemPath, int index) const
{
    // Fast path: a route bound directly in the active mode - the
    // overwhelmingly common case, and the only one this method paid for
    // before Sprint 5's mode inheritance existed. Zero allocations.
    if (const auto handler = resolveHandlerForMode(routesByMode, activeMode, systemPath, index)) {
        return handler;
    }

    // Slow path: climb m_modeParents (activeMode's parent, its parent, ...).
    // The QSet is only ever constructed once the fast path above has
    // already missed, so it never costs anything on the hot per-event path.
    QSet<QString> visited{activeMode};
    QString mode = getModeParent(activeMode);
    while (!mode.isEmpty() && !visited.contains(mode)) {
        visited.insert(mode);
        if (const auto handler = resolveHandlerForMode(routesByMode, mode, systemPath, index)) {
            return handler;
        }
        mode = getModeParent(mode);
    }
    return nullptr;
}
