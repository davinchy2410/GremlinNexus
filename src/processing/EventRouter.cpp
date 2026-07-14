#include "EventRouter.h"

#include <algorithm>
#include <iterator>
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

/// Some devices (seen on VKB sticks) expose the same physical button through
/// a second, generic HID collection interface (systemPath containing
/// "HIDCLASS&Col") in addition to their real vendor-specific VID/PID
/// interface - Windows reports a duplicate press event on that shadow
/// interface for every real one. Profiles only ever bind against the real
/// VID/PID path, so the shadow interface's event legitimately never has a
/// handler; logging "NO handler found" for it every single press was just
/// noise (the real interface's own press, logged right alongside it, is what
/// actually drives the handler) that made a user's session log look like a
/// binding problem when nothing was actually broken.
bool isShadowHidInterface(const QString &systemPath)
{
    return systemPath.contains(QStringLiteral("HIDCLASS&Col"), Qt::CaseInsensitive);
}
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

    // BUG-001 fix: reset mouse velocity to zero so a deflected axis mapped to
    // mouse motion doesn't keep moving the cursor forever after the handler
    // that was writing that velocity is destroyed by this clear. Unlike the
    // vJoy/ViGEm output devices below, the MouseWorkerThread has no
    // relinquish() equivalent in this path - it stays alive across profile
    // reloads and simply retains the last velocity written to it.
    if (m_mouseWorker) {
        m_mouseWorker->setVelocityX(0.0f);
        m_mouseWorker->setVelocityY(0.0f);
    }

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

void EventRouter::pushTemporaryMode(const void *owner, const QString &targetMode)
{
    if (m_modeStack.empty()) {
        m_modeStackBaseMode = currentMode();
    }
    m_modeStack.emplace_back(owner, targetMode);
    setMode(targetMode);
}

