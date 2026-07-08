#pragma once

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>

#include "IActionHandler.h"
#include "IVirtualOutputDevice.h"

class QTimer;

/**
 * @brief Central event router: subscribes to DeviceManager's raw axis/
 *        button signals, dispatches each event to the IActionHandler(s)
 *        bound to its (mode, systemPath, index) route, and drives a
 *        fixed-rate tick that flushes every acquired output device's
 *        staged state to the OS driver in one batched report per device
 *        per tick — never per input event.
 *
 * Threading: EventRouter does not move itself to a dedicated thread; it is
 * a plain QObject living on whichever thread constructs it (expected to be
 * the same thread DeviceManager itself lives on — DeviceManager's public
 * axisMoved/buttonPressed signals are re-emitted from DeviceManager's own
 * thread affinity, see DeviceManager::onAxisMoved/onButtonPressed). Because
 * of that, onAxisMoved(), onButtonPressed() and tick() all run serialized
 * on that one thread's event loop — never concurrently with each other —
 * which is what makes the routing table and m_outputDevices safe to touch
 * without an internal mutex. Call start() from that same thread, before
 * DeviceManager could start delivering events to it; likewise, whatever
 * populates the routing table (see ProfileManager) must also run on that
 * thread — these methods do not lock, by design (see class docs above).
 *
 * The one deliberate exception is the active mode (see setMode()): unlike
 * the rest of this class's state, that can legitimately be set from
 * outside the router's own thread (e.g. a GUI mode-select control), in
 * addition to the expected same-thread call from a ModeSwitchHandler's
 * processButton(). It gets its own small dedicated mutex rather than
 * pulling the whole class into a locking scheme it doesn't otherwise need.
 *
 * Modes: routes are registered under a named mode (kGlobalMode, "Global",
 * by default). On each incoming event, the router dispatches to whatever's
 * bound for the *currently active* mode at that (systemPath, index) — AND,
 * separately, to whatever's bound in "Global" for the same (systemPath,
 * index), since Global bindings are meant to stay active no matter which
 * mode is selected (e.g. the very ModeSwitchHandler used to leave a mode,
 * or any binding that should never turn off). If the active mode *is*
 * "Global" already, only one lookup/dispatch happens, not two redundant ones.
 */
class EventRouter : public QObject
{
    Q_OBJECT

public:
    /// Name of the always-active fallback mode.
    static const QString kGlobalMode;

    explicit EventRouter(QObject *parent = nullptr);
    ~EventRouter() override;

    /// Connects to DeviceManager::instance()'s signals and starts the
    /// output tick timer. Safe to call more than once; later calls are ignored.
    void start();

    /// Disconnects from DeviceManager::instance()'s signals and stops the
    /// output tick timer - the inverse of start(). Routes, registered
    /// output devices and per-(systemPath, index) axis/button state are all
    /// left exactly as they are (only the live input->output pipeline
    /// pauses), so a later start() resumes with the same routing table
    /// instead of needing a profile reload. Safe to call when not started;
    /// does nothing in that case.
    void stop();

    /// Whether the router is currently connected to DeviceManager and
    /// ticking its output devices (i.e. whether start() has been called
    /// more recently than stop()).
    bool isRunning() const;

    /// Drops every axis/button route (in every mode), the mode-parent tree
    /// (see setModeParent(), Sprint 5), and relinquishes + drops every
    /// registered output device, resetting the router to an empty state.
    /// Called by ProfileManager before applying a newly loaded profile, so
    /// that reloading a profile (or switching to a different one) never
    /// leaves the previous profile's devices acquired-but-orphaned or its
    /// mode hierarchy bleeding into the next profile's own. NOTE:
    /// ProfileManager::renameMode() also calls this mid-rename to reapply
    /// every binding under its new mode name - it snapshots
    /// allModeParents() first and restores it (renamed) afterward, so a
    /// rename never silently drops the hierarchy this clears.
    void clearRoutes();

