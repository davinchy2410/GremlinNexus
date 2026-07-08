#pragma once

#include <atomic>
#include <thread>

/**
 * @brief Background 100Hz relative-mouse-movement pump.
 *
 * Owns a single free-running std::thread - not a QThread/QObject, since
 * this loop needs nothing from Qt's event loop (no signals/slots, no
 * message pump), just a fixed 10ms sleep_for loop. One instance is meant to
 * be shared by an X and a Y MouseRelativeAxisHandler pair (see that class'
 * own docs), each writing into its own axis' velocity independently.
 *
 * Threading/lock-free design: velocityX/velocityY are std::atomic<float>,
 * written from whichever thread EventRouter::onAxisMoved() runs on (one
 * writer per axis) and read every tick by this class' own background
 * thread (the only reader) - a classic single-writer/single-reader pair
 * per atomic, so relaxed memory ordering is sufficient: there is no other
 * state a velocity write needs to stay causally ordered against, and a
 * torn/stale read merely means the cursor moves at last tick's speed for
 * one more 10ms tick, not a correctness issue. accX/accY (the sub-pixel
 * accumulators) are owned exclusively by the background thread itself and
 * never touched from outside it, so they need no synchronization at all.
 *
 * Decoupling *why*: a raw joystick axis can report at up to 250Hz (see
 * DeviceMonitorWorker's own flush-cadence docs), but SendInput should be
 * paced at a fixed, OS-friendly rate rather than fired once per raw HID
 * event - this class is that pacing boundary. MouseRelativeAxisHandler's
 * processAxis() never calls Win32MouseInjector itself; it only ever writes
 * a target velocity here.
 */
class MouseWorkerThread
{
public:
    MouseWorkerThread();
    ~MouseWorkerThread();

    // Owns a live background thread and its own start/stop lifecycle - not
    // meaningful to copy or move.
    MouseWorkerThread(const MouseWorkerThread &) = delete;
    MouseWorkerThread &operator=(const MouseWorkerThread &) = delete;

    /// Starts the background loop. Safe to call more than once; a call
    /// while already running is ignored.
    void start();

    /// Signals the loop to stop and joins the thread before returning. Safe
    /// to call when not running (no-op). Also called from the destructor,
    /// so a MouseWorkerThread never outlives its own background thread.
    void stop();

    /// Lock-free target-velocity setters, in pixels *per 10ms tick* (not
    /// pixels/second - see run()'s own docs) at whatever rate
    /// MouseRelativeAxisHandler computed from its own sensitivity/deadzone.
    /// Safe to call from any thread.
    void setVelocityX(float value);
    void setVelocityY(float value);

private:
    /// The background loop itself - see class docs for the accumulator/
    /// atomics design. Runs until m_running is cleared by stop().
    void run();

    std::thread m_thread;
    std::atomic<bool> m_running{false};

    std::atomic<float> m_velocityX{0.0f};
    std::atomic<float> m_velocityY{0.0f};
};
