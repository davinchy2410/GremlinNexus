#pragma once

#include "IKeyboardBackend.h"

/**
 * @brief Stub for a future kernel-mode keyboard injection backend.
 *
 * Not implemented yet — see sendKey()'s body for the intended design. This
 * class exists now purely so KeyboardHandler's public contract (constructed
 * against an IKeyboardBackend) doesn't need to change when the real
 * implementation lands; only the backend passed to it does.
 */
class KernelKeyboardBackend : public IKeyboardBackend
{
public:
    void sendKey(uint16_t scanCode, bool pressed) override;
};
