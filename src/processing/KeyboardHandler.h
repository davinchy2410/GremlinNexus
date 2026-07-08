#pragma once

#include <cstdint>
#include <memory>

#include "IActionHandler.h"

class IKeyboardBackend;

/**
 * @brief Routes a physical button's press/release to a keyboard key via a
 *        pluggable IKeyboardBackend.
 *
 * Holds no opinion about *how* the key gets injected — that's entirely
 * IKeyboardBackend's job (SendInputKeyboardBackend vs. the future
 * KernelKeyboardBackend). Axis events carry no meaning here and are ignored.
 */
class KeyboardHandler : public IActionHandler
{
public:
    /**
     * @param backend  Injection backend to drive.
     * @param scanCode Target hardware scan code (see IKeyboardBackend::sendKey).
     */
    KeyboardHandler(std::shared_ptr<IKeyboardBackend> backend, uint16_t scanCode);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    /// Rebuilds this handler's "KeyboardHandler" binding JSON (Fase 10.8),
    /// matching ProfileManager::instantiateKeyboardHandler()'s schema.
    QJsonObject toJson() const override;

private:
    std::shared_ptr<IKeyboardBackend> m_backend;
    uint16_t m_scanCode;
};
