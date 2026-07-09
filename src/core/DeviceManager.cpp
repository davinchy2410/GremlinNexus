#include "DeviceManager.h"
#include "ShutdownTrace.h"

#include <QElapsedTimer>
#include <QHash>
#include <QMetaType>
#include <QReadLocker>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <QWriteLocker>

#include <windows.h>
#include <dbt.h>
#include <setupapi.h>
#include <cfgmgr32.h>

// hidusage.h/hidpi.h/hidsdi.h ship without extern "C" guards on this MinGW
// SDK, so their HidP_*/HidD_* declarations get C++ name-mangled unless we
// wrap them ourselves; the import library (hid.lib) only exports plain C
// symbols, so without this the final link fails with "undefined reference".
extern "C" {
#include <hidusage.h>
#include <hidpi.h>
#include <hidsdi.h>
}

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Well-known device-interface class GUID for HID devices
// ({4D1E55B2-F16F-11CF-88CB-001111000030}).
//
// Defined by hand (instead of pulling in <hidclass.h>, which mixes poorly
// with plain user-mode <windows.h> on some MinGW SDK layouts) so this file
// has no dependency beyond the standard <dbt.h> / <hidpi.h> / <hidsdi.h>
// headers already required for RawInput/HID parsing.
// ---------------------------------------------------------------------------
constexpr GUID kGuidDevInterfaceHid = {
    0x4d1e55b2L, 0xf16f, 0x11cf, {0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}
};

// ---------------------------------------------------------------------------
// DEVPKEY_Device_BusReportedDeviceDesc ({540b947e-8b40-45bc-a8a2-6a0b894cbda2}, 4).
//
// Defined by hand instead of including <devpkey.h>: that header's
// DEFINE_DEVPROPKEY macro only DECLARES this key extern unless INITGUID is
// defined before including it, and this MinGW SDK ships no import library
// that actually provides the symbol - linking would fail with "undefined
// reference". #define-ing INITGUID here instead would risk turning every
// other GUID this file pulls in via <windows.h>/<setupapi.h> into a full
// (re)definition too, the same category of header conflict already avoided
// above for kGuidDevInterfaceHid.
// ---------------------------------------------------------------------------
constexpr DEVPROPKEY kDevPropKeyBusReportedDeviceDesc = {
    {0x540b947e, 0x8b40, 0x45bc, {0xa8, 0xa2, 0x6a, 0x0b, 0x89, 0x4c, 0xbd, 0xa2}}, 4
};

namespace {

// Fase SC-6: 0x08 = Multi-axis Controller - the HID usage Virpil (and other
// premium HOTAS brands') pedals/throttles present themselves as, distinct
// from the plain Joystick (0x04)/Gamepad (0x05) usages this app originally
// only recognized/registered for. Used as a literal rather than
// HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER since not every SDK this project
// builds against is guaranteed to define that macro.
constexpr USHORT kHidUsageMultiAxisController = 0x08;

/// Per-device state needed to interpret subsequent WM_INPUT HID reports.
struct HidDeviceContext
{
    QString systemPath;

    /// Raw PHIDP_PREPARSED_DATA blob returned by GetRawInputDeviceInfo;
    /// buttonCaps/valueCaps and every HidP_Get* call below point into it.
    std::vector<uint8_t> preparsedDataBuffer;

    std::vector<HIDP_BUTTON_CAPS> buttonCaps;
    std::vector<HIDP_VALUE_CAPS> valueCaps;

    /// Real physical buttons plus 4 synthetic ones (Up/Right/Down/Left) per
    /// hat, appended after the real buttons - see hatCount and
    /// parseHidReport() (Fase 16.7).
    int buttonCount = 0;
    int axisCount = 0; ///< True analog axes only - hats are buttons now, not axes.
    int hatCount = 0;  ///< POV hat switches; each contributes 4 entries at the end of buttonCount, not an axis.

    /// Last known state, used to emit only on actual change. lastButtonStates
    /// covers both real and synthetic (hat) buttons - size == buttonCount.
    QVector<bool> lastButtonStates;
    QVector<int> lastAxisValues; ///< size == axisCount.

    /// Standardized engine-facing axis index for each analog-axis position
    /// encountered while iterating valueCaps (skipping hats), in that same
    /// iteration order - registerHidDevice() builds this once from the
    /// device's declared HID usages (X=0, Y=1, Z=2, Rx=3, Ry=4, Rz=5,
    /// Slider=6, Dial=7, anything else/any collision starting at 8), and
    /// parseHidReport() walks valueCaps in the identical order at report-
    /// parsing time, consuming one entry per position to recover the same
    /// standardized index without recomputing the mapping per report. This
    /// exists because a HID device's own valueCaps report order is
    /// arbitrary - a device reporting X, Y, Slider, Z in that order would
    /// otherwise get axisRunningIndex-assigned indices 0,1,2,3 (Slider
    /// landing on index 2, Z on 3), silently disagreeing with DirectInput/
    /// vJoy/Joystick Gremlin's own fixed X=0/Y=1/Z=2/... convention that
    /// every imported profile and this app's own DeviceTesterView assume.
    std::vector<int> axisIndices;
};

/// Parses the VID/PID out of a RawInput device path such as
/// "\\?\HID#VID_045E&PID_028E&IG_00#7&1234abcd&0&0000#{...}".
void parseVidPid(const QString &devicePath, QString &vendorId, QString &productId)
{
    static const QRegularExpression pattern(QStringLiteral("VID_([0-9A-Fa-f]{4})&PID_([0-9A-Fa-f]{4})"));
    const QRegularExpressionMatch match = pattern.match(devicePath);
    vendorId = match.hasMatch() ? match.captured(1).toUpper() : QStringLiteral("0000");
    productId = match.hasMatch() ? match.captured(2).toUpper() : QStringLiteral("0000");
}

/// Diagnostic-only (Heisenbug investigation): stringifies GetLastError()
/// (e.g. "ERROR_ACCESS_DENIED (5)") instead of a bare numeric code, so a
/// silently-failing Win32 call - CreateFileW returning INVALID_HANDLE_VALUE,
/// GetRawInputDeviceList/RegisterRawInputDevices returning an error -
/// actually says what blocked it in logShutdownTrace()/qWarning() output.
QString formatLastError(DWORD errorCode)
{
    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    QString text = length > 0 ? QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed()
                               : QStringLiteral("<unknown error>");
    if (buffer) {
        LocalFree(buffer);
    }
    return QStringLiteral("%1 (0x%2)").arg(text).arg(errorCode, 0, 16);
}

} // namespace

/**
 * @brief Background worker that owns all OS-level device monitoring.
 *
 * Lives on DeviceManager's dedicated QThread (see DeviceManager::initialize).
 * Every Windows API call, RawInput enumeration and HID report parse happens
 * here, on the worker thread; only fully-processed, plain-data results
 * (DeviceInfo / axis / button values) cross over to DeviceManager, via
 * queued signal/slot connections.
 *
 * Fase 0 (stabilization): a high-report-rate HOTAS (some report at up to
 * 1000 Hz) moving a single axis would otherwise emit axisMoved() - and
 * therefore post a Qt::QueuedConnection event into the main/GUI thread's
 * event loop - up to 1000 times/sec, which is enough to visibly lag input
 * processing and UI responsiveness under sustained stick movement. Axis
 * events are instead accumulated into m_pendingAxisValues (latest value per
 * (systemPath, axisIndex) wins, exactly like ctx.lastAxisValues' own
 * per-report dedup, just at a coarser cadence) and flushed by
 * m_axisFlushTimer at kAxisFlushIntervalMs instead of once per HID report -
 * capping the queued-event rate at a fixed ceiling regardless of how fast
 * the physical device reports, while staying well under the 200 Hz
 * (5 ms) output tick (EventRouter::tick()) so no perceptible extra latency
 * is added on top of what the output side already has. Buttons are exempt -
 * discrete press/release transitions can't occur anywhere near 1000/sec, so
 * throttling them would only add real input lag for no benefit.
 */