    /// Binds axisIndex on the source device identified by systemPath (empty
    /// = wildcard, matches any device) to handler, under mode. Replaces any
    /// existing route for the same (mode, systemPath, axisIndex) triple.
    void addAxisRoute(const QString &systemPath, int axisIndex, std::shared_ptr<IActionHandler> handler,
                       const QString &mode = kGlobalMode);

    /// Binds buttonIndex on the source device identified by systemPath
    /// (empty = wildcard) to handler, under mode. Replaces any existing
    /// route for the same (mode, systemPath, buttonIndex) triple.
    void addButtonRoute(const QString &systemPath, int buttonIndex, std::shared_ptr<IActionHandler> handler,
                         const QString &mode = kGlobalMode);

    /// Registers device so the tick loop flushes it once per tick. A device
    /// already registered (compared by shared_ptr identity, e.g. the same
    /// vJoy device backing two different bindings) is not added twice.
    void registerOutputDevice(std::shared_ptr<IVirtualOutputDevice> device);

    /// Switches the router's active mode. See the threading note above:
    /// safe to call from any thread.
    void setMode(const QString &modeName);

    /// Thread-safe snapshot of the currently active mode.
    QString currentMode() const;

    /// Sprint 5 (Familias de Modos): records that child's routes should
    /// cascade up to parent whenever child itself has no route bound for a
    /// given (systemPath, index) - see resolveHandlerWithFallback() for the
    /// actual climb. child == kGlobalMode is refused: Global is always the
    /// tree's own root, so its "parent" stays empty/null no matter what's
    /// asked. An empty parent (or parent == child, a self-reference) clears
    /// any existing entry instead of storing one, which makes child a root
    /// of its own - it will NOT implicitly fall back to Global unless
    /// something re-points it there (addMode() does this by default for
    /// every newly created mode; ProfileManager::loadProfile() does it for
    /// any legacy mode a profile references with no explicit
    /// "modeHierarchy" entry - see that method's own docs). Called from any
    /// thread the GUI happens to run on, same as setMode() - unlike
    /// setMode() this isn't consulted mid-dispatch from a different thread
    /// than it's written on in practice (profile load/edit only happens on
    /// the router's own thread), so it isn't mutex-guarded.
    void setModeParent(const QString &child, const QString &parent);

    /// child's configured parent, or an empty string if child has none
    /// (e.g. kGlobalMode itself, or any other mode nothing ever pointed
    /// elsewhere).
    QString getModeParent(const QString &mode) const;

    /// Snapshot of every configured (child -> parent) edge - used by
    /// ProfileManager::serializeProfile() to write the profile's
    /// "modeHierarchy" JSON object out from live router state, the same
    /// "read from the router, not a cached copy" approach allRoutes() and
    /// currentMode() already follow.
    QHash<QString, QString> allModeParents() const;

    /// Re-dispatches every currently-held axis/button as if it had just been
    /// (re-)pressed/moved under whatever mode is active *right now* (Fase
    /// 20.36) - called by ModeSwitchHandler/TemporaryModeSwitchHandler right
    /// after they change the active mode, so a trigger the user is already
    /// holding through a mode switch immediately cuts over to the new mode's
    /// binding instead of waiting for the next physical press/release to
    /// notice. A held button already tracked in m_activeButtonHandlers is
    /// released against its *old* handler first (so that handler doesn't get
    /// stuck thinking it's still held) before being re-pressed against
    /// whatever resolves for the new mode. excludeSystemPath/excludeButtonIndex
    /// skip the mode-switch button itself, which already got its own
    /// press/release through the normal dispatch path and must not be
    /// double-processed here.
    void reevaluateHeldState(const QString &excludeSystemPath, int excludeButtonIndex);

