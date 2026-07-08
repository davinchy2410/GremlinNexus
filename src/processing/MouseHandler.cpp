#include "MouseHandler.h"

#include <algorithm>
#include <cstdint>

#include <QDebug>

#include <windows.h>

MouseHandler::MouseHandler(MouseAction action)
    : m_action(action)
{
}

void MouseHandler::processAxis(const AxisEvent &evt)
{
    if (m_action == MouseAction::MoveX) {
        sendAbsoluteMove(true, evt.value);
    } else if (m_action == MouseAction::MoveY) {
        sendAbsoluteMove(false, evt.value);
    }
    // Click actions carry no meaning for an axis event.
}

void MouseHandler::processButton(const ButtonEvent &evt)
{
    DWORD flag = 0;
    switch (m_action) {
    case MouseAction::LeftClick:
        flag = evt.pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        break;
    case MouseAction::RightClick:
        flag = evt.pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
        break;
    case MouseAction::MiddleClick:
        flag = evt.pressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
        break;
    case MouseAction::MoveX:
    case MouseAction::MoveY:
        return; // Move actions carry no meaning for a button event.
    }

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flag;

    if (SendInput(1, &input, sizeof(INPUT)) != 1) {
        qWarning() << "MouseHandler: SendInput (click) failed - GetLastError=" << GetLastError();
    }
}

void MouseHandler::sendAbsoluteMove(bool isXAxis, int rawValue) const
{
    const int clampedValue = std::clamp(rawValue, 0, 65535);

    POINT cursor{};
    GetCursorPos(&cursor);

    // -1 in the denominator matches Microsoft's own documented formula for
    // converting a screen pixel coordinate to/from the MOUSEEVENTF_ABSOLUTE
    // [0, 65535] normalized range.
    const int screenWidth = std::max(1, GetSystemMetrics(SM_CXSCREEN) - 1);
    const int screenHeight = std::max(1, GetSystemMetrics(SM_CYSCREEN) - 1);

    const LONG currentNormalizedX = static_cast<LONG>((static_cast<int64_t>(cursor.x) * 65535) / screenWidth);
    const LONG currentNormalizedY = static_cast<LONG>((static_cast<int64_t>(cursor.y) * 65535) / screenHeight);

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = isXAxis ? static_cast<LONG>(clampedValue) : currentNormalizedX;
    input.mi.dy = isXAxis ? currentNormalizedY : static_cast<LONG>(clampedValue);
    input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;

    if (SendInput(1, &input, sizeof(INPUT)) != 1) {
        qWarning() << "MouseHandler: SendInput (move) failed - GetLastError=" << GetLastError();
    }
}
