#include "DeviceTesterViewModel.h"

#include <algorithm>

#include "DeviceManager.h"

DeviceTesterViewModel::DeviceTesterViewModel(QObject *parent)
    : QObject(parent)
{
    m_axisValues.reserve(kNumAxes);
    for (int i = 0; i < kNumAxes; ++i) {
        m_axisValues.append(0);
    }

    m_buttonStates.reserve(kNumButtons);
    for (int i = 0; i < kNumButtons; ++i) {
        m_buttonStates.append(false);
    }

    m_axisLogicalMin.reserve(kNumAxes);
    m_axisLogicalMax.reserve(kNumAxes);
    for (int i = 0; i < kNumAxes; ++i) {
        m_axisLogicalMin.append(0);
        m_axisLogicalMax.append(65535);
    }

    connect(&DeviceManager::instance(), &DeviceManager::deviceAdded, this, &DeviceTesterViewModel::onDeviceInfoUpdated);
    connect(&DeviceManager::instance(), &DeviceManager::axisMoved, this, &DeviceTesterViewModel::onAxisMoved);
    connect(&DeviceManager::instance(), &DeviceManager::buttonPressed, this, &DeviceTesterViewModel::onButtonPressed);

    m_uiThrottleTimer = new QTimer(this);
    m_uiThrottleTimer->setInterval(4); // 250 Hz - see the member's own docs.
    connect(m_uiThrottleTimer, &QTimer::timeout, this, &DeviceTesterViewModel::flushPendingUiUpdates);
    m_uiThrottleTimer->start();
}

QString DeviceTesterViewModel::currentSystemPath() const
{
    return m_currentSystemPath;
}

void DeviceTesterViewModel::setCurrentSystemPath(const QString &systemPath)
{
    if (m_currentSystemPath == systemPath) {
        return;
    }
    m_currentSystemPath = systemPath;

    // Switching devices: stale readings from whatever was selected before
    // would otherwise sit on screen looking like live data for the new one.
    std::fill(m_axisValues.begin(), m_axisValues.end(), QVariant(0));
    std::fill(m_buttonStates.begin(), m_buttonStates.end(), QVariant(false));

    refreshDeviceMetadataFromManager();

    emit currentSystemPathChanged();
    emit axisValuesChanged();
    emit buttonStatesChanged();
}

void DeviceTesterViewModel::onDeviceInfoUpdated(const DeviceInfo &device)
{
    if (device.systemPath != m_currentSystemPath) {
        return;
    }
    // Same device, but DeviceManager's own catalogue entry for it just
    // changed (a later rescan resolved its real name/button count - see
    // this slot's own docs in the header) - re-run the exact same capture
    // setCurrentSystemPath() does, without touching m_currentSystemPath
    // itself or clearing the live axis/button readings.
    refreshDeviceMetadataFromManager();
    emit currentSystemPathChanged();
}

void DeviceTesterViewModel::refreshDeviceMetadataFromManager()
{
    // Re-derive the per-axis HID logical range for the current device
    // (Fase 16.6) - falls back to [0, 65535] per axis (the starting value
    // std::fill'd below) for a device DeviceManager has no captured HID
    // caps for, e.g. a mock/synthetic entry, or one not currently found at
    // all (falls through the loop below untouched).
    std::fill(m_axisLogicalMin.begin(), m_axisLogicalMin.end(), QVariant(0));
    std::fill(m_axisLogicalMax.begin(), m_axisLogicalMax.end(), QVariant(65535));
    m_currentDeviceNumButtons = 0;
    m_currentDeviceNumHats = 0;
    const QList<DeviceInfo> connected = DeviceManager::instance().getConnectedDevices();
    for (const DeviceInfo &device : connected) {
        if (device.systemPath != m_currentSystemPath) {
            continue;
        }
        const int count = std::min<int>(device.axisLogicalMin.size(), kNumAxes);
        for (int i = 0; i < count; ++i) {
            m_axisLogicalMin[i] = device.axisLogicalMin.at(i);
            m_axisLogicalMax[i] = device.axisLogicalMax.at(i);
        }
        m_currentDeviceNumButtons = device.numButtons;
        m_currentDeviceNumHats = device.numHats;
        break;
    }
}

QVariantList DeviceTesterViewModel::axisValues() const
{
    return m_axisValues;
}

QVariantList DeviceTesterViewModel::buttonStates() const
{
    return m_buttonStates;
}

QVariantList DeviceTesterViewModel::axisLogicalMin() const
{
    return m_axisLogicalMin;
}

QVariantList DeviceTesterViewModel::axisLogicalMax() const
{
    return m_axisLogicalMax;
}

int DeviceTesterViewModel::currentDeviceNumButtons() const
{
    return m_currentDeviceNumButtons;
}

int DeviceTesterViewModel::currentDeviceNumHats() const
{
    return m_currentDeviceNumHats;
}

QString DeviceTesterViewModel::currentDeviceLabel() const
{
    if (m_currentSystemPath.isEmpty()) {
        return tr("No Device Selected");
    }

    const QList<DeviceInfo> connected = DeviceManager::instance().getConnectedDevices();
    int vjoyOrdinal = 0;
    for (const DeviceInfo &device : connected) {
        const bool isVjoy = device.deviceName.contains(QStringLiteral("vjoy"), Qt::CaseInsensitive);
        if (isVjoy) {
            ++vjoyOrdinal;
        }
        if (device.systemPath == m_currentSystemPath) {
            return isVjoy ? tr("vJoy %1 Output").arg(vjoyOrdinal) : device.deviceName;
        }
    }
    return tr("Unknown Device");
}

void DeviceTesterViewModel::onAxisMoved(const QString &systemPath, int axisIndex, int value)
{
    if (systemPath != m_currentSystemPath) {
        return;
    }

    if (axisIndex < 0 || axisIndex >= kNumAxes) {
        return;
    }
    // Was clamped to [0, 65535] (unsigned-only) - every HID joystick this
    // app dealt with until now reports a zero-based logical range, but
    // XInput's thumbsticks are signed (-32768..32767, centered on 0), so a
    // push left/up produced a negative raw value that this clamp silently
    // floored to 0, making the Tester radar look permanently stuck at
    // center for those directions. Widened to also cover that signed range
    // - real HID devices never report negative values in the first place,
    // so this is a strict widening, not a behavior change for them.
    m_axisValues[axisIndex] = std::clamp(value, -65535, 65535);
    m_axesDirty = true;
}

void DeviceTesterViewModel::onButtonPressed(const QString &systemPath, int buttonIndex, bool pressed)
{
    if (systemPath != m_currentSystemPath || buttonIndex < 0 || buttonIndex >= kNumButtons) {
        return;
    }
    m_buttonStates[buttonIndex] = pressed;
    m_buttonsDirty = true;
}

void DeviceTesterViewModel::flushPendingUiUpdates()
{
    // Independent checks are the whole point: moving the stick sets only
    // m_axesDirty, so a stick-only frame never touches buttonStatesChanged()
    // - and therefore never makes QML's 256-delegate button GridView
    // re-scan buttonStates - and vice versa for a button-only frame.
    if (m_axesDirty) {
        m_axesDirty = false;
        emit axisValuesChanged();
    }
    if (m_buttonsDirty) {
        m_buttonsDirty = false;
        emit buttonStatesChanged();
    }
}