    /// Returns whether buttonIndex on systemPath is *physically* held right
    /// now, per the most recent onButtonPressed() for that (systemPath,
    /// index) - independent of mode, and independent of whatever that
    /// button's own route (if any) is bound to. A button can simultaneously
    /// be a normal action trigger and a ConditionHandler modifier. An empty
    /// systemPath is a wildcard: true if *any* device currently has
    /// buttonIndex held, mirroring the "" == any-device convention used
    /// everywhere else in this schema (see resolveHandler()). Not
    /// mutex-guarded: like the rest of the routing table (see the threading
    /// note above), both the write (onButtonPressed) and every read
    /// (ConditionHandler::processAxis/processButton, called synchronously
    /// from within onAxisMoved/onButtonPressed's own dispatch) happen only
    /// on the router's own thread, so m_modeMutex would be protecting
    /// against a race that can't happen here.
    bool isButtonPressed(const QString &systemPath, int buttonIndex) const;

    /// Returns the last raw value reported for axisIndex on systemPath, per
    /// the most recent onAxisMoved() for that exact (systemPath, index) -
    /// or 32767 (a HID logical-range center, matching the "not moved yet"
    /// assumption other handlers already make) if no such axis has ever
    /// reported. Unlike isButtonPressed(), this is an *exact* lookup only -
    /// no "" wildcard fallback - since a MergeAxisHandler's whole purpose
    /// is combining two concretely-identified physical axes; "any device's
    /// axis N" is not a meaningful thing to merge against. Same
    /// no-mutex-needed reasoning as isButtonPressed(): both the write
    /// (onAxisMoved) and every read happen only on the router's own thread.
    int getAxisValue(const QString &systemPath, int axisIndex) const;

    /// One registered route, flattened out of the (mode -> RouteTable)
    /// structure for easy iteration - see allRoutes().
    struct RouteDescriptor
    {
        QString mode;
        QString systemPath; ///< "" = wildcard (matches any source device).
        int index = 0;      ///< Axis or button index, per isAxis.
        bool isAxis = false;
        std::shared_ptr<IActionHandler> handler;
    };

    /// Snapshot of every currently registered axis/button route, across
    /// every mode (Fase 10.8) - used by ProfileManager::serializeProfile()
    /// to rebuild a profile's JSON "bindings" array from *live* routing
    /// state instead of a cached copy of whatever was last read from disk.
    /// A plain read-only walk of m_axisRoutesByMode/m_buttonRoutesByMode;
    /// same single-thread-only contract as the rest of the routing table
    /// (see class docs above) - callers must be on EventRouter's own thread.
    std::vector<RouteDescriptor> allRoutes() const;

    /// Synthetic systemPath used to address a PWA remote-control client's
    /// virtual "device" in the routing table (Fase 16 Part 1). PWA taps are
    /// injected via DeviceManager::injectButtonPress() (see main.cpp's
    /// PwaServer::remoteInputReceived wiring) using this same systemPath, so
    /// they flow into EventRouter through the ordinary
    /// DeviceManager::buttonPressed -> onButtonPressed() path - no
    /// PWA-specific entry point on EventRouter itself - which is also how
    /// DeviceTesterViewModel and ProfileEditorViewModel's Quick Bind see them.
    /// Keyed by the user-chosen deviceName (Fase 17) rather than the random
    /// per-install deviceId, so bindings survive a cache wipe/new tablet: the
    /// identity that matters to Grembling is "the same named webpad", not
    /// "the same browser storage". Real DeviceInfo::systemPath values never
    /// collide with this: they are Windows device-interface paths, always
    /// starting with "\\\\?\\", never "pwa:".
    static QString pwaSystemPath(const QString &deviceName);

    /// Rewrites every route (in every mode), plus the per-(systemPath,
    /// index) physical button/axis state cache (see isButtonPressed()/
    /// getAxisValue()), currently keyed under fromPath's exact systemPath
    /// so it is keyed under toPath instead (Fase 14 "Swap Devices" - moving
    /// a whole device's existing bindings onto a different physical
    /// device's systemPath without rebuilding the profile from scratch).
    /// Wildcard ("") routes are untouched - there is no single wildcard
    /// route to redirect, only ever exact-systemPath entries are moved. A
    /// route already registered at (mode, toPath, index) is silently
    /// overwritten by the moved one, the same replace-in-place semantics
    /// addAxisRoute()/addButtonRoute() already document. A no-op if
    /// fromPath == toPath.
    void swapDeviceSystemPaths(const QString &fromPath, const QString &toPath);

public slots:
    /// Processes all button events in a single HID report frame, prioritizing mode switches
    void onButtonsChanged(const QVector<ButtonEvent> &events);
    
