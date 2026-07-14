#include "ViGEmDevice.h"

#include <QDebug>

ViGEmDevice::ViGEmDevice(int slotId)
    : m_slotId(slotId)
    , m_library(QStringLiteral("ViGEmClient"))
{
}

ViGEmDevice::~ViGEmDevice()
{
    relinquish();
}

bool ViGEmDevice::ensureLibraryLoaded()
{
    if (m_library.isLoaded()) {
        return true;
    }

    if (!m_library.load()) {
        qWarning() << "ViGEmDevice: failed to load ViGEmClient.dll:" << m_library.errorString();
        return false;
    }

    m_vigemAlloc = reinterpret_cast<VigemAlloc_t>(m_library.resolve("vigem_alloc"));
    m_vigemFree = reinterpret_cast<VigemFree_t>(m_library.resolve("vigem_free"));
    m_vigemConnect = reinterpret_cast<VigemConnect_t>(m_library.resolve("vigem_connect"));
    m_vigemDisconnect = reinterpret_cast<VigemDisconnect_t>(m_library.resolve("vigem_disconnect"));
    m_vigemTargetX360Alloc = reinterpret_cast<VigemTargetX360Alloc_t>(m_library.resolve("vigem_target_x360_alloc"));
    m_vigemTargetFree = reinterpret_cast<VigemTargetFree_t>(m_library.resolve("vigem_target_free"));
    m_vigemTargetAdd = reinterpret_cast<VigemTargetAdd_t>(m_library.resolve("vigem_target_add"));
    m_vigemTargetRemove = reinterpret_cast<VigemTargetRemove_t>(m_library.resolve("vigem_target_remove"));
    m_vigemTargetX360Update =
        reinterpret_cast<VigemTargetX360Update_t>(m_library.resolve("vigem_target_x360_update"));

    if (!m_vigemAlloc || !m_vigemFree || !m_vigemConnect || !m_vigemDisconnect || !m_vigemTargetX360Alloc ||
        !m_vigemTargetFree || !m_vigemTargetAdd || !m_vigemTargetRemove || !m_vigemTargetX360Update) {
        qWarning() << "ViGEmDevice: ViGEmClient.dll is missing one or more required exports"
                       " (vigem_alloc/vigem_connect/vigem_target_x360_alloc/vigem_target_add/...)";
        m_library.unload();
        m_vigemAlloc = nullptr;
        m_vigemFree = nullptr;
        m_vigemConnect = nullptr;
        m_vigemDisconnect = nullptr;
        m_vigemTargetX360Alloc = nullptr;
        m_vigemTargetFree = nullptr;
        m_vigemTargetAdd = nullptr;
        m_vigemTargetRemove = nullptr;
        m_vigemTargetX360Update = nullptr;
        return false;
    }

    return true;
}

bool ViGEmDevice::acquire()
{
    if (m_acquired) {
        return true;
    }

    if (!ensureLibraryLoaded()) {
        return false;
    }

    m_client = m_vigemAlloc();
    if (!m_client) {
        qWarning() << "ViGEmDevice: vigem_alloc failed";
        return false;
    }

    const VIGEM_ERROR connectResult = m_vigemConnect(m_client);
    if (connectResult != kVigemErrorNone) {
        qWarning() << "ViGEmDevice: vigem_connect failed (is the ViGEmBus driver installed and running?),"
                       " error code" << Qt::hex << Qt::showbase << connectResult;
        m_vigemFree(m_client);
        m_client = nullptr;
        return false;
    }

    m_target = m_vigemTargetX360Alloc();
    if (!m_target) {
        qWarning() << "ViGEmDevice: vigem_target_x360_alloc failed";
        m_vigemDisconnect(m_client);
        m_vigemFree(m_client);
        m_client = nullptr;
        return false;
    }

    const VIGEM_ERROR addResult = m_vigemTargetAdd(m_client, m_target);
    if (addResult != kVigemErrorNone) {
        qWarning() << "ViGEmDevice: vigem_target_add failed (no free ViGEmBus slot?), error code" << Qt::hex
                    << Qt::showbase << addResult;
        m_vigemTargetFree(m_target);
        m_target = nullptr;
        m_vigemDisconnect(m_client);
        m_vigemFree(m_client);
        m_client = nullptr;
        return false;
    }

    // Unlike VJoyDevice's own equivalent bugfix, this already IS the
    // correctly-centered state, not just a reset: XusbReport's signed
    // thumbstick fields (sThumbLX/LY/RX/RY, range [-32768, 32767]) have 0 as
    // their true center, and its unsigned trigger fields (bLeftTrigger/
    // bRightTrigger, range [0, 255]) have 0 as their correct "unpulled" rest
    // position - both coincide with a plain value-initialized XusbReport{},
    // unlike vJoy's own [0, 32767] unsigned axis range where 0 means the
    // extreme minimum instead. Reassigned explicitly (rather than relying
    // on the constructor's one-time initialization) so a re-acquire() after
    // a prior relinquish() also starts from this same known-centered state,
    // not whatever m_state was left holding beforehand.
    m_state = XusbReport{};
    // Force a push on the next update() even on a re-acquire where m_dirty
    // might otherwise still read false (from a successful update() in a
    // prior acquired session, untouched by relinquish()) - the target just
    // added is a brand new ViGEmBus object that has never received a
    // report at all, so update()'s dirty-gate must not skip its first one.
    m_dirty = true;
    m_acquired = true;
    return true;
}