class DeviceMonitorWorker : public QObject
{
    Q_OBJECT

public:
    explicit DeviceMonitorWorker(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~DeviceMonitorWorker() override
    {
        // teardownNativeResources() is idempotent (null-guarded), so if
        // DeviceManager::shutdown() already ran it via BlockingQueuedConnection
        // before deleting this object, this is just a harmless no-op safety
        // net - not the primary teardown path anymore.
        logShutdownTrace(QStringLiteral("~DeviceMonitorWorker: enter (hwnd=%1, deviceContexts=%2)")
                              .arg(reinterpret_cast<quintptr>(m_hwnd))
                              .arg(m_deviceContexts.size()));
        teardownNativeResources();
        logShutdownTrace(QStringLiteral("~DeviceMonitorWorker: exit"));
    }

public slots:
    /**
     * @brief Entry point invoked once the worker has been moved to its
     *        monitoring thread and that thread's event loop has started.
     */
    void startMonitoring()
    {
        if (!createMessageWindow()) {
            return;
        }
        registerForDeviceNotifications();
        registerForRawInput();
        performInitialScan();
        scheduleStartupWarmupScans();

        // Created here (not in the constructor) so it's already parented on
        // this worker's own thread - see the class docs above for why this
        // exists.
        m_axisFlushTimer = new QTimer(this);
        connect(m_axisFlushTimer, &QTimer::timeout, this, &DeviceMonitorWorker::flushPendingAxisEvents);
        m_axisFlushTimer->start(kAxisFlushIntervalMs);
    }

    /**
     * @brief Explicit, synchronous native-resource teardown: unregisters
     *        raw input (RIDEV_REMOVE) and device-change notifications, then
     *        destroys the message-only window.
     *
     * Called by DeviceManager::shutdown() via Qt::BlockingQueuedConnection
     * *while this worker's own thread event loop is still running* (i.e.
     * before QThread::quit()), so every one of these calls executes on the
     * same thread that created m_hwnd in the first place - Windows requires
     * DestroyWindow (and, in practice, a window's raw input/notification
     * registrations) to be torn down from the thread that owns that window's
     * message queue, not an arbitrary other thread. Idempotent: every step
     * is guarded on the handle it clears, so calling this twice (e.g. once
     * explicitly, once again from the destructor as a safety net) is safe.
     */
    void teardownNativeResources()
    {
        if (m_axisFlushTimer) {
            m_axisFlushTimer->stop();
        }

        if (m_deviceNotifyHandle) {
            const BOOL unregistered = UnregisterDeviceNotification(m_deviceNotifyHandle);
            if (!unregistered) {
                logShutdownTrace(QStringLiteral("teardownNativeResources: UnregisterDeviceNotification FAILED - %1")
                                      .arg(formatLastError(GetLastError())));
            }
            m_deviceNotifyHandle = nullptr;
        }
        if (m_hwnd) {
            // Stop receiving raw input before the window disappears.
            RAWINPUTDEVICE devices[3] = {
                {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_JOYSTICK, RIDEV_REMOVE, nullptr},
                {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_GAMEPAD, RIDEV_REMOVE, nullptr},
                {HID_USAGE_PAGE_GENERIC, kHidUsageMultiAxisController, RIDEV_REMOVE, nullptr},
            };
            if (!RegisterRawInputDevices(devices, 3, sizeof(RAWINPUTDEVICE))) {
                logShutdownTrace(QStringLiteral("teardownNativeResources: RegisterRawInputDevices(RIDEV_REMOVE) FAILED - %1")
                                      .arg(formatLastError(GetLastError())));
            } else {
                logShutdownTrace(QStringLiteral("teardownNativeResources: RIDEV_REMOVE OK"));
            }

            if (!DestroyWindow(m_hwnd)) {
                logShutdownTrace(QStringLiteral("teardownNativeResources: DestroyWindow FAILED - %1")
                                      .arg(formatLastError(GetLastError())));
            }
            m_hwnd = nullptr;
        }
    }

signals:
    /// Raised when a device is found, carrying its fully-populated metadata.
    void deviceDiscovered(const DeviceInfo &device);

    /// Raised when a previously known device disappears.
    void deviceLost(const QString &systemPath);

    /// Raised when an axis/hat value changes (already de-duplicated).
    void axisMoved(const QString &systemPath, int axisIndex, int value);

    /// Raised when a button's pressed state changes (already de-duplicated).
    void buttonPressed(const QString &systemPath, int buttonIndex, bool pressed);

    /// Raised once per HID report with every button that changed state in
    /// that same report (Fase 20.32) - lets EventRouter resolve a Shift held
    /// simultaneously with the buttons it's meant to gate, instead of only
    /// seeing them one at a time via the buttonPressed() signal above.
    void buttonsChanged(const QVector<ButtonEvent> &events);

private:
    // -- Setup -------------------------------------------------------------

    bool createMessageWindow()
    {
        static const wchar_t *kClassName = L"GremblingExDeviceMonitorWindowClass";

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = &DeviceMonitorWorker::staticWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;

        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            logShutdownTrace(QStringLiteral("createMessageWindow: RegisterClassExW FAILED - %1")
                                  .arg(formatLastError(GetLastError())));
            return false;
        }

        // HWND_MESSAGE => message-only window: never visible, never receives
        // paint/input-focus traffic, only used as a native message sink.
        m_hwnd = CreateWindowExW(0, kClassName, L"GremblingExDeviceMonitor", 0,
                                  0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, this);
        if (!m_hwnd) {
            logShutdownTrace(QStringLiteral("createMessageWindow: CreateWindowExW FAILED - %1")
                                  .arg(formatLastError(GetLastError())));
        }
        return m_hwnd != nullptr;
    }