    /// Handle a raw button press event from DeviceManager (Legacy single button fallback)
    void onButtonPressed(const QString &systemPath, int buttonIndex, bool pressed);

    /// Handle a raw axis movement event from DeviceManager
    void onAxisMoved(const QString &systemPath, int axisIndex, int value);

    /// Fase 0 (stabilization): purges every route (in every mode) bound to
    /// systemPath - plus its cached physical button/axis state
    /// (isButtonPressed()/getAxisValue()) and any handler still tracked in
    /// m_activeButtonHandlers as mid-press for it - when that device
    /// physically disconnects (wire this to DeviceManager::deviceRemoved;
    /// start()/stop() do so automatically). Left unhandled, a route bound
    /// to a since-vanished systemPath would just sit inert (nothing ever
    /// arrives from a disconnected device to dispatch it), but a *held*
    /// button's handler would stay stuck thinking it's still pressed
    /// forever, and the state caches would grow unbounded across repeated
    /// hot-plug cycles - this clears all of it out immediately instead.
    /// Registered output devices (m_outputDevices) are untouched: a vJoy/
    /// ViGEm *output* target is never owned by whichever *input* device
    /// happened to be bound to it, so removing one has no bearing on the
    /// other. Wildcard ("") routes are never purged, matching
    /// swapDeviceSystemPaths()'s own convention - a no-op if systemPath is
    /// empty.
    void onDeviceRemoved(const QString &systemPath);

    /// Flushes every acquired output device's staged state to the driver.
    /// Runs on m_tickTimer, decoupled from the rate of incoming input events.
    void tick();

signals:
    /// Emitted from setMode() whenever the active mode actually changes
    /// (not on every call - see setMode()'s own updated docs). setMode()
    /// can be called from any thread (per the threading note above), so a
    /// slot connected from the GUI thread receives this via a queued
    /// connection automatically - it never runs synchronously on whichever
    /// thread called setMode().
    void modeChanged(const QString &newMode);

private:
    /// Routing key: (source systemPath, source axis/button index). An
    /// empty systemPath is a wildcard matching any source device, used as
    /// a fallback when no route exists for the event's exact systemPath.
    using RouteKey = std::pair<QString, int>;

    struct RouteKeyHash
    {
        size_t operator()(const RouteKey &key) const noexcept
        {
            return qHash(key.first) ^ (static_cast<size_t>(key.second) * 0x9E3779B97F4A7C15ULL);
        }
    };

    using RouteTable = std::unordered_map<RouteKey, std::shared_ptr<IActionHandler>, RouteKeyHash>;

    /// One RouteTable per mode name; "Global" is just another entry here,
    /// with no special storage — only dispatch treats it specially.
    using ModeRouteTables = std::unordered_map<QString, RouteTable>;

    /// Exact-systemPath lookup, falling back to the wildcard ("") systemPath,
    /// within a single already-selected RouteTable.
    std::shared_ptr<IActionHandler> resolveHandler(const RouteTable &table, const QString &systemPath, int index) const;

    /// Finds mode's RouteTable (if any routes have ever been registered
    /// under that mode name) and resolves within it.
    std::shared_ptr<IActionHandler> resolveHandlerForMode(const ModeRouteTables &routesByMode, const QString &mode,
                                                            const QString &systemPath, int index) const;

