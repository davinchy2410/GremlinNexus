#pragma once

#include "IKeyboardBackend.h"

/**
 * @brief User-mode keyboard backend using the Win32 SendInput() API with
 *        hardware scan codes (KEYEVENTF_SCANCODE), never virtual-key codes.
 *
 * This is the development/testing fallback: SendInput is a well-known,
 * globally-hookable injection point, so aggressive anti-cheat software
 * (Vanguard, EasyAntiCheat, ...) can and does detect input synthesized this
 * way. It exists so KeyboardHandler has something real to drive while
 * KernelKeyboardBackend's actual kernel-mode injection path is built out.
 */
class SendInputKeyboardBackend : public IKeyboardBackend
{
public:
    void sendKey(uint16_t scanCode, bool pressed) override;
};