    void registerForDeviceNotifications()
    {
        DEV_BROADCAST_DEVICEINTERFACE_W filter{};
        filter.dbcc_size = sizeof(filter);
        filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        filter.dbcc_classguid = kGuidDevInterfaceHid;

        m_deviceNotifyHandle = RegisterDeviceNotificationW(m_hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
        if (!m_deviceNotifyHandle) {
            logShutdownTrace(QStringLiteral("registerForDeviceNotifications: RegisterDeviceNotificationW FAILED - %1")
                                  .arg(formatLastError(GetLastError())));
        }
    }

    void registerForRawInput()
    {
        // RIDEV_INPUTSINK: keep receiving WM_INPUT even while our (invisible,
        // never-focused) message-only window doesn't have keyboard focus.
        // Fase SC-6: also registers kHidUsageMultiAxisController (0x08) -
        // registerHidDevice()'s own filter accepting that usage isn't
        // enough on its own, since performInitialScan() (GetRawInputDeviceList)
        // and live WM_INPUT delivery are two independent things: without
        // this, a Multi-axis Controller device would show up in the
        // Profiles device list but never actually report input.
        RAWINPUTDEVICE devices[3] = {
            {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_JOYSTICK, RIDEV_INPUTSINK, m_hwnd},
            {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_GAMEPAD, RIDEV_INPUTSINK, m_hwnd},
            {HID_USAGE_PAGE_GENERIC, kHidUsageMultiAxisController, RIDEV_INPUTSINK, m_hwnd},
        };
        if (!RegisterRawInputDevices(devices, 3, sizeof(RAWINPUTDEVICE))) {
            logShutdownTrace(QStringLiteral("registerForRawInput: RegisterRawInputDevices FAILED - %1")
                                  .arg(formatLastError(GetLastError())));
        }
    }

    // -- Enumeration ---------------------------------------------------------

    /// Re-enumerates every currently attached RawInput HID device and emits
    /// deviceDiscovered() for any joystick/gamepad not already tracked.
    /// Safe to call repeatedly: registerHidDevice() is a no-op for devices
    /// already present in m_deviceContexts.
    void performInitialScan()
    {
        UINT numDevices = 0;
        const UINT precheck = GetRawInputDeviceList(nullptr, &numDevices, sizeof(RAWINPUTDEVICELIST));
        if (precheck != 0) {
            logShutdownTrace(QStringLiteral("performInitialScan: GetRawInputDeviceList(count query) FAILED - %1")
                                  .arg(formatLastError(GetLastError())));
            return;
        }
        if (numDevices == 0) {
            logShutdownTrace(QStringLiteral("performInitialScan: GetRawInputDeviceList reports 0 attached devices"));
            return;
        }

        std::vector<RAWINPUTDEVICELIST> deviceList(numDevices);
        const UINT listed = GetRawInputDeviceList(deviceList.data(), &numDevices, sizeof(RAWINPUTDEVICELIST));
        if (listed == static_cast<UINT>(-1)) {
            logShutdownTrace(QStringLiteral("performInitialScan: GetRawInputDeviceList(fetch) FAILED - %1")
                                  .arg(formatLastError(GetLastError())));
            return;
        }
        deviceList.resize(listed);

        for (const RAWINPUTDEVICELIST &entry : deviceList) {
            if (entry.dwType == RIM_TYPEHID) {
                registerHidDevice(entry.hDevice);
            }
        }
    }

    /// Schedules a few blind re-scans after startup. Needed because a HID
    /// filter driver sitting on top of a device (e.g. HidHide uncloaking a
    /// just-whitelisted .exe) can take up to ~1-2s to expose every endpoint
    /// of a composite device, so the very first performInitialScan() at
    /// startMonitoring() time may only see a subset of what's actually
    /// attached. Each rescan is idempotent (registerHidDevice() skips
    /// already-tracked devices), so this only ever adds late-arriving
    /// devices - never duplicates or disrupts ones already found. Uses
    /// singleShot (same idiom as the WM_DEVICEARRIVAL rescan above) so no
    /// extra member/cleanup bookkeeping is needed: the timers are owned by
    /// the thread's event loop and simply never fire if the worker is
    /// destroyed first.
    void scheduleStartupWarmupScans()
    {
        for (int delayMs : {500, 1000, 2000}) {
            QTimer::singleShot(delayMs, this, &DeviceMonitorWorker::performInitialScan);
        }
    }

    /// Queries capabilities/metadata for hDevice and, if it is a joystick or
    /// gamepad not already tracked, stores its HidDeviceContext and emits
    /// deviceDiscovered(). Any failed/unsupported step aborts silently: a
    /// single misbehaving device must never take down the whole scan.
    void registerHidDevice(HANDLE hDevice)
    {
        if (m_deviceContexts.find(hDevice) != m_deviceContexts.end()) {
            return;
        }

        RID_DEVICE_INFO deviceInfo{};
        deviceInfo.cbSize = sizeof(deviceInfo);
        UINT size = sizeof(deviceInfo);
        if (GetRawInputDeviceInfoW(hDevice, RIDI_DEVICEINFO, &deviceInfo, &size) == static_cast<UINT>(-1)) {
            logShutdownTrace(QStringLiteral("registerHidDevice(hDevice=%1): GetRawInputDeviceInfoW(RIDI_DEVICEINFO) FAILED - %2")
                                  .arg(reinterpret_cast<quintptr>(hDevice))
                                  .arg(formatLastError(GetLastError())));
            return;
        }
        if (deviceInfo.dwType != RIM_TYPEHID) {
            return;
        }

        const bool isJoystickOrGamepad =
            deviceInfo.hid.usUsagePage == HID_USAGE_PAGE_GENERIC &&
            (deviceInfo.hid.usUsage == HID_USAGE_GENERIC_JOYSTICK ||
             deviceInfo.hid.usUsage == HID_USAGE_GENERIC_GAMEPAD ||
             deviceInfo.hid.usUsage == kHidUsageMultiAxisController);
        if (!isJoystickOrGamepad) {
            return;
        }

        // Device path (also serves as our unique systemPath key).
        UINT nameSize = 0;
        GetRawInputDeviceInfoW(hDevice, RIDI_DEVICENAME, nullptr, &nameSize);
        if (nameSize == 0) {
            logShutdownTrace(QStringLiteral("registerHidDevice(hDevice=%1): GetRawInputDeviceInfoW(RIDI_DEVICENAME) size query returned 0 - %2")
                                  .arg(reinterpret_cast<quintptr>(hDevice))
                                  .arg(formatLastError(GetLastError())));
            return;
        }
        std::vector<wchar_t> nameBuffer(nameSize);
        if (GetRawInputDeviceInfoW(hDevice, RIDI_DEVICENAME, nameBuffer.data(), &nameSize) == static_cast<UINT>(-1)) {
            logShutdownTrace(QStringLiteral("registerHidDevice(hDevice=%1): GetRawInputDeviceInfoW(RIDI_DEVICENAME) FAILED - %2")
                                  .arg(reinterpret_cast<quintptr>(hDevice))
                                  .arg(formatLastError(GetLastError())));
            return;
        }
        const QString devicePath = QString::fromWCharArray(nameBuffer.data());

        // Preparsed HID data, needed for capability + report parsing.
        UINT preparsedSize = 0;
        GetRawInputDeviceInfoW(hDevice, RIDI_PREPARSEDDATA, nullptr, &preparsedSize);
        if (preparsedSize == 0) {
            logShutdownTrace(QStringLiteral("registerHidDevice: %1 - GetRawInputDeviceInfoW(RIDI_PREPARSEDDATA) size query returned 0 - %2")
                                  .arg(devicePath, formatLastError(GetLastError())));
            return;
        }
        std::vector<uint8_t> preparsedBuffer(preparsedSize);
        if (GetRawInputDeviceInfoW(hDevice, RIDI_PREPARSEDDATA, preparsedBuffer.data(), &preparsedSize) ==
            static_cast<UINT>(-1)) {
            logShutdownTrace(QStringLiteral("registerHidDevice: %1 - GetRawInputDeviceInfoW(RIDI_PREPARSEDDATA) FAILED - %2")
                                  .arg(devicePath, formatLastError(GetLastError())));
            return;
        }
        auto *preparsedData = reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsedBuffer.data());

        HIDP_CAPS caps{};
        if (HidP_GetCaps(preparsedData, &caps) != HIDP_STATUS_SUCCESS) {
            logShutdownTrace(QStringLiteral("registerHidDevice: %1 - HidP_GetCaps FAILED").arg(devicePath));
            return;
        }

        std::vector<HIDP_BUTTON_CAPS> buttonCaps;
        if (caps.NumberInputButtonCaps > 0) {
            USHORT count = caps.NumberInputButtonCaps;
            buttonCaps.resize(count);
            if (HidP_GetButtonCaps(HidP_Input, buttonCaps.data(), &count, preparsedData) == HIDP_STATUS_SUCCESS) {
                buttonCaps.resize(count);
            } else {
                buttonCaps.clear();
            }
        }

        std::vector<HIDP_VALUE_CAPS> valueCaps;
        if (caps.NumberInputValueCaps > 0) {
            USHORT count = caps.NumberInputValueCaps;
            valueCaps.resize(count);
            if (HidP_GetValueCaps(HidP_Input, valueCaps.data(), &count, preparsedData) == HIDP_STATUS_SUCCESS) {
                valueCaps.resize(count);
            } else {
                valueCaps.clear();
            }
        }

        uint32_t buttonCount = 0;
        for (const HIDP_BUTTON_CAPS &bc : buttonCaps) {
            buttonCount += bc.IsRange ? static_cast<uint32_t>(bc.Range.UsageMax - bc.Range.UsageMin + 1) : 1u;
        }

        uint32_t hatCount = 0;
        for (const HIDP_VALUE_CAPS &vc : valueCaps) {
            const bool isHat = !vc.IsRange && vc.NotRange.Usage == HID_USAGE_GENERIC_HATSWITCH;
            if (isHat) {
                hatCount += vc.IsRange ? static_cast<uint32_t>(vc.Range.UsageMax - vc.Range.UsageMin + 1) : 1u;
            }
        }

        // Standardize each analog axis' engine-facing index to DirectInput/
        // vJoy/Joystick Gremlin's own fixed convention (X=0, Y=1, Z=2,
        // Rx=3, Ry=4, Rz=5, Slider=6, Dial=7) instead of this device's own
        // arbitrary valueCaps report order - a device reporting X, Y,
        // Slider, Z in that order would otherwise get Slider assigned index
        // 2 and Z index 3 via a simple running counter, silently
        // disagreeing with every other tool that assumes the fixed
        // convention. Anything that isn't one of those 8 generic usages, or
        // collides with a standardized index another axis already claimed,
        // gets the next free index starting from 8 upward. axisIndices is
        // built here (one entry per analog-axis position, skipping hats, in
        // valueCaps iteration order) and stored on ctx so parseHidReport()
        // can recover the same mapping per report without recomputing it.
        std::vector<int> axisIndices;
        std::vector<int> rawLogicalMin;
        std::vector<int> rawLogicalMax;
        bool usedIndices[256] = {false};
        int nextFreeIndex = 8;
        for (const HIDP_VALUE_CAPS &vc : valueCaps) {
            const bool isHat = !vc.IsRange && vc.NotRange.Usage == HID_USAGE_GENERIC_HATSWITCH;
            if (isHat) {
                continue;
            }
            const USAGE usageMin = vc.IsRange ? vc.Range.UsageMin : vc.NotRange.Usage;
            const USAGE usageMax = vc.IsRange ? vc.Range.UsageMax : vc.NotRange.Usage;

            for (int usage = usageMin; usage <= usageMax; ++usage) {
                int standardIndex = -1;
                if (vc.UsagePage == HID_USAGE_PAGE_GENERIC) {
                    switch (usage) {
                        case HID_USAGE_GENERIC_X: standardIndex = 0; break;
                        case HID_USAGE_GENERIC_Y: standardIndex = 1; break;
                        case HID_USAGE_GENERIC_Z: standardIndex = 2; break;
                        case HID_USAGE_GENERIC_RX: standardIndex = 3; break;
                        case HID_USAGE_GENERIC_RY: standardIndex = 4; break;
                        case HID_USAGE_GENERIC_RZ: standardIndex = 5; break;
                        case HID_USAGE_GENERIC_SLIDER: standardIndex = 6; break;
                        case HID_USAGE_GENERIC_DIAL: standardIndex = 7; break;
                        default: break;
                    }
                }
                if (standardIndex < 0 || usedIndices[standardIndex]) {
                    while (nextFreeIndex < 256 && usedIndices[nextFreeIndex]) {
                        ++nextFreeIndex;
                    }
                    standardIndex = nextFreeIndex;
                }
                if (standardIndex >= 0 && standardIndex < 256) {
                    usedIndices[standardIndex] = true;
                }
                axisIndices.push_back(standardIndex);
                rawLogicalMin.push_back(static_cast<int>(vc.LogicalMin));
                rawLogicalMax.push_back(static_cast<int>(vc.LogicalMax));
            }
        }

        int maxAxisIndex = -1;
        for (const int standardIndex : axisIndices) {
            maxAxisIndex = std::max(maxAxisIndex, standardIndex);
        }
        const uint32_t finalAxisCount = static_cast<uint32_t>(maxAxisIndex + 1);

        // Per-axis raw HID logical range (Fase 16.6). A HID device's native
        // range varies (e.g. this project has seen both a 16-bit-range vJoy
        // device and a 12-bit-range VKBsim stick), so nothing downstream
        // should assume a fixed [0, 65535]. Sized to finalAxisCount (not
        // just however many axes this device actually declares) so a
        // standardized index with no matching declared axis still gets a
        // real (if unused) slot, keeping every axis-indexed array the same
        // shape as Joystick Gremlin/vJoy's own fixed-position convention.
        std::vector<int> axisLogicalMin(finalAxisCount, 0);
        std::vector<int> axisLogicalMax(finalAxisCount, 65535);
        for (std::size_t i = 0; i < axisIndices.size(); ++i) {
            const int standardIndex = axisIndices[i];
            if (standardIndex >= 0 && static_cast<uint32_t>(standardIndex) < finalAxisCount) {
                axisLogicalMin[standardIndex] = rawLogicalMin[i];
                axisLogicalMax[standardIndex] = rawLogicalMax[i];
            }
        }

        // Fase 16.7: each POV hat becomes 4 synthetic buttons (Up/Right/
        // Down/Left - see parseHidReport()), appended after the device's
        // real physical buttons, instead of a fake analog axis. Much easier
        // to bind per-direction than a single 0-8 hat value.
        const uint32_t totalButtonCount = buttonCount + hatCount * 4;

        HidDeviceContext ctx;
        ctx.systemPath = devicePath;
        ctx.preparsedDataBuffer = std::move(preparsedBuffer);
        ctx.buttonCaps = std::move(buttonCaps);
        ctx.valueCaps = std::move(valueCaps);
        ctx.axisIndices = std::move(axisIndices);
        ctx.buttonCount = static_cast<int>(std::min<uint32_t>(totalButtonCount, 255));
        ctx.axisCount = static_cast<int>(std::min<uint32_t>(finalAxisCount, 255));
        ctx.hatCount = static_cast<int>(std::min<uint32_t>(hatCount, 255));
        ctx.lastButtonStates.assign(ctx.buttonCount, false);
        ctx.lastAxisValues.assign(ctx.axisCount, std::numeric_limits<int>::min());

        DeviceInfo info;
        info.systemPath = devicePath;
        info.deviceName = queryProductName(devicePath);

        parseVidPid(devicePath, info.vendorId, info.productId);
        info.isConnected = true;
        info.numAxes = static_cast<uint8_t>(ctx.axisCount);
        info.numButtons = static_cast<uint8_t>(ctx.buttonCount);
        info.numHats = static_cast<uint8_t>(ctx.hatCount);
        info.axisLogicalMin = QVector<int>(axisLogicalMin.begin(), axisLogicalMin.end());
        info.axisLogicalMax = QVector<int>(axisLogicalMax.begin(), axisLogicalMax.end());

        m_deviceContexts.emplace(hDevice, std::move(ctx));

        emit deviceDiscovered(info);
    }

