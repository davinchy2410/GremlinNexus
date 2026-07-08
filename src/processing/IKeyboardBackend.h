#pragma once

#include <cstdint>

/**
 * @brief Abstraction over however a keyboard press/release actually gets
 *        injected into the OS.
 *
 * Deliberately not tied to a specific driver/API: KeyboardHandler only
 * depends on this interface, so the actual injection mechanism (user-mode
 * SendInput today, a kernel-mode driver later — see KernelKeyboardBackend)
 * can be swapped without touching any routing/handler code.
 */
class IKeyboardBackend
{
public:
    virtual ~IKeyboardBackend() = default;

    /// Presses (pressed=true) or releases (pressed=false) the key
    /// identified by scanCode (a PS/2 Set 1 hardware scan code, optionally
    /// with an 0xE0/0xE1 extended-key prefix in the high byte — NOT a
    /// virtual-key code).
    virtual void sendKey(uint16_t scanCode, bool pressed) = 0;
};
