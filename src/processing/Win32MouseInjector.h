#pragma once

#include <QString>

/**
 * @brief Thin, stateless wrapper over the Win32 SendInput() API for
 *        everything mouse-related: relative cursor movement, clicks, and
 *        the scroll wheel.
 *
 * Used by MouseRelativeAxisHandler (movement, via the shared
 * MouseWorkerThread - see that class' own docs for why movement is paced
 * on a background thread) and MouseButtonHandler (clicks/scroll, called
 * directly and synchronously from processButton() - see that class' own
 * docs on why no pacing/thread is needed there). Every method here is a
 * single SendInput() call - stateless, side-effect-free beyond that one OS
 * call, and safe to construct/use from any thread.
 */
class Win32MouseInjector
{
public:
    /// Moves the OS cursor by (deltaX, deltaY) pixels relative to its
    /// current position, via SendInput(INPUT_MOUSE, MOUSEEVENTF_MOVE).
    /// Returns immediately without touching the OS if both deltas are 0 -
    /// SendInput is a real driver-level call, not worth paying for a no-op.
    void moveCursor(int deltaX, int deltaY);

    /// Sends one mouse button down/up event via SendInput(INPUT_MOUSE).
    /// buttonType is "Left", "Right", or "Middle" (case-sensitive) - any
    /// other value is a no-op (logged via qWarning), not a crash.
    void sendMouseButton(const QString &buttonType, bool isPressed);

    /// Sends one scroll-wheel tick via SendInput(INPUT_MOUSE,
    /// MOUSEEVENTF_WHEEL). delta is in the same units as Win32's own
    /// WHEEL_DELTA (120 = one notch up, -120 = one notch down). Returns
    /// immediately without touching the OS if delta is 0.
    void sendMouseScroll(int delta);
};