    /// Opens the device just long enough to ask the driver for its
    /// human-readable product string. dwDesiredAccess=0 is a query-only
    /// open, which Windows permits even for system keyboard/mouse-class HID
    /// devices that reject GENERIC_READ/WRITE from unprivileged processes.
    ///
    /// This CreateFile+HidD_GetProductString round-trip still goes through
    /// any third-party filter driver a user has installed (e.g. HidHide,
    /// manually whitelisted by the user themselves - GremblingNexus no
    /// longer manages that whitelist itself) like any other open of the
    /// device, unlike RawInput's own data delivery (RIDI_DEVICEINFO/report
    /// parsing), which never opens the device from this process at all - so
    /// a device can be fully readable for axes/buttons/VID/PID while this
    /// call alone comes back empty. When that happens,
    /// queryProductNameFromRegistry() below is used as a fallback instead of
    /// giving up.
    QString queryProductName(const QString &devicePath) const
    {
        QString productName;

        HANDLE fileHandle = CreateFileW(reinterpret_cast<LPCWSTR>(devicePath.utf16()), 0,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (fileHandle != INVALID_HANDLE_VALUE) {
            wchar_t buffer[256] = {};
            if (!HidD_GetProductString(fileHandle, buffer, sizeof(buffer))) {
                // No GetLastError() here on purpose: HidD_GetProductString is
                // a HID minidriver IOCTL wrapper, not a plain Win32 API - an
                // empty/failed result (denied open, cloak race, no USB string
                // descriptor) doesn't reliably set the thread-last-error the
                // way CreateFileW below does.
                logShutdownTrace(QStringLiteral("queryProductName: %1 - HidD_GetProductString returned no string, falling back to registry").arg(devicePath));
            } else {
                productName = QString::fromWCharArray(buffer).trimmed();
            }
            // Handle-management audit: this is the only HANDLE this function
            // ever opens, and it's always closed on every path that opened it
            // - confirmed here rather than just trusted, since a leaked HID
            // handle held across a Quit->relaunch cycle would itself explain
            // devices coming back "busy"/inaccessible to the new process.
            if (!CloseHandle(fileHandle)) {
                logShutdownTrace(QStringLiteral("queryProductName: %1 - CloseHandle FAILED - %2")
                                      .arg(devicePath, formatLastError(GetLastError())));
            }
        } else {
            logShutdownTrace(QStringLiteral("queryProductName: %1 - CreateFileW FAILED - %2")
                                  .arg(devicePath, formatLastError(GetLastError())));
        }

        if (productName.isEmpty()) {
            productName = queryProductNameFromRegistry(devicePath);
        }

        return productName.isEmpty() ? QStringLiteral("Unknown HID Device") : productName;
    }

    /// Fallback for when queryProductName()'s live HidD_GetProductString
    /// call fails or comes back empty (denied open, cloak/whitelist race, or
    /// a device whose firmware never populated a USB string descriptor in
    /// the first place). Reads the same friendly name Windows itself shows
    /// in Device Manager straight out of the registry via SetupAPI -
    /// SPDRP_FRIENDLYNAME/SPDRP_DEVICEDESC are PnP/INF metadata the OS
    /// already cached at driver-install time, not a live descriptor
    /// round-trip, so this never opens the device (no CreateFile at all) and
    /// is therefore immune to HidHide's cloak and to any exclusive-access
    /// contention that blocks the live query above.
    QString queryProductNameFromRegistry(const QString &devicePath) const
    {
        HDEVINFO deviceInfoSet =
            SetupDiGetClassDevsW(&kGuidDevInterfaceHid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (deviceInfoSet == INVALID_HANDLE_VALUE) {
            return QString();
        }

        QString result;
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);

        for (DWORD index = 0;
             SetupDiEnumDeviceInterfaces(deviceInfoSet, nullptr, &kGuidDevInterfaceHid, index, &interfaceData);
             ++index) {
            DWORD requiredSize = 0;
            SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &interfaceData, nullptr, 0, &requiredSize, nullptr);
            if (requiredSize == 0) {
                continue;
            }

            std::vector<BYTE> detailBuffer(requiredSize);
            auto *detailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailBuffer.data());
            detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

            SP_DEVINFO_DATA devInfoData{};
            devInfoData.cbSize = sizeof(devInfoData);

            if (!SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &interfaceData, detailData, requiredSize, nullptr,
                                                   &devInfoData)) {
                continue;
            }

