#include "KernelKeyboardBackend.h"

#include <QDebug>

void KernelKeyboardBackend::sendKey(uint16_t scanCode, bool pressed)
{
    // TODO(Phase 7+): real kernel-mode injection.
    //
    // The intended design is a kernel-mode HID mini-driver / input filter
    // (following the model used by tools like Interception, or a custom
    // signed virtual-keyboard mini-driver) that this class talks to via
    // CreateFile() on the device interface symlink it exposes, then
    // DeviceIoControl(handle, IOCTL_<DRIVER>_SEND_KEY, &report,
    // sizeof(report), nullptr, 0, &bytesReturned, nullptr) to inject a raw
    // keyboard HID input report directly into the kernel's input stack —
    // below the point where user-mode SendInput hooks (and the anti-cheat
    // drivers watching for them, e.g. Vanguard/EasyAntiCheat) can observe
    // or block it.
    //
    // That keeps this class's public surface (sendKey(scanCode, pressed))
    // identical to SendInputKeyboardBackend's, so KeyboardHandler can be
    // pointed at whichever backend is available without any other code
    // (including callers already wired up against IKeyboardBackend)
    // changing when this lands.
    qInfo() << "KernelKeyboardBackend (stub): would inject scanCode" << Qt::hex << scanCode << Qt::dec << "pressed"
            << pressed << "- no kernel driver wired up yet.";
}
