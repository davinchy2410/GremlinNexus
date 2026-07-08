#include "SendInputKeyboardBackend.h"

#include <QDebug>

#include <windows.h>

void SendInputKeyboardBackend::sendKey(uint16_t scanCode, bool pressed)
{
    // Scan codes with an 0xE0/0xE1 prefix (e.g. the arrow keys, Right Ctrl/
    // Alt, Insert/Delete/Home/End/PageUp/PageDown) need KEYEVENTF_EXTENDEDKEY
    // alongside the bare low-byte scan code, or Windows resolves them to the
    // wrong (non-extended) key.
    const bool isExtended = (scanCode & 0xFF00) != 0;
    const WORD lowByteScanCode = static_cast<WORD>(scanCode & 0x00FF);

    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = 0; // 0 + KEYEVENTF_SCANCODE => driven purely by scan code, never a virtual-key code.
    input.ki.wScan = lowByteScanCode;
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (isExtended ? KEYEVENTF_EXTENDEDKEY : 0) | (pressed ? 0 : KEYEVENTF_KEYUP);
    input.ki.time = 0;
    input.ki.dwExtraInfo = 0;

    if (SendInput(1, &input, sizeof(INPUT)) != 1) {
        qWarning() << "SendInputKeyboardBackend: SendInput failed for scanCode" << Qt::hex << scanCode << Qt::dec
                    << "pressed" << pressed << "- GetLastError=" << GetLastError();
    }
}
