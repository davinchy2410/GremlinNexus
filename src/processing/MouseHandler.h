#pragma once

#include "IActionHandler.h"

/// Which single piece of mouse behavior a given MouseHandler instance drives.
enum class MouseAction
{
    MoveX,
    MoveY,
    LeftClick,
    RightClick,
    MiddleClick,
};

/**
 * @brief Pure mouse emulation via the Win32 SendInput() API: one instance
 *        drives exactly one axis-or-button of mouse behavior, selected by
 *        MouseAction at construction (mirrors KeyboardHandler's one-
 *        instance-one-scanCode shape).
 *
 * MoveX/MoveY are absolute-positioning axis actions: processAxis() maps the
 * incoming [0, 65535] raw value onto that one screen dimension and issues a
 * MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_MOVE SendInput call. Because a single
 * MOUSEEVENTF_ABSOLUTE move must supply *both* X and Y, and this handler
 * only ever hears about the one axis it's bound to, it reads the OS's own
 * current cursor position (GetCursorPos) for the axis it *isn't* driving,
 * so a MoveX event never stomps the Y position (or vice versa) — no shared
 * state between a MoveX and a MoveY handler instance is needed for this,
 * keeping both lock-free.
 *
 * LeftClick/RightClick/MiddleClick are button actions: processButton()
 * sends the matching *DOWN on press and *UP on release.
 *
 * Basic/first-iteration limitation: absolute coordinates are mapped against
 * the primary display only (GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)), not
 * the full virtual desktop (MOUSEEVENTF_VIRTUALDESK) — multi-monitor
 * absolute addressing is a deliberately deferred follow-up.
 */
class MouseHandler : public IActionHandler
{
public:
    explicit MouseHandler(MouseAction action);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

private:
    /// Issues one absolute MOUSEEVENTF_MOVE SendInput call: isXAxis selects
    /// which screen dimension rawValue (already expected in [0, 65535])
    /// drives; the other dimension is read live from GetCursorPos() so it's
    /// left untouched.
    void sendAbsoluteMove(bool isXAxis, int rawValue) const;

    MouseAction m_action;
};
