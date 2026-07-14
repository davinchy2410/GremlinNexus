#include "KeyboardHandler.h"

#include <QLatin1String>

#include <windows.h>

#include "IKeyboardBackend.h"

KeyboardHandler::KeyboardHandler(std::shared_ptr<IKeyboardBackend> backend, uint16_t scanCode)
    : m_backend(std::move(backend))
    , m_scanCode(scanCode)
{
}

void KeyboardHandler::processAxis(const AxisEvent & /*evt*/)
{
    // Keyboard remaps have no axis equivalent.
}

void KeyboardHandler::processButton(const ButtonEvent &evt)
{
    if (m_backend) {
        m_backend->sendKey(m_scanCode, evt.pressed);
    }
}

QJsonObject KeyboardHandler::toJson() const
{
    QJsonObject binding;
    binding[QLatin1String("actionType")] = QStringLiteral("KeyboardHandler");

    QJsonObject parameters;
    parameters[QLatin1String("scanCode")] = static_cast<int>(m_scanCode);
    binding[QLatin1String("parameters")] = parameters;
    return binding;
}

QString KeyboardHandler::scanCodeToKeyName(uint16_t scanCode)
{
    // Same low-byte-scan-code + non-zero-high-byte-means-extended convention
    // SendInputKeyboardBackend::sendKey() already uses - see its own docs.
    // GetKeyNameTextW's lParam is shaped like a WM_KEYDOWN message's own:
    // bits 16-23 carry the scan code, bit 24 is the extended-key flag.
    const LPARAM lParam = (static_cast<LPARAM>(scanCode & 0x00FF) << 16) | ((scanCode & 0xFF00) ? (1L << 24) : 0);

    wchar_t buffer[64] = {};
    const int length = GetKeyNameTextW(static_cast<LONG>(lParam), buffer, static_cast<int>(sizeof(buffer) / sizeof(buffer[0])));
    if (length > 0) {
        return QString::fromWCharArray(buffer, length);
    }
    // Same fallback shape ActionPickerPopup.qml's own live key-capture
    // already uses when it has no printable text for a key.
    return QStringLiteral("Key 0x%1").arg(scanCode, 0, 16);
}