void EventRouter::popTemporaryMode(const void *owner)
{
    for (auto it = m_modeStack.rbegin(); it != m_modeStack.rend(); ++it) {
        if (it->first == owner) {
            const bool wasTop = (it == m_modeStack.rbegin());
            // rbegin()-based reverse_iterator -> base() is the corresponding
            // forward iterator one element ahead; erase() needs the forward one.
            m_modeStack.erase(std::next(it).base());
            if (wasTop) {
                setMode(m_modeStack.empty() ? m_modeStackBaseMode : m_modeStack.back().second);
            }
            return;
        }
    }
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

        // Diff old vs. new handler(s) by pointer identity instead of
        // unconditionally releasing-then-re-pressing: a held button whose
        // route falls back to the same Global-bound handler both before and
        // after this mode change (the common case - most bindings aren't
        // overridden per-mode) would otherwise get a spurious release+press
        // pair against a handler that never actually changed. Harmless for
        // a plain ButtonRemapHandler (it just re-writes the same vJoy bit),
        // but a ToggleHandler-wrapped binding would silently flip its
        // toggle state a second time, and a TempoHandler-wrapped one would
        // restart its gesture - on every mode switch, even one unrelated to
        // that specific button's own binding.
        auto it = m_activeButtonHandlers.find(key);
        std::vector<std::shared_ptr<IActionHandler>> oldHandlers;
        if (it != m_activeButtonHandlers.end()) {
            oldHandlers = std::move(it->second);
            m_activeButtonHandlers.erase(it);
        }

        std::vector<std::shared_ptr<IActionHandler>> newHandlers;
        if (const auto handler = resolveHandlerWithFallback(m_buttonRoutesByMode, activeMode, key.first, key.second)) {
            newHandlers.push_back(handler);
        }

        const ButtonEvent releaseEvt{key.first, key.second, false};
        for (const auto &oldHandler : oldHandlers) {
            if (std::find(newHandlers.begin(), newHandlers.end(), oldHandler) == newHandlers.end()) {
                qInfo() << "EventRouter: reevaluateHeldState re-releasing button" << key.second << "on" << key.first
                         << "against mode" << activeMode << "(triggered by button" << excludeButtonIndex << "on"
                         << excludeSystemPath << ")";
                oldHandler->processButton(releaseEvt);
            }
        }

        const ButtonEvent pressEvt{key.first, key.second, true};
        for (const auto &newHandler : newHandlers) {
            if (std::find(oldHandlers.begin(), oldHandlers.end(), newHandler) == oldHandlers.end()) {
                qInfo() << "EventRouter: reevaluateHeldState re-pressing button" << key.second << "on" << key.first
                         << "against mode" << activeMode << "(triggered by button" << excludeButtonIndex << "on"
                         << excludeSystemPath << ")";
                newHandler->processButton(pressEvt);
            }
        }

        if (!newHandlers.empty()) {
            m_activeButtonHandlers[key] = std::move(newHandlers);
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

std::shared_ptr<IActionHandler> EventRouter::resolveButtonHandlerForCurrentMode(const QString &systemPath,
                                                                                  int buttonIndex) const
{
    return resolveHandlerWithFallback(m_buttonRoutesByMode, currentMode(), systemPath, buttonIndex);
}

void EventRouter::onAxisMoved(const QString &systemPath, int axisIndex, int value)
{
    m_axisStates[RouteKey{systemPath, axisIndex}] = value;

    const AxisEvent evt{systemPath, axisIndex, value};
    const QString activeMode = currentMode();

    if (const auto handler = resolveHandlerWithFallback(m_axisRoutesByMode, activeMode, systemPath, axisIndex)) {
        handler->processAxis(evt);
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
            if (handler && handler->isModeSwitch()) {
                isModeSwitch = true;
            }
        } else {
            auto it = m_activeButtonHandlers.find(RouteKey{evt.systemPath, evt.buttonIndex});
            if (it != m_activeButtonHandlers.end()) {
                for (const auto &handler : it->second) {
                    if (handler->isModeSwitch()) {
                        isModeSwitch = true;
                        break;
                    }
                }
            } else {
                auto handler = resolveHandlerWithFallback(m_buttonRoutesByMode, activeMode, evt.systemPath, evt.buttonIndex);
                if (handler && handler->isModeSwitch()) {
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

    // Idempotency guard: a genuine hardware transition never reaches here
    // twice in a row with the same state - DeviceManager::parseHidReport()
    // only emits buttonPressed/buttonsChanged on an actual bit flip, and a
    // real mechanical bounce is itself a genuine oscillating transition, not
    // a repeated identical one, so neither is silently dropped by this
    // check. What this DOES catch is a source that bypasses that dedup - a
    // flaky PWA client re-sending "pressed=true" as a keepalive via
    // DeviceManager::injectButtonPress(), or a mis-wired macro re-firing
    // without a matching release - which would otherwise re-run the full
    // route-resolve + handler-dispatch + logging path on every duplicate
    // for no behavioral effect (UpdateVJD() is already rate-limited to
    // EventRouter::tick()'s fixed 200 Hz regardless, so this isn't a driver-
    // saturation fix - it's wasted CPU/log-spam avoidance on a redundant
    // event that was never going to change any output).
    const auto previousStateIt = m_buttonStates.find(key);
    if (previousStateIt != m_buttonStates.end() && previousStateIt->second == pressed) {
        return;
    }
    m_buttonStates[key] = pressed;

    const ButtonEvent evt{systemPath, buttonIndex, pressed};
    const QString activeMode = currentMode();

    if (pressed) {
        std::vector<std::shared_ptr<IActionHandler>> handlers;
        if (const auto handler = resolveHandlerWithFallback(m_buttonRoutesByMode, activeMode, systemPath, buttonIndex)) {
            qInfo() << "EventRouter: Button" << buttonIndex << "on" << systemPath
                    << "pressed. Executing handler resolved via mode cascade from:" << activeMode;
            handler->processButton(evt);
            handlers.push_back(handler);
        } else if (!isShadowHidInterface(systemPath)) {
            qInfo() << "EventRouter: Button" << buttonIndex << "on" << systemPath
                    << "pressed. NO handler found anywhere in the mode cascade from" << activeMode;
        }
        if (!handlers.empty()) {
            m_activeButtonHandlers[key] = std::move(handlers);
        }
    } else {
        auto it = m_activeButtonHandlers.find(key);
        if (it != m_activeButtonHandlers.end()) {
            qInfo() << "EventRouter: Button" << buttonIndex << "on" << systemPath
                    << "released. Found" << it->second.size() << "tracked handler(s) from previous press.";
            for (const auto &handler : it->second) {
                handler->processButton(evt);
            }
            m_activeButtonHandlers.erase(it);
        } else {
            // Fallback for buttons already held before the router or profile loaded
            if (const auto handler = resolveHandlerWithFallback(m_buttonRoutesByMode, activeMode, systemPath, buttonIndex)) {
                qInfo() << "EventRouter: Button" << buttonIndex << "on" << systemPath
                        << "released (untracked). Executing handler resolved via mode cascade from:" << activeMode;
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

    // Stuck-ON fix: force-release every handler still tracked as mid-press
    // for this device *before* discarding the tracking entry - mirrors the
    // release reevaluateHeldState() already sends on a mode switch. A
    // handler that reflects a physical hold directly onto persistent output
    // state (ButtonRemapHandler's vJoy bit, HatRemapHandler's POV angle, a
    // held keyboard key) would otherwise never see its matching release when
    // the device is unplugged mid-press, and that output stays stuck exactly
    // as it was at the moment of disconnection - indefinitely, since no
    // further event can ever arrive from a device that's gone. Must run
    // before erasePath(m_activeButtonHandlers, ...) below, which only
    // discards the tracking - it never dispatches anything itself.
    for (auto it = m_activeButtonHandlers.begin(); it != m_activeButtonHandlers.end();) {
        if (it->first.first == systemPath) {
            const ButtonEvent releaseEvt{systemPath, it->first.second, false};
            for (const auto &handler : it->second) {
                handler->processButton(releaseEvt);
            }
            it = m_activeButtonHandlers.erase(it);
        } else {
            ++it;
        }
    }

    for (auto &modeEntry : m_axisRoutesByMode) {
        erasePath(modeEntry.second, systemPath);
    }
    for (auto &modeEntry : m_buttonRoutesByMode) {
        erasePath(modeEntry.second, systemPath);
    }
    erasePath(m_buttonStates, systemPath);
    erasePath(m_axisStates, systemPath);
    // m_activeButtonHandlers already fully purged by the release loop above.

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