            // RawInput's own RIDI_DEVICENAME path and SetupDi's DevicePath
            // for the same physical interface routinely differ only in
            // case (RawInput tends to hand back all-lowercase paths), hence
            // the case-insensitive compare below.
            const QString interfacePath = QString::fromWCharArray(detailData->DevicePath);
            if (interfacePath.compare(devicePath, Qt::CaseInsensitive) != 0) {
                continue;
            }

            // The HID collection node itself (devInfoData) almost always
            // carries a generic, OS-localized class name - "HID-compliant
            // game controller" / "Dispositivo de juego compatible con HID" -
            // for composite USB devices (e.g. VIRPIL panels), because that
            // name comes from the generic HID class driver's own INF, not
            // the device's actual USB string descriptors. The real
            // manufacturer/product string instead lives on the PARENT node
            // (the USB function node usbccgp creates for this interface),
            // under DEVPKEY_Device_BusReportedDeviceDesc specifically - a
            // property the USB bus driver populates directly from the
            // device's iProduct/iManufacturer descriptor at enumeration
            // time, independent of whichever class driver later attached a
            // generic display name to that same node. So: walk up one level
            // via CM_Get_Parent() and read that property from there first.
            DEVINST parentDevInst = 0;
            if (CM_Get_Parent(&parentDevInst, devInfoData.DevInst, 0) == CR_SUCCESS) {
                wchar_t propBuffer[256] = {};
                ULONG propSize = sizeof(propBuffer);
                DEVPROPTYPE propType = 0;
                if (CM_Get_DevNode_PropertyW(parentDevInst, &kDevPropKeyBusReportedDeviceDesc, &propType,
                                              reinterpret_cast<PBYTE>(propBuffer), &propSize, 0) == CR_SUCCESS &&
                    propType == DEVPROP_TYPE_STRING) {
                    result = QString::fromWCharArray(propBuffer).trimmed();
                }

                if (result.isEmpty()) {
                    // Older/simpler drivers may not populate
                    // BusReportedDeviceDesc at all - CM_DRP_DEVICEDESC on
                    // the same parent node is the classic (pre-DEVPROPKEY)
                    // registry property with the same intent, and still
                    // frequently holds the real product string when the
                    // newer property comes back empty.
                    wchar_t descBuffer[256] = {};
                    ULONG descSize = sizeof(descBuffer);
                    ULONG descRegType = 0;
                    if (CM_Get_DevNode_Registry_PropertyW(parentDevInst, CM_DRP_DEVICEDESC, &descRegType,
                                                           descBuffer, &descSize, 0) == CR_SUCCESS) {
                        result = QString::fromWCharArray(descBuffer).trimmed();
                    }
                }
            }

