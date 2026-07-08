#include "Win32MouseInjector.h"

#include <QDebug>
#include <QLatin1String>

#include <windows.h>

void Win32MouseInjector::moveCursor(int deltaX, int deltaY)
{
    if (deltaX == 0 && deltaY == 0) {
        return;
    }

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = deltaX;
    input.mi.dy = deltaY;
    input.mi.dwFlags = MOUSEEVENTF_MOVE; // Relative to current position - no MOUSEEVENTF_ABSOLUTE.

    SendInput(1, &input, sizeof(INPUT));
}

void Win32MouseInjector::sendMouseButton(const QString &buttonType, bool isPressed)
{
    DWORD flag = 0;
    if (buttonType == QLatin1String("Left")) {
        flag = isPressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    } else if (buttonType == QLatin1String("Right")) {
        flag = isPressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    } else if (buttonType == QLatin1String("Middle")) {
        flag = isPressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
    } else {
        qWarning() << "Win32MouseInjector: unrecognized buttonType" << buttonType << "- ignoring";
        return;
    }

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flag;

    SendInput(1, &input, sizeof(INPUT));
}

void Win32MouseInjector::sendMouseScroll(int delta)
{
    if (delta == 0) {
        return;
    }

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.mouseData = static_cast<DWORD>(delta);
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;

    SendInput(1, &input, sizeof(INPUT));
}
