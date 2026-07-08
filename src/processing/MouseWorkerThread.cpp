#include "MouseWorkerThread.h"

#include <chrono>

#include "Win32MouseInjector.h"

namespace {
/// Fixed tick period: 10ms => 100Hz, matching the class docs' own pacing
/// rationale (an OS-friendly SendInput rate, decoupled from however fast
/// the raw joystick axis itself reports).
constexpr std::chrono::milliseconds kTickInterval{10};
} // namespace

MouseWorkerThread::MouseWorkerThread() = default;

MouseWorkerThread::~MouseWorkerThread()
{
    stop();
}

void MouseWorkerThread::start()
{
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true)) {
        return; // Already running.
    }
    m_thread = std::thread(&MouseWorkerThread::run, this);
}

void MouseWorkerThread::stop()
{
    if (!m_running.exchange(false)) {
        return; // Wasn't running.
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void MouseWorkerThread::setVelocityX(float value)
{
    m_velocityX.store(value, std::memory_order_relaxed);
}

void MouseWorkerThread::setVelocityY(float value)
{
    m_velocityY.store(value, std::memory_order_relaxed);
}

void MouseWorkerThread::run()
{
    // Owned exclusively by this thread - see class docs on why these need
    // no atomics of their own.
    Win32MouseInjector injector;
    float accX = 0.0f;
    float accY = 0.0f;

    while (m_running.load(std::memory_order_relaxed)) {
        const float velocityX = m_velocityX.load(std::memory_order_relaxed);
        const float velocityY = m_velocityY.load(std::memory_order_relaxed);

        accX += velocityX;
        accY += velocityY;

        // Truncation toward zero is intentional, not rounding: it's what
        // makes "subtract the integer part back out" below leave exactly
        // the fractional remainder in the accumulator, regardless of sign,
        // so sub-pixel motion never gets silently dropped - it just carries
        // over and eventually accumulates into a real pixel of movement.
        const int dx = static_cast<int>(accX);
        const int dy = static_cast<int>(accY);

        accX -= static_cast<float>(dx);
        accY -= static_cast<float>(dy);

        if (dx != 0 || dy != 0) {
            injector.moveCursor(dx, dy);
        }

        std::this_thread::sleep_for(kTickInterval);
    }
}