    /// Sprint 5 (Familias de Modos): resolveHandlerForMode(activeMode) first
    /// - the hot path (a route bound directly in the mode that's actually
    /// active, by far the common case) stays exactly as cheap as it was
    /// before this feature existed: one hash lookup, zero allocations. Only
    /// once that misses does this climb m_modeParents (activeMode's parent,
    /// then its parent, ...), each hop paying one more resolveHandlerForMode
    /// lookup, until one resolves or a mode has no parent left to try. A
    /// QSet<QString> guards the climb against a corrupted/hand-edited
    /// "modeHierarchy" describing a cycle (A's parent is B, B's parent is
    /// A) - allocated only once the fast path has already missed, so it
    /// never costs anything on the common route-found-immediately case
    /// this runs on every single physical axis/button event.
    std::shared_ptr<IActionHandler> resolveHandlerWithFallback(const ModeRouteTables &routesByMode,
                                                                 const QString &activeMode, const QString &systemPath,
                                                                 int index) const;

    /// Moves every entry in map keyed (fromPath, *) to (toPath, *), index
    /// unchanged - shared implementation for swapDeviceSystemPaths()'s pass
    /// over each mode's own RouteTable plus the flat m_buttonStates/
    /// m_axisStates physical-state caches (three structurally-identical
    /// unordered_map<RouteKey, V, RouteKeyHash> instances that only differ
    /// in V). A destination key already present is overwritten by the
    /// moved entry - same "last write wins" semantics as addAxisRoute()/
    /// addButtonRoute() already document.
    template <typename ValueType>
    static void swapSystemPath(std::unordered_map<RouteKey, ValueType, RouteKeyHash> &map, const QString &fromPath,
                                const QString &toPath)
    {
        std::vector<std::pair<RouteKey, ValueType>> moved;
        for (auto it = map.begin(); it != map.end();) {
            if (it->first.first == fromPath) {
                moved.emplace_back(RouteKey{toPath, it->first.second}, std::move(it->second));
                it = map.erase(it);
            } else {
                ++it;
            }
        }
        for (auto &entry : moved) {
            map[entry.first] = std::move(entry.second);
        }
    }

    /// Erases every entry in map keyed (systemPath, *) - onDeviceRemoved()'s
    /// shared implementation, over the same structurally-identical
    /// unordered_map<RouteKey, V, RouteKeyHash> instances swapSystemPath()
    /// above already walks.
    template <typename ValueType>
    static void erasePath(std::unordered_map<RouteKey, ValueType, RouteKeyHash> &map, const QString &systemPath)
    {
        for (auto it = map.begin(); it != map.end();) {
            if (it->first.first == systemPath) {
                it = map.erase(it);
            } else {
                ++it;
            }
        }
    }

    bool m_started = false;

    QTimer *m_tickTimer = nullptr;

    ModeRouteTables m_axisRoutesByMode;
    ModeRouteTables m_buttonRoutesByMode;

    /// Sprint 5 (Familias de Modos): child -> parent mode name. Global never
    /// has an entry here (see setModeParent()) - it is always the tree's
    /// root, reached whenever the climb in resolveHandlerWithFallback() runs
    /// out of parents to try.
    QHash<QString, QString> m_modeParents;

    /// Every virtual output device this router owns/acquired, flushed once per tick.
    std::vector<std::shared_ptr<IVirtualOutputDevice>> m_outputDevices;

    /// Physical press/release state per (systemPath, buttonIndex), updated
    /// in onButtonPressed() before any dispatch - see isButtonPressed().
    std::unordered_map<RouteKey, bool, RouteKeyHash> m_buttonStates;

    /// Last reported raw value per (systemPath, axisIndex), updated in
    /// onAxisMoved() before any dispatch - see getAxisValue().
    std::unordered_map<RouteKey, int, RouteKeyHash> m_axisStates;

    /// Tracks which handlers processed a 'pressed=true' event for a given button,
    /// so the corresponding 'pressed=false' event is guaranteed to reach them
    /// even if the active mode has changed in the meantime (e.g. user released
    /// a mode-shift button before releasing this button).
    std::unordered_map<RouteKey, std::vector<std::shared_ptr<IActionHandler>>, RouteKeyHash> m_activeButtonHandlers;

    mutable QMutex m_modeMutex;
    QString m_currentMode; ///< Guarded by m_modeMutex; see setMode()/currentMode().
};