            if (result.isEmpty()) {
                // Last resort: the HID node's own name. Generic for
                // composite devices, but still better than "Unknown HID
                // Device" for anything the parent-node lookup above missed.
                wchar_t nameBuffer[256] = {};
                if (SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &devInfoData, SPDRP_FRIENDLYNAME, nullptr,
                                                       reinterpret_cast<PBYTE>(nameBuffer), sizeof(nameBuffer),
                                                       nullptr) ||
                    SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &devInfoData, SPDRP_DEVICEDESC, nullptr,
                                                       reinterpret_cast<PBYTE>(nameBuffer), sizeof(nameBuffer),
                                                       nullptr)) {
                    result = QString::fromWCharArray(nameBuffer).trimmed();
                }
            }
            break;
        }

        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return result;
    }

    /// Finds the tracked device whose systemPath corresponds to the
    /// DEV_BROADCAST_DEVICEINTERFACE path from a DBT_DEVICEREMOVECOMPLETE
    /// notification and drops it. Windows doesn't guarantee identical
    /// casing/formatting between this path and RawInput's RIDI_DEVICENAME,
    /// so the match is a case-insensitive substring check rather than exact
    /// equality.
    void handleDeviceInterfaceRemoved(const QString &interfacePath)
    {
        for (auto it = m_deviceContexts.begin(); it != m_deviceContexts.end(); ++it) {
            const QString &known = it->second.systemPath;
            if (known.contains(interfacePath, Qt::CaseInsensitive) || interfacePath.contains(known, Qt::CaseInsensitive)) {
                const QString removedPath = known;
                m_deviceContexts.erase(it);
                emit deviceLost(removedPath);
                return;
            }
        }
    }

    // -- Raw input report parsing --------------------------------------------

    void processHidInput(const RAWINPUT *raw)
    {
        const auto it = m_deviceContexts.find(raw->header.hDevice);
        if (it == m_deviceContexts.end()) {
            return;
        }
        HidDeviceContext &ctx = it->second;

        const BYTE *reports = raw->data.hid.bRawData;
        for (DWORD i = 0; i < raw->data.hid.dwCount; ++i) {
            parseHidReport(ctx, reports + static_cast<size_t>(i) * raw->data.hid.dwSizeHid, raw->data.hid.dwSizeHid);
        }
    }

    /// Decodes a POV hat's raw value into its 4 cardinal-direction booleans
    /// (Fase 16.7, continuous hats added Fase 20.16). logicalMax (from the
    /// same HIDP_VALUE_CAPS this value came from) tells discrete and
    /// continuous hats apart: a discrete hat reports 0-8 (0=Up, 1=Up+Right,
    /// ..., 7=Up+Left, 8/out-of-range=released); a continuous hat reports a
    /// full 0-35999 (1/100-degree) sweep instead, which used to fall
    /// through every discrete case except 0 (Up) straight to "released" -
    /// this is why only Up ever worked on a continuous physical hat. For a
    /// continuous hat, the value is instead bucketed into one of 8 45-degree
    /// sectors around the same Up/Right/Down/Left/diagonal layout.
    static void decodeHatDirections(ULONG value, LONG logicalMax, bool &up, bool &right, bool &down, bool &left)
    {
        up = right = down = left = false;

        if (logicalMax <= 8) {
            switch (value) {
            case 0: up = true; break;
            case 1: up = true; right = true; break;
            case 2: right = true; break;
            case 3: right = true; down = true; break;
            case 4: down = true; break;
            case 5: down = true; left = true; break;
            case 6: left = true; break;
            case 7: left = true; up = true; break;
            default: break; // 8 = released; anything else is treated the same way.
            }
            return;
        }

        // Continuous hat (e.g. 0-35999) - anything past logicalMax (a
        // device's own "released" sentinel, e.g. 65535 or -1) is centered.
        if (value > static_cast<ULONG>(logicalMax) && value != 0) {
            return;
        }
        ULONG sectorSize = (static_cast<ULONG>(logicalMax) + 1) / 8;
        if (sectorSize == 0) {
            sectorSize = 4500; // Safety fallback (equivalent to a 0-35999 range).
        }

        ULONG sector = (value + (sectorSize / 2)) / sectorSize;
        sector = sector % 8; // Wraps 360 degrees back around to 0 (Up).

        switch (sector) {
        case 0: up = true; break;
        case 1: up = true; right = true; break;
        case 2: right = true; break;
        case 3: right = true; down = true; break;
        case 4: down = true; break;
        case 5: down = true; left = true; break;
        case 6: left = true; break;
        case 7: left = true; up = true; break;
        }
    }

    /// Decodes one HID input report against ctx's cached button/value caps,
    /// diffs it against ctx's last known state, and emits buttonPressed /
    /// axisMoved only for the values that actually changed. Fase 16.7: a POV
    /// hat's value is decoded into 4 synthetic buttons (Up/Right/Down/Left)
    /// merged into the same currentButtons array as the device's real
    /// buttons - see registerHidDevice()'s buttonCount (= real buttons +
    /// hatCount*4) - rather than reported as a fake analog axis.
    void parseHidReport(HidDeviceContext &ctx, const BYTE *reportData, DWORD reportSize)
    {
        auto *preparsedData = reinterpret_cast<PHIDP_PREPARSED_DATA>(ctx.preparsedDataBuffer.data());
        // The HidP_* API takes a non-const PCHAR report buffer even though it
        // only reads from it; there is no const-correct overload.
        auto *mutableReport = reinterpret_cast<PCHAR>(const_cast<BYTE *>(reportData));

        const int physicalButtonCount = ctx.buttonCount - ctx.hatCount * 4;

        // Fase (bugfix - spurious release/press pairs): currentButtons used
        // to default every index to false regardless of whether this
        // report's HidP_GetUsages call for that button's own range actually
        // SUCCEEDED. A device with buttons split across multiple
        // HIDP_BUTTON_CAPS ranges can have HidP_GetUsages transiently fail
        // (HIDP_STATUS_BUFFER_TOO_SMALL/HIDP_STATUS_INCOMPATIBLE_REPORT_ID/...)
        // for ONE range on ONE report - a plausible trigger being another
        // range's field changing width/content in the same report when a
        // second button (in a DIFFERENT range) is pressed. The old code
        // silently treated that failure as "every button in this range is
        // released" (currentButtons' own false default), diffed that against
        // ctx.lastButtonStates, emitted a spurious release for every
        // already-held button in the failed range, and then - the very next,
        // successfully-parsed report - emitted an immediate spurious re-press
        // once the real (unchanged) state was read correctly again. That
        // exact "released, then immediately re-pressed" pair is what broke
        // TemporaryModeSwitchHandler: its debounce only rate-limits a second
        // PRESS edge, so the spurious RELEASE half of the pair still lands
        // and drops the shift out of its target mode, and the compensating
        // re-press then arrives too soon after and gets swallowed by that
        // same debounce, leaving the shift stuck deactivated.
        //
        // Fix: seed currentButtons from the OLD state (ctx.lastButtonStates)
        // rather than a blanket false, then only overwrite a given range's
        // slice once HidP_GetUsages actually succeeds for it - a successful
        // read's "not in the returned usage list" still correctly means
        // released (every index in that range gets explicitly written, true
        // or false, by the loop below), but a FAILED read leaves that
        // range's indices exactly as they were on the last known-good report
        // instead of being cleared out from under the diff before it's even
        // calculated.
        QVector<bool> currentButtons = ctx.lastButtonStates;
        currentButtons.resize(ctx.buttonCount, false);
        int buttonBase = 0;
        for (const HIDP_BUTTON_CAPS &bc : ctx.buttonCaps) {
            const USAGE usageMin = bc.IsRange ? bc.Range.UsageMin : bc.NotRange.Usage;
            const USAGE usageMax = bc.IsRange ? bc.Range.UsageMax : bc.NotRange.Usage;
            const int rangeCount = usageMax - usageMin + 1;

            // Fase (bugfix, applied preemptively): same LinkCollection
            // reasoning as HidP_GetUsageValue's own fix below - a device
            // with buttons split across multiple HIDP_BUTTON_CAPS entries in
            // DIFFERENT link collections (rather than one big range) would
            // have every range's HidP_GetUsages call here scoped to link
            // collection 0 instead of bc's own, same latent class of bug as
            // the hats one.
            ULONG usageListLength = static_cast<ULONG>(rangeCount);
            std::vector<USAGE> usages(usageListLength);
            const NTSTATUS status = HidP_GetUsages(HidP_Input, bc.UsagePage, bc.LinkCollection, usages.data(),
                                                     &usageListLength, preparsedData, mutableReport, reportSize);
            if (status == HIDP_STATUS_SUCCESS) {
                // A successful read is authoritative for this whole range -
                // clear it first (every slot defaults to released) so a
                // button that WAS held but is no longer in the returned
                // usage list is correctly detected as released, not left at
                // its seeded old value.
                for (int local = 0; local < rangeCount; ++local) {
                    const int globalIndex = buttonBase + local;
                    if (globalIndex < physicalButtonCount) {
                        currentButtons[globalIndex] = false;
                    }
                }
                for (ULONG i = 0; i < usageListLength; ++i) {
                    const int localIndex = static_cast<int>(usages[i]) - usageMin;
                    const int globalIndex = buttonBase + localIndex;
                    if (localIndex >= 0 && localIndex < rangeCount && globalIndex < physicalButtonCount) {
                        currentButtons[globalIndex] = true;
                    }
                }
            }
            // status != HIDP_STATUS_SUCCESS: leave this range's slice exactly
            // as seeded from ctx.lastButtonStates above - see this block's
            // own comment for why that's the fix, not a fallback default.
            buttonBase += rangeCount;
        }

        std::size_t axisPosition = 0; ///< Indexes ctx.axisIndices - advances once per non-hat position, in lockstep with registerHidDevice()'s own identical iteration order.
        int hatIndex = 0;
        for (const HIDP_VALUE_CAPS &vc : ctx.valueCaps) {
            const bool isHat = !vc.IsRange && vc.NotRange.Usage == HID_USAGE_GENERIC_HATSWITCH;
            const USAGE usageMin = vc.IsRange ? vc.Range.UsageMin : vc.NotRange.Usage;
            const USAGE usageMax = vc.IsRange ? vc.Range.UsageMax : vc.NotRange.Usage;

            for (int usage = usageMin; usage <= usageMax; ++usage) {
                // CRITICAL: this position's standardized index is resolved -
                // and axisPosition advanced - before HidP_GetUsageValue's
                // result is even checked below. A failed read still
                // occupied this axis' declared slot in the report; skipping
                // the advance on failure (the old bug) silently shifted
                // every subsequent axis in this same report by one index.
                int index = -1;
                if (!isHat) {
                    if (axisPosition < ctx.axisIndices.size()) {
                        index = ctx.axisIndices[axisPosition];
                    }
                    ++axisPosition;
                }

                // Fase (bugfix - all hats moving together): a hardcoded 0
                // here means "give me this UsagePage/Usage's value from
                // LINK COLLECTION 0", NOT "from whichever collection vc
                // itself belongs to". A hat switch's usage (Generic Desktop,
                // Hat Switch) is identical for every hat on a multi-hat
                // device - the ONLY thing that tells hat 0's own value-caps
                // entry apart from hat 1/2/3's is which HID link collection
                // each one lives in (vc.LinkCollection), since each physical
                // hat is its own collection in the report descriptor. With
                // this hardcoded to 0, every iteration of this loop for
                // every hat's own vc asked "what's hat 0's value?" and got
                // hat 0's own live reading back, four times over - so
                // decodeHatDirections() below stamped hat 0's real state
                // into hat 1/2/3's synthetic button slots too, on every
                // report, regardless of which hat actually moved. Passing
                // vc.LinkCollection instead correctly scopes each read to
                // the specific hat vc describes.
                ULONG value = 0;
                const NTSTATUS status = HidP_GetUsageValue(HidP_Input, vc.UsagePage, vc.LinkCollection, usage, &value,
                                                             preparsedData, mutableReport, reportSize);
                if (status != HIDP_STATUS_SUCCESS) {
                    continue;
                }

                if (isHat) {
                    bool up = false;
                    bool right = false;
                    bool down = false;
                    bool left = false;
                    decodeHatDirections(value, vc.LogicalMax, up, right, down, left);

                    const int base = physicalButtonCount + hatIndex * 4;
                    if (base + 3 < currentButtons.size()) {
                        currentButtons[base + 0] = up;
                        currentButtons[base + 1] = right;
                        currentButtons[base + 2] = down;
                        currentButtons[base + 3] = left;
                    }
                    ++hatIndex;
                    continue;
                }

                if (index < 0 || index >= ctx.lastAxisValues.size()) {
                    continue;
                }
                const int intValue = static_cast<int>(value);
                if (ctx.lastAxisValues[index] != intValue) {
                    ctx.lastAxisValues[index] = intValue;
                    // Buffered, not emitted immediately - see the class
                    // docs above (m_pendingAxisValues/m_axisFlushTimer).
                    m_pendingAxisValues[ctx.systemPath][index] = intValue;
                }
            }
        }

        // Diffed once currentButtons holds both real buttons and every hat's
        // 4 synthetic directions, so a hat flipping a direction is reported
        // exactly like any other button's press/release transition.
        QVector<ButtonEvent> changedButtons;
        for (int i = 0; i < currentButtons.size() && i < ctx.lastButtonStates.size(); ++i) {
            if (currentButtons[i] != ctx.lastButtonStates[i]) {
                ctx.lastButtonStates[i] = currentButtons[i];
                changedButtons.append(ButtonEvent{ctx.systemPath, i, currentButtons[i]});
                emit buttonPressed(ctx.systemPath, i, currentButtons[i]);
            }
        }

        if (!changedButtons.isEmpty()) {
            emit buttonsChanged(changedButtons);
        }
    }

    /// Emits axisMoved() once per (systemPath, axisIndex) currently buffered
    /// in m_pendingAxisValues, then clears it - m_axisFlushTimer's own
    /// slot, capping axisMoved()'s emission rate regardless of how fast
    /// parseHidReport() is called. Copies-then-clears rather than erasing
    /// while iterating; both this and parseHidReport() run on this same
    /// worker thread's event loop, so there is no concurrent-access race to
    /// guard against here, only container-mutation-during-iteration.
    void flushPendingAxisEvents()
    {
        if (m_pendingAxisValues.isEmpty()) {
            return;
        }
        const auto pending = m_pendingAxisValues;
        m_pendingAxisValues.clear();

        for (auto pathIt = pending.constBegin(); pathIt != pending.constEnd(); ++pathIt) {
            for (auto axisIt = pathIt.value().constBegin(); axisIt != pathIt.value().constEnd(); ++axisIt) {
                emit axisMoved(pathIt.key(), axisIt.key(), axisIt.value());
            }
        }
    }

    // -- Native message handling ---------------------------------------------

    LRESULT handleWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_DEVICECHANGE:
            if (lParam != 0 && (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE)) {
                const auto *hdr = reinterpret_cast<DEV_BROADCAST_HDR *>(lParam);
                if (hdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                    if (wParam == DBT_DEVICEARRIVAL) {
                        performInitialScan();
                        // RawInput's device list can lag a few milliseconds
                        // behind the PnP arrival notification; a short
                        // deferred re-scan catches devices missed above.
                        QTimer::singleShot(50, this, &DeviceMonitorWorker::performInitialScan);
                    } else {
                        const auto *iface = reinterpret_cast<const DEV_BROADCAST_DEVICEINTERFACE_W *>(hdr);
                        handleDeviceInterfaceRemoved(QString::fromWCharArray(iface->dbcc_name));
                    }
                }
            }
            return TRUE;

        case WM_INPUT: {
            UINT dwSize = 0;
            GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &dwSize, sizeof(RAWINPUTHEADER));
            if (dwSize > 0) {
                std::vector<BYTE> buffer(dwSize);
                if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, buffer.data(), &dwSize,
                                     sizeof(RAWINPUTHEADER)) == dwSize) {
                    const auto *raw = reinterpret_cast<const RAWINPUT *>(buffer.data());
                    if (raw->header.dwType == RIM_TYPEHID) {
                        processHidInput(raw);
                    }
                }
            }
            break; // Must still reach DefWindowProc: required for WM_INPUT cleanup.
        }

        default:
            break;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    static LRESULT CALLBACK staticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        DeviceMonitorWorker *self = nullptr;
        if (msg == WM_NCCREATE) {
            auto *createStruct = reinterpret_cast<CREATESTRUCTW *>(lParam);
            self = static_cast<DeviceMonitorWorker *>(createStruct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<DeviceMonitorWorker *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self) {
            return self->handleWindowMessage(hwnd, msg, wParam, lParam);
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    /// Flush cadence for m_pendingAxisValues/m_axisFlushTimer - see the
    /// class docs above. Comfortably under EventRouter::tick()'s own 200 Hz
    /// (5 ms) output rate, so this never becomes the tighter bottleneck.
    static constexpr int kAxisFlushIntervalMs = 4;

    HWND m_hwnd = nullptr;
    HDEVNOTIFY m_deviceNotifyHandle = nullptr;
    QTimer *m_axisFlushTimer = nullptr;

    /// systemPath -> axisIndex -> latest raw value not yet flushed as
    /// axisMoved() - see flushPendingAxisEvents().
    QHash<QString, QHash<int, int>> m_pendingAxisValues;

    /// Keyed by the RawInput device HANDLE (stable while a device stays
    /// connected). Only ever touched from this worker's own thread.
    std::unordered_map<HANDLE, HidDeviceContext> m_deviceContexts;
};

DeviceManager &DeviceManager::instance()
{
    static DeviceManager s_instance;
    return s_instance;
}

DeviceManager::DeviceManager(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<DeviceInfo>("DeviceInfo");
    qRegisterMetaType<AxisEvent>("AxisEvent");
    qRegisterMetaType<ButtonEvent>("ButtonEvent");
    qRegisterMetaType<QVector<ButtonEvent>>("QVector<ButtonEvent>");
}

DeviceManager::~DeviceManager()
{
    // Purely a safety net now - see shutdown()'s own docs. Since
    // DeviceManager::instance() is a Meyer's singleton, this destructor runs
    // via atexit(), strictly *after* main()'s own stack (QGuiApplication,
    // the QML engine, every ViewModel) has already unwound; running native
    // teardown that late is undefined-behavior territory for any Qt
    // cross-thread machinery it would touch. main.cpp calls shutdown()
    // explicitly right after app.exec() returns, while qApp is still alive,
    // so by the time this destructor runs, shutdown() has almost always
    // already completed and this call is just a guarded no-op. It's only not
    // a no-op if something terminates the process without going through
    // main()'s normal app.exec()-then-return path in the first place.
    shutdown();
}

void DeviceManager::shutdown()
{
    if (m_shutdownComplete) {
        return;
    }
    m_shutdownComplete = true;

    logShutdownTrace(QStringLiteral("DeviceManager::shutdown: enter (monitorThread=%1, monitorWorker=%2, qApp alive=%3)")
                          .arg(reinterpret_cast<quintptr>(m_monitorThread))
                          .arg(reinterpret_cast<quintptr>(m_monitorWorker))
                          .arg(QCoreApplication::instance() != nullptr));

    if (m_monitorThread && m_monitorWorker) {
        // Synchronously run the RIDEV_REMOVE/UnregisterDeviceNotification/
        // DestroyWindow teardown *on the worker thread itself*, while its
        // event loop is still alive (i.e. before quit() below stops it) -
        // BlockingQueuedConnection blocks this (main) thread until that
        // slot finishes executing on the worker thread, so we're guaranteed
        // every native handle is released before we proceed to stop/delete
        // anything. Safe to block here: this is a plain cross-thread call
        // with no risk of the worker thread turning around and waiting on
        // the main thread itself.
        QMetaObject::invokeMethod(m_monitorWorker, &DeviceMonitorWorker::teardownNativeResources,
                                   Qt::BlockingQueuedConnection);
        logShutdownTrace(QStringLiteral("DeviceManager::shutdown: teardownNativeResources() completed synchronously"));

        QElapsedTimer waitTimer;
        waitTimer.start();

        m_monitorThread->quit();
        const bool joined = m_monitorThread->wait(10000);
        logShutdownTrace(QStringLiteral("DeviceManager::shutdown: monitor thread quit()+wait() returned %1 after %2 ms")
                              .arg(joined ? QStringLiteral("true") : QStringLiteral("FALSE (timed out!)"))
                              .arg(waitTimer.elapsed()));

        if (!joined) {
            // Something is genuinely stuck - deleting the worker object now
            // would race whatever the thread is still doing, so leave it be
            // (leaked) rather than risk a use-after-free/crash on exit.
            logShutdownTrace(QStringLiteral(
                "DeviceManager::shutdown: monitor thread did not stop within 10s (isRunning=%1) - "
                "leaving worker undeleted rather than risk a race")
                                  .arg(m_monitorThread->isRunning()));
        } else {
            // The OS thread has fully exited (wait() returned true), so
            // there is no longer any concurrent access to m_monitorWorker -
            // safe to delete it directly, synchronously, right here, instead
            // of the old deleteLater()-posted-to-a-dead-loop connection that
            // could never actually be delivered (see initialize()'s own docs,
            // now removed along with that connection).
            delete m_monitorWorker;
            m_monitorWorker = nullptr;
            logShutdownTrace(QStringLiteral("DeviceManager::shutdown: monitor worker deleted synchronously"));
        }
    }

    logShutdownTrace(QStringLiteral("DeviceManager::shutdown: exit"));
}

void DeviceManager::initialize()
{
    if (m_initialized) {
        return;
    }
    m_initialized = true;

    m_monitorThread = new QThread(this);
    m_monitorWorker = new DeviceMonitorWorker();
    m_monitorWorker->moveToThread(m_monitorThread);

    connect(m_monitorThread, &QThread::started, m_monitorWorker, &DeviceMonitorWorker::startMonitoring);
    connect(m_monitorWorker, &DeviceMonitorWorker::deviceDiscovered, this, &DeviceManager::addOrUpdateDevice);
    connect(m_monitorWorker, &DeviceMonitorWorker::deviceLost, this, &DeviceManager::removeDevice);
    connect(m_monitorWorker, &DeviceMonitorWorker::axisMoved, this, &DeviceManager::onAxisMoved);
    connect(m_monitorWorker, &DeviceMonitorWorker::buttonPressed, this, &DeviceManager::onButtonPressed);
    connect(m_monitorWorker, &DeviceMonitorWorker::buttonsChanged, this, &DeviceManager::onButtonsChanged);
    // Deliberately no QThread::finished -> deleteLater() connection here
    // anymore (confirmed Heisenbug root cause): m_monitorThread (the QObject)
    // lives on the thread that created it - this one, the main thread -
    // while m_monitorWorker lives on the worker thread it was
    // moveToThread()'d onto, so that used to be a queued connection whose
    // deleteLater() event was posted to m_monitorWorker's own (worker)
    // thread queue. But finished() is emitted right after run()'s call to
    // exec() *returns* - i.e. after that worker thread's event loop has
    // already stopped processing events - so that posted event could never
    // actually be delivered: ~DeviceMonitorWorker() (and its
    // RIDEV_REMOVE/UnregisterDeviceNotification/DestroyWindow cleanup) never
    // ran on any normal shutdown. shutdown() below now deletes
    // m_monitorWorker explicitly and synchronously instead, after
    // confirming (via QThread::wait()) that the thread has actually stopped.

    m_monitorThread->start();
}

QList<DeviceInfo> DeviceManager::getConnectedDevices() const
{
    QReadLocker locker(&m_devicesLock);
    return m_devices;
}

void DeviceManager::addOrUpdateDevice(const DeviceInfo &device)
{
    {
        QWriteLocker locker(&m_devicesLock);
        const int index = m_devices.indexOf(device);
        if (index >= 0) {
            m_devices[index] = device;
        } else {
            m_devices.append(device);
        }
    }
    emit deviceAdded(device);
}

void DeviceManager::removeDevice(const QString &systemPath)
{
    {
        QWriteLocker locker(&m_devicesLock);
        for (auto it = m_devices.begin(); it != m_devices.end();) {
            if (it->systemPath == systemPath) {
                it = m_devices.erase(it);
            } else {
                ++it;
            }
        }
    }
    emit deviceRemoved(systemPath);
}

void DeviceManager::injectButtonPress(const QString &systemPath, int buttonIndex, bool pressed)
{
    emit buttonPressed(systemPath, buttonIndex, pressed);
}

void DeviceManager::onAxisMoved(const QString &systemPath, int axisIndex, int value)
{
    emit axisMoved(systemPath, axisIndex, value);
}

void DeviceManager::onButtonPressed(const QString &systemPath, int buttonIndex, bool pressed)
{
    emit buttonPressed(systemPath, buttonIndex, pressed);
}

void DeviceManager::onButtonsChanged(const QVector<ButtonEvent> &events)
{
    emit buttonsChanged(events);
}

#include "DeviceManager.moc"