void ViGEmDevice::relinquish()
{
    if (!m_acquired) {
        return;
    }

    if (m_client && m_target && m_vigemTargetRemove) {
        m_vigemTargetRemove(m_client, m_target);
    }
    if (m_target && m_vigemTargetFree) {
        m_vigemTargetFree(m_target);
    }
    if (m_client) {
        if (m_vigemDisconnect) {
            m_vigemDisconnect(m_client);
        }
        if (m_vigemFree) {
            m_vigemFree(m_client);
        }
    }

    m_target = nullptr;
    m_client = nullptr;
    m_acquired = false;
}

void ViGEmDevice::setAxis(int axisIndex, int value)
{
    switch (axisIndex) {
    case 0: {
        const auto v = static_cast<int16_t>(value);
        if (m_state.sThumbLX != v) { m_state.sThumbLX = v; m_dirty = true; }
        break;
    }
    case 1: {
        const auto v = static_cast<int16_t>(value);
        if (m_state.sThumbLY != v) { m_state.sThumbLY = v; m_dirty = true; }
        break;
    }
    case 2: {
        const auto v = static_cast<int16_t>(value);
        if (m_state.sThumbRX != v) { m_state.sThumbRX = v; m_dirty = true; }
        break;
    }
    case 3: {
        const auto v = static_cast<int16_t>(value);
        if (m_state.sThumbRY != v) { m_state.sThumbRY = v; m_dirty = true; }
        break;
    }
    case 4: {
        const auto v = static_cast<uint8_t>(value);
        if (m_state.bLeftTrigger != v) { m_state.bLeftTrigger = v; m_dirty = true; }
        break;
    }
    case 5: {
        const auto v = static_cast<uint8_t>(value);
        if (m_state.bRightTrigger != v) { m_state.bRightTrigger = v; m_dirty = true; }
        break;
    }
    default:
        qWarning() << "ViGEmDevice: axisIndex" << axisIndex << "out of range [0," << kMaxAxes << ")";
        break;
    }
}

void ViGEmDevice::setButton(int buttonIndex, bool pressed)
{
    // Bit values match XINPUT_GAMEPAD/XUSB_REPORT's wButtons layout
    // (Xusb.h's XUSB_BUTTON enum); index 10 (Guide/XBOX button) has no
    // standard XInput bit and is intentionally not wired to a real one.
    static constexpr uint16_t kButtonBits[ViGEmDevice::kMaxButtons] = {
        0x0001, // 0: DPAD_UP
        0x0002, // 1: DPAD_DOWN
        0x0004, // 2: DPAD_LEFT
        0x0008, // 3: DPAD_RIGHT
        0x0010, // 4: START
        0x0020, // 5: BACK
        0x0040, // 6: LEFT_THUMB
        0x0080, // 7: RIGHT_THUMB
        0x0100, // 8: LEFT_SHOULDER
        0x0200, // 9: RIGHT_SHOULDER
        0x0000, // 10: GUIDE (no standard XInput bit - no-op)
        0x1000, // 11: A
        0x2000, // 12: B
        0x4000, // 13: X
        0x8000, // 14: Y
    };

    if (buttonIndex < 0 || buttonIndex >= kMaxButtons) {
        qWarning() << "ViGEmDevice: buttonIndex" << buttonIndex << "out of range [0," << kMaxButtons << ")";
        return;
    }

    const uint16_t mask = kButtonBits[buttonIndex];
    const uint16_t before = m_state.wButtons;
    if (pressed) {
        m_state.wButtons |= mask;
    } else {
        m_state.wButtons &= static_cast<uint16_t>(~mask);
    }
    if (m_state.wButtons != before) {
        m_dirty = true;
    }
}

void ViGEmDevice::setHat(int hatIndex, int /*value*/)
{
    // XInput has no POV-hat concept distinct from the D-Pad, which is
    // already exposed as buttons 0-3 (see setButton()); there is nothing
    // meaningful for a hat call to do here.
    qWarning() << "ViGEmDevice: setHat(" << hatIndex
               << ") ignored - XInput has no hat/POV, use setButton(0..3) for the D-Pad";
}

void ViGEmDevice::setHatDirection(int hatIndex, int direction, bool pressed)
{
    if (hatIndex != 0) {
        // XInput only ever exposes one D-Pad - a second/third/fourth "hat"
        // has nothing to route to, so this is a silent no-op rather than a
        // warning (HatRemapHandler bindings targeting vJoy hats > 0 are
        // expected to simply do nothing on a ViGEm target).
        return;
    }
    if (direction < 0 || direction > 3) {
        qWarning() << "ViGEmDevice: direction" << direction << "out of range [0, 3] (0=Up, 1=Right, 2=Down, 3=Left)";
        return;
    }

    // Our discrete direction order (0=Up, 1=Right, 2=Down, 3=Left) doesn't
    // match setButton()'s D-Pad bit order (0=DPAD_UP, 1=DPAD_DOWN,
    // 2=DPAD_LEFT, 3=DPAD_RIGHT, fixed by XUSB_REPORT's own wire format) -
    // translate rather than reordering either table.
    static constexpr int kDirectionToDpadButton[4] = {0, 3, 1, 2};
    setButton(kDirectionToDpadButton[direction], pressed);
}

bool ViGEmDevice::update()
{
    if (!m_acquired || !m_vigemTargetX360Update) {
        return false;
    }
    if (!m_dirty) {
        return true;
    }
    const bool ok = m_vigemTargetX360Update(m_client, m_target, m_state) == kVigemErrorNone;
    if (ok) {
        m_dirty = false;
    }
    return ok;
}
