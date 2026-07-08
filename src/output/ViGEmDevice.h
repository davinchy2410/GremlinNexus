#pragma once

#include <cstdint>

#include <QLibrary>

#include "IVirtualOutputDevice.h"

/**
 * @brief ViGEmBus-backed virtual Xbox 360 controller (XInput), for titles
 *        that only read XInput and ignore generic DirectInput devices like
 *        vJoy (see VJoyDevice).
 *
 * Talks to ViGEmClient.dll, resolved dynamically at runtime via QLibrary,
 * mirroring VJoyDevice's design exactly: no link-time/SDK-header dependency,
 * so GremblingEx builds and runs (minus this one output backend) even on a
 * machine without ViGEmBus installed. If the DLL, any of its required
 * exports, or the vigem_alloc/vigem_connect/vigem_target_x360_alloc/
 * vigem_target_add init chain fails, acquire() logs a warning and returns
 * false instead of crashing; every other call is then a safe no-op until a
 * later acquire() succeeds.
 *
 * The handful of ViGEmClient types/functions/constants declared here are
 * the minimum needed to drive one X360 pad and are reproduced from
 * ViGEmClient's public C API (Nefarius ViGEmClient, ViGEmClient.h / Xusb.h)
 * rather than included from the SDK, since only the DLL itself is a runtime
 * dependency here, not the SDK's headers/import lib.
 */
class ViGEmDevice : public IVirtualOutputDevice
{
public:
    /// LX, LY, RX, RY (signed thumbsticks) then LeftTrigger, RightTrigger
    /// (unsigned, 0-255) — see setAxis().
    static constexpr int kMaxAxes = 6;

    /// D-Pad Up/Down/Left/Right, Start, Back, LThumb, RThumb, LShoulder,
    /// RShoulder, Guide, A, B, X, Y — see setButton().
    static constexpr int kMaxButtons = 15;

    /// @param slotId Logical Xbox 360 controller slot [1, 4] this instance
    ///               was created for (VirtualOutputManager::getViGEmDevice()'s
    ///               own cache key) - purely a GremblingEx-side bookkeeping
    ///               value, not anything ViGEmBus itself assigns, but needed
    ///               so deviceId() can round-trip a ViGEm binding's
    ///               "targetOutputId" through toJson() the same way
    ///               VJoyDevice's real device ID does. Defaults to 0 (the
    ///               prior "not applicable" sentinel) for any caller that
    ///               doesn't care about serialization.
    explicit ViGEmDevice(int slotId = 0);
    ~ViGEmDevice() override;

    ViGEmDevice(const ViGEmDevice &) = delete;
    ViGEmDevice &operator=(const ViGEmDevice &) = delete;

    /// Fase 0 (stabilization): the vigem_alloc/vigem_connect chain below
    /// relies on SetupAPI/WinUsb, which expects the calling thread's COM
    /// apartment to already be initialized - main.cpp does this once,
    /// globally, for the whole process lifetime (see its own comment by
    /// the CoInitializeEx call) rather than this class managing
    /// Co(Un)Initialize itself per instance, since every ViGEmDevice call
    /// happens on that same main thread anyway (see EventRouter's threading
    /// docs) and a per-instance pair would risk one instance's relinquish()
    /// tearing down the apartment out from under another still using it.
    bool acquire() override;
    void relinquish() override;
    void setAxis(int axisIndex, int value) override;
    void setButton(int buttonIndex, bool pressed) override;
    void setHat(int hatIndex, int value) override;
    void setHatDirection(int hatIndex, int direction, bool pressed) override;
    bool update() override;

    /// Returns the logical slotId passed to the constructor - see its own
    /// docs. Unlike vJoy, ViGEmBus itself has no per-target numeric ID (one
    /// ViGEmDevice == one ad-hoc X360 target); this is GremblingEx's own
    /// bookkeeping value, not anything the driver assigns.
    int deviceId() const override { return m_slotId; }
    bool isViGEmDevice() const override { return true; }

private:
    // Opaque ViGEmClient handles (struct _VIGEM_CLIENT_T*/_VIGEM_TARGET_T*
    // in the real SDK; the real layout is never touched here, only passed
    // back to the DLL, so an opaque forward-declared struct is sufficient).
    struct VigemClientT;
    struct VigemTargetT;
    using PVIGEM_CLIENT = VigemClientT *;
    using PVIGEM_TARGET = VigemTargetT *;

    /// ViGEmClient's VIGEM_ERROR is a big enum; the only value this class
    /// needs to recognize is the "call succeeded" sentinel (VIGEM_ERROR_NONE
    /// == 0x20000000 in the public SDK) — every other value is a failure.
    using VIGEM_ERROR = unsigned int;
    static constexpr VIGEM_ERROR kVigemErrorNone = 0x20000000u;

    /// Binary layout of an XInput report, matching ViGEmClient's XUSB_REPORT
    /// (Xusb.h) field-for-field: this is wire ABI shared with the driver via
    /// vigem_target_x360_update, not just an API shape, so field order/
    /// types/count must not be changed without matching the SDK.
    struct XusbReport
    {
        uint16_t wButtons = 0;
        uint8_t bLeftTrigger = 0;
        uint8_t bRightTrigger = 0;
        int16_t sThumbLX = 0;
        int16_t sThumbLY = 0;
        int16_t sThumbRX = 0;
        int16_t sThumbRY = 0;
    };

    using VigemAlloc_t = PVIGEM_CLIENT(__stdcall *)();
    using VigemFree_t = void(__stdcall *)(PVIGEM_CLIENT);
    using VigemConnect_t = VIGEM_ERROR(__stdcall *)(PVIGEM_CLIENT);
    using VigemDisconnect_t = void(__stdcall *)(PVIGEM_CLIENT);
    using VigemTargetX360Alloc_t = PVIGEM_TARGET(__stdcall *)();
    using VigemTargetFree_t = void(__stdcall *)(PVIGEM_TARGET);
    using VigemTargetAdd_t = VIGEM_ERROR(__stdcall *)(PVIGEM_CLIENT, PVIGEM_TARGET);
    using VigemTargetRemove_t = VIGEM_ERROR(__stdcall *)(PVIGEM_CLIENT, PVIGEM_TARGET);
    using VigemTargetX360Update_t = VIGEM_ERROR(__stdcall *)(PVIGEM_CLIENT, PVIGEM_TARGET, XusbReport);

    /// Loads ViGEmClient.dll and resolves the functions this class needs,
    /// the first time it's called. Returns false (and logs via qWarning)
    /// without throwing if the DLL or any required export is missing.
    bool ensureLibraryLoaded();

    bool m_acquired = false;
    int m_slotId = 0;

    QLibrary m_library;
    VigemAlloc_t m_vigemAlloc = nullptr;
    VigemFree_t m_vigemFree = nullptr;
    VigemConnect_t m_vigemConnect = nullptr;
    VigemDisconnect_t m_vigemDisconnect = nullptr;
    VigemTargetX360Alloc_t m_vigemTargetX360Alloc = nullptr;
    VigemTargetFree_t m_vigemTargetFree = nullptr;
    VigemTargetAdd_t m_vigemTargetAdd = nullptr;
    VigemTargetRemove_t m_vigemTargetRemove = nullptr;
    VigemTargetX360Update_t m_vigemTargetX360Update = nullptr;

    PVIGEM_CLIENT m_client = nullptr;
    PVIGEM_TARGET m_target = nullptr;

    XusbReport m_state{};
};
