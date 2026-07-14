#pragma once

#include <cstdint>
#include <memory>

#include <QString>

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

    /// Resolves scanCode (same convention as sendKey()/IKeyboardBackend - the
    /// low byte is the raw hardware scan code, a non-zero high byte marks it
    /// "extended", e.g. arrow keys/Insert/Delete/Right Ctrl/Right Alt) into
    /// the exact human-readable key name Windows itself would show for it
    /// (via GetKeyNameTextW - the OS' own scan-code-to-name resolver,
    /// matching the user's actual current keyboard layout rather than a
    /// hardcoded US-QWERTY table), so binding-label previews (Fase - Macro/
    /// Keyboard binding previews) show a real key name instead of just
    /// "Keyboard" or a bare hex scan code. Returns "Key 0x<hex>" for a scan
    /// code Windows can't name (unassigned/invalid) - the same fallback
    /// ActionPickerPopup.qml's own live key-capture already uses when it has
    /// no printable text for a key.
    static QString scanCodeToKeyName(uint16_t scanCode);

private:
    std::shared_ptr<IKeyboardBackend> m_backend;
    uint16_t m_scanCode;
};
