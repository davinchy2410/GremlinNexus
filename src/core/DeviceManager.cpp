#include "DeviceManager.h"

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
        if (m_deviceNotifyHandle) {
            UnregisterDeviceNotification(m_deviceNotifyHandle);
            m_deviceNotifyHandle = nullptr;
        }
        if (m_hwnd) {
            // Stop receiving raw input before the window disappears.
            RAWINPUTDEVICE devices[3] = {
                {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_JOYSTICK, RIDEV_REMOVE, nullptr},
                {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_GAMEPAD, RIDEV_REMOVE, nullptr},
                {HID_USAGE_PAGE_GENERIC, kHidUsageMultiAxisController, RIDEV_REMOVE, nullptr},
            };
            RegisterRawInputDevices(devices, 3, sizeof(RAWINPUTDEVICE));

            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
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

        // Created here (not in the constructor) so it's already parented on
        // this worker's own thread - see the class docs above for why this
        // exists.
        m_axisFlushTimer = new QTimer(this);
        connect(m_axisFlushTimer, &QTimer::timeout, this, &DeviceMonitorWorker::flushPendingAxisEvents);
        m_axisFlushTimer->start(kAxisFlushIntervalMs);
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
            return false;
        }

        // HWND_MESSAGE => message-only window: never visible, never receives
        // paint/input-focus traffic, only used as a native message sink.
        m_hwnd = CreateWindowExW(0, kClassName, L"GremblingExDeviceMonitor", 0,
                                  0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, this);
        return m_hwnd != nullptr;
    }

    void registerForDeviceNotifications()
    {
        DEV_BROADCAST_DEVICEINTERFACE_W filter{};
        filter.dbcc_size = sizeof(filter);
        filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        filter.dbcc_classguid = kGuidDevInterfaceHid;

        m_deviceNotifyHandle = RegisterDeviceNotificationW(m_hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
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
        RegisterRawInputDevices(devices, 3, sizeof(RAWINPUTDEVICE));
    }

    // -- Enumeration ---------------------------------------------------------

    /// Re-enumerates every currently attached RawInput HID device and emits
    /// deviceDiscovered() for any joystick/gamepad not already tracked.
    /// Safe to call repeatedly: registerHidDevice() is a no-op for devices
    /// already present in m_deviceContexts.
    void performInitialScan()
    {
        UINT numDevices = 0;
        if (GetRawInputDeviceList(nullptr, &numDevices, sizeof(RAWINPUTDEVICELIST)) != 0 || numDevices == 0) {
            return;
        }

        std::vector<RAWINPUTDEVICELIST> deviceList(numDevices);
        const UINT listed = GetRawInputDeviceList(deviceList.data(), &numDevices, sizeof(RAWINPUTDEVICELIST));
        if (listed == static_cast<UINT>(-1)) {
            return;
        }
        deviceList.resize(listed);

        for (const RAWINPUTDEVICELIST &entry : deviceList) {
            if (entry.dwType == RIM_TYPEHID) {
                registerHidDevice(entry.hDevice);
            }
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
            return;
        }
        std::vector<wchar_t> nameBuffer(nameSize);
        if (GetRawInputDeviceInfoW(hDevice, RIDI_DEVICENAME, nameBuffer.data(), &nameSize) == static_cast<UINT>(-1)) {
            return;
        }
        const QString devicePath = QString::fromWCharArray(nameBuffer.data());

        // Preparsed HID data, needed for capability + report parsing.
        UINT preparsedSize = 0;
        GetRawInputDeviceInfoW(hDevice, RIDI_PREPARSEDDATA, nullptr, &preparsedSize);
        if (preparsedSize == 0) {
            return;
        }
        std::vector<uint8_t> preparsedBuffer(preparsedSize);
        if (GetRawInputDeviceInfoW(hDevice, RIDI_PREPARSEDDATA, preparsedBuffer.data(), &preparsedSize) ==
            static_cast<UINT>(-1)) {
            return;
        }
        auto *preparsedData = reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsedBuffer.data());

        HIDP_CAPS caps{};
        if (HidP_GetCaps(preparsedData, &caps) != HIDP_STATUS_SUCCESS) {
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
    QString queryProductName(const QString &devicePath) const
    {
        HANDLE fileHandle = CreateFileW(reinterpret_cast<LPCWSTR>(devicePath.utf16()), 0,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (fileHandle == INVALID_HANDLE_VALUE) {
            return QStringLiteral("Unknown HID Device");
        }

        wchar_t buffer[256] = {};
        QString productName;
        if (HidD_GetProductString(fileHandle, buffer, sizeof(buffer))) {
            productName = QString::fromWCharArray(buffer).trimmed();
        }
        CloseHandle(fileHandle);

        return productName.isEmpty() ? QStringLiteral("Unknown HID Device") : productName;
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

        QVector<bool> currentButtons(ctx.buttonCount, false);
        int buttonBase = 0;
        for (const HIDP_BUTTON_CAPS &bc : ctx.buttonCaps) {
            const USAGE usageMin = bc.IsRange ? bc.Range.UsageMin : bc.NotRange.Usage;
            const USAGE usageMax = bc.IsRange ? bc.Range.UsageMax : bc.NotRange.Usage;
            const int rangeCount = usageMax - usageMin + 1;

            ULONG usageListLength = static_cast<ULONG>(rangeCount);
            std::vector<USAGE> usages(usageListLength);
            const NTSTATUS status = HidP_GetUsages(HidP_Input, bc.UsagePage, 0, usages.data(), &usageListLength,
                                                     preparsedData, mutableReport, reportSize);
            if (status == HIDP_STATUS_SUCCESS) {
                for (ULONG i = 0; i < usageListLength; ++i) {
                    const int localIndex = static_cast<int>(usages[i]) - usageMin;
                    const int globalIndex = buttonBase + localIndex;
                    if (localIndex >= 0 && localIndex < rangeCount && globalIndex < physicalButtonCount) {
                        currentButtons[globalIndex] = true;
                    }
                }
            }
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

                ULONG value = 0;
                const NTSTATUS status = HidP_GetUsageValue(HidP_Input, vc.UsagePage, 0, usage, &value, preparsedData,
                                                             mutableReport, reportSize);
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
    if (m_monitorThread) {
        m_monitorThread->quit();
        m_monitorThread->wait();
    }
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
    connect(m_monitorThread, &QThread::finished, m_monitorWorker, &QObject::deleteLater);

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
