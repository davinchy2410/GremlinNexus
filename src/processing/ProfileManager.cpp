#include "ProfileManager.h"

#include <memory>
#include <utility>

#include <QDebug>
#include <QFile>
#include <QFont>
#include <QHash>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLatin1String>
#include <QList>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QStringList>

#include "AudioAction.h"
#include "AxisSplitterHandler.h"
#include "AxisToButtonHandler.h"
#include "ButtonRemapHandler.h"
#include "ButtonToAxisHandler.h"
#include "ConditionHandler.h"
#include "CurveHandler.h"
#include "DelayAction.h"
#include "DeviceManager.h"
#include "EventRouter.h"
#include "HatRemapHandler.h"
#include "KeyboardHandler.h"
#include "MacroHandler.h"
#include "MergeAxisHandler.h"
#include "ModeSwitchHandler.h"
#include "MouseButtonHandler.h"
#include "MouseRelativeAxisHandler.h"
#include "SendInputKeyboardBackend.h"
#include "SequenceHandler.h"
#include "SplitAxisHandler.h"
#include "SmoothingHandler.h"
#include "TempoHandler.h"
#include "TemporaryModeSwitchHandler.h"
#include "ToggleHandler.h"
#include "TrimHandler.h"
#include "TTSAction.h"
#include "VirtualOutputManager.h"

namespace {

/// Security hardening: caps how deep instantiateHandler()'s wrapper
/// recursion (ConditionHandler/ToggleHandler/AxisToButtonHandler/
/// SmoothingHandler's "wrappedAction", TempoHandler's shortActions/
/// longActions/doubleActions, SequenceHandler's actions) and
/// renameModeInBinding()'s own mirrored recursion are allowed to go - a
/// corrupted or maliciously crafted profile with thousands of nested
/// wrappers would otherwise exhaust the call stack (a crash, not a
/// recoverable error) well before any legitimate profile could ever need
/// anywhere near this many levels.
constexpr int kMaxHandlerNestingDepth = 32;

/// Sprint QoL Part 2: a binding's optional free-text "parameters.note" - see
/// IActionHandler::note()'s own docs for why this is read here (rather than
/// each concrete instantiate*Handler() touching it) and merged back in by
/// serializeProfile() below. Same "small local duplicate rather than a
/// shared call" pattern as extractVidPid() below - ProfileEditorViewModel.cpp
/// has its own private copy of this exact one-liner for its own
/// findBindingLabel().
QString noteFromActionJson(const QJsonObject &binding)
{
    return binding.value(QLatin1String("parameters")).toObject().value(QLatin1String("note")).toString();
}

/// Parses the VID/PID out of a RawInput device path such as
/// "\\?\HID#VID_045E&PID_028E&IG_00#7&1234abcd&0&0000#{...}" - same pattern
/// DeviceManager::parseVidPid() uses (that one is private to
/// DeviceManager.cpp's own anonymous namespace, hence this small local
/// duplicate rather than a shared call). Returns false (vendorId/productId
/// untouched) if devicePath doesn't contain a VID_/PID_ pair at all.
bool extractVidPid(const QString &devicePath, QString &vendorId, QString &productId)
{
    static const QRegularExpression pattern(QStringLiteral("VID_([0-9A-Fa-f]{4})&PID_([0-9A-Fa-f]{4})"));
    const QRegularExpressionMatch match = pattern.match(devicePath);
    if (!match.hasMatch()) {
        return false;
    }
    vendorId = match.captured(1).toUpper();
    productId = match.captured(2).toUpper();
    return true;
}

/// Overwrites inputMin/inputMax with sourceDevice's own true HID logical
/// range for sourceAxis (Fase 20), if that device is currently connected and
/// reports one - a no-op otherwise (inputMin/inputMax keep whatever the
/// caller already set, e.g. a binding's own "parameters.inputMin/inputMax"
/// or the hardcoded 0/65535 default). Some joysticks report a narrower raw
/// range than 0-65535 (e.g. a 12-bit axis maxing out at 4095); without this,
/// such an axis's CurveHandler/AxisSplitterHandler never sees the top of
/// its own physical travel, so vJoy's output visibly stalls short of full
/// deflection. Called before any explicit per-axis calibration (see
/// getAxisCalibration()) is applied, so a user's Calibration Wizard result
/// still wins over this hardware default.
void applyAxisLogicalRange(const QString &sourceDevice, int sourceAxis, int &inputMin, int &inputMax)
{
    const QList<DeviceInfo> devices = DeviceManager::instance().getConnectedDevices();
    for (const DeviceInfo &dev : devices) {
        if (dev.systemPath == sourceDevice && sourceAxis >= 0 && sourceAxis < dev.axisLogicalMin.size()) {
            inputMin = dev.axisLogicalMin[sourceAxis];
            inputMax = dev.axisLogicalMax[sourceAxis];
            break;
        }
    }
}

/// Recursively replaces oldName with newName wherever it appears as a
/// binding's own "mode" field or a ModeSwitch/TemporaryModeSwitch
/// binding's "parameters.targetMode", walking into every wrapper/cascade
/// shape this schema currently has (wrappedAction; TempoHandler's
/// shortActions/longActions/doubleActions and their pre-cascade legacy
/// shortAction/longAction/doubleAction singular form; SequenceHandler's
/// parameters.actions) - see ProfileManager::renameMode()'s own docs for
/// why a shallow top-level-only replace isn't enough. "mode" is only ever
/// meaningful on a routed (top-level) binding - see this class' own JSON
/// schema docs - so checking it on a nested object here is a harmless
/// no-op (nested objects simply never have that key).
void renameModeInBinding(QJsonObject &binding, const QString &oldName, const QString &newName, int depth = 0)
{
    // Security hardening: a corrupted/malicious profile could nest
    // wrappedAction/action-list entries arbitrarily deep (or even contain a
    // cycle reconstructed as ever-deeper copies once re-serialized) -
    // bailing out past kMaxHandlerNestingDepth keeps this from exhausting
    // the call stack, at the cost of simply not renaming anything past that
    // depth (the same "graceful degradation over a crash" policy
    // instantiateHandler() uses).
    if (depth > kMaxHandlerNestingDepth) {
        qWarning() << "ProfileManager: renameModeInBinding nesting exceeds" << kMaxHandlerNestingDepth
                    << "levels - refusing to recurse further (corrupt/malicious profile?)";
        return;
    }

    if (binding.value(QLatin1String("mode")).toString() == oldName) {
        binding[QLatin1String("mode")] = newName;
    }

    if (binding.contains(QLatin1String("parameters"))) {
        QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();
        if (parameters.value(QLatin1String("targetMode")).toString() == oldName) {
            parameters[QLatin1String("targetMode")] = newName;
        }
        if (parameters.contains(QLatin1String("actions"))) {
            QJsonArray actions = parameters.value(QLatin1String("actions")).toArray();
            for (int i = 0; i < actions.size(); ++i) {
                QJsonObject action = actions[i].toObject();
                renameModeInBinding(action, oldName, newName, depth + 1);
                actions[i] = action;
            }
            parameters[QLatin1String("actions")] = actions;
        }
        binding[QLatin1String("parameters")] = parameters;
    }

    if (binding.contains(QLatin1String("wrappedAction"))) {
        QJsonObject wrapped = binding.value(QLatin1String("wrappedAction")).toObject();
        renameModeInBinding(wrapped, oldName, newName, depth + 1);
        binding[QLatin1String("wrappedAction")] = wrapped;
    }

    static const QStringList kActionListKeys{
        QStringLiteral("shortActions"), QStringLiteral("longActions"), QStringLiteral("doubleActions")};
    for (const QString &key : kActionListKeys) {
        if (!binding.contains(key)) {
            continue;
        }
        QJsonArray list = binding.value(key).toArray();
        for (int i = 0; i < list.size(); ++i) {
            QJsonObject action = list[i].toObject();
            renameModeInBinding(action, oldName, newName, depth + 1);
            list[i] = action;
        }
        binding[key] = list;
    }

    static const QStringList kLegacySingularKeys{
        QStringLiteral("shortAction"), QStringLiteral("longAction"), QStringLiteral("doubleAction")};
    for (const QString &key : kLegacySingularKeys) {
        if (!binding.contains(key)) {
            continue;
        }
        QJsonObject action = binding.value(key).toObject();
        renameModeInBinding(action, oldName, newName, depth + 1);
        binding[key] = action;
    }
}

} // namespace

bool ProfileManager::loadProfile(const QString &filePath, EventRouter &router)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "ProfileManager: could not open profile" << filePath << ":" << file.errorString();
        return false;
    }

    const QByteArray raw = file.readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "ProfileManager: JSON parse error in" << filePath << "at offset" << parseError.offset << ":"
                    << parseError.errorString();
        return false;
    }
    if (!doc.isObject()) {
        qWarning() << "ProfileManager: profile" << filePath << "is not a JSON object at its root";
        return false;
    }

    return loadProfileFromJson(doc.object(), router, filePath);
}

bool ProfileManager::loadProfileFromJson(const QJsonObject &root, EventRouter &router, const QString &sourceLabel)
{
    const QJsonValue bindingsValue = root.value(QLatin1String("bindings"));
    if (!bindingsValue.isArray()) {
        qWarning() << "ProfileManager: profile" << sourceLabel << "has no \"bindings\" array";
        return false;
    }

    router.clearRoutes();

    // Fase 20: Windows can shift a device's systemPath's trailing Instance
    // ID across a reboot/replug even though it's the exact same physical
    // hardware (same VID/PID) - a profile bound to the old string would
    // otherwise silently bind nothing at all (not in the tester, not in the
    // Profiles screen). Cached per orphaned sourceDevice string so a profile
    // with many bindings on the same device only pays the VID/PID scan once.
    // Fase (bugfix): now also consulted for "calibrations" entries below,
    // not just bindings - a saved calibration used to stay keyed under the
    // device's OLD (pre-replug) systemPath forever, silently missing every
    // getAxisCalibration() lookup made against the migrated path the very
    // next line already resolves bindings to, and quietly falling back to
    // the raw uncalibrated [0, 65535] range instead.
    const QList<DeviceInfo> connectedDevices = DeviceManager::instance().getConnectedDevices();
    QHash<QString, QString> migratedPaths;

    auto resolveMigratedPath = [&](const QString &oldPath) -> QString {
        if (oldPath.isEmpty()) {
            return oldPath;
        }
        for (const DeviceInfo &device : connectedDevices) {
            if (device.systemPath == oldPath) {
                return oldPath; // Still connected under the same path - nothing to migrate.
            }
        }

        if (!migratedPaths.contains(oldPath)) {
            QString migrated;
            QString vendorId, productId;
            if (extractVidPid(oldPath, vendorId, productId)) {
                const DeviceInfo *candidate = nullptr;
                int matchCount = 0;
                for (const DeviceInfo &device : connectedDevices) {
                    if (device.vendorId == vendorId && device.productId == productId) {
                        candidate = &device;
                        ++matchCount;
                    }
                }
                if (matchCount == 1 && candidate) {
                    migrated = candidate->systemPath;
                    qInfo() << "ProfileManager: migrated orphaned device" << oldPath << "->" << migrated
                            << "(VID" << vendorId << "PID" << productId << ")";
                }
            }
            // Cached even on a miss (empty string) so a device with no
            // current match isn't re-scanned for every one of its own
            // bindings/calibrations.
            migratedPaths.insert(oldPath, migrated);
        }

        const QString &migrated = migratedPaths[oldPath];
        return migrated.isEmpty() ? oldPath : migrated;
    };

    m_calibrations.clear();
    const QJsonValue calibrationsValue = root.value(QLatin1String("calibrations"));
    if (calibrationsValue.isArray()) {
        const QJsonArray calibrations = calibrationsValue.toArray();
        for (const QJsonValue &val : calibrations) {
            if (val.isObject()) {
                const QJsonObject obj = val.toObject();
                const QString sysPath = resolveMigratedPath(obj.value(QLatin1String("systemPath")).toString());
                const int axisIndex = obj.value(QLatin1String("axisIndex")).toInt();
                const int calMin = obj.value(QLatin1String("calibratedMin")).toInt();
                const int calMax = obj.value(QLatin1String("calibratedMax")).toInt();
                m_calibrations[qMakePair(sysPath, axisIndex)] = AxisCalibration{calMin, calMax};
            }
        }
    }

    const QJsonArray bindings = bindingsValue.toArray();
    int applied = 0;
    for (const QJsonValue &bindingValue : bindings) {
        if (!bindingValue.isObject()) {
            qWarning() << "ProfileManager: skipping non-object entry in \"bindings\" in" << sourceLabel;
            continue;
        }

        QJsonObject binding = bindingValue.toObject();
        const QString sourceDevice = binding.value(QLatin1String("sourceDevice")).toString();
        if (!sourceDevice.isEmpty()) {
            const QString migrated = resolveMigratedPath(sourceDevice);
            if (migrated != sourceDevice) {
                binding[QLatin1String("sourceDevice")] = migrated;
            }
        }

        if (applyBinding(binding, router)) {
            ++applied;
        }
    }

    // Sprint 5 (Familias de Modos): restore the mode-inheritance tree this
    // profile saved (see serializeProfile()) - must run before the legacy
    // backfill below so an explicit "modeHierarchy" entry always wins over
    // the assumed-parent-is-Global default.
    const QJsonObject modeHierarchy = root.value(QLatin1String("modeHierarchy")).toObject();
    for (auto it = modeHierarchy.constBegin(); it != modeHierarchy.constEnd(); ++it) {
        router.setModeParent(it.key(), it.value().toString());
    }

    // Backward compatibility: a profile saved before this feature existed
    // (or a mode referenced only by a binding's "mode" field, never
    // explicitly added via ProfileEditorViewModel::addMode()) has no
    // "modeHierarchy" entry for itself at all - without this, such a mode
    // would silently become its own root (see EventRouter::setModeParent()'s
    // docs) and stop falling back to Global, a behavior change no one asked
    // for. Every non-Global mode discovered among the routes just applied
    // that still has no parent gets defaulted to Global, exactly matching
    // the router's old hardcoded fallback.
    for (const EventRouter::RouteDescriptor &route : router.allRoutes()) {
        if (route.mode != EventRouter::kGlobalMode && router.getModeParent(route.mode).isEmpty()) {
            router.setModeParent(route.mode, EventRouter::kGlobalMode);
        }
    }

    qInfo() << "ProfileManager: loaded" << applied << "of" << bindings.size() << "binding(s) from" << sourceLabel;
    return true;
}

bool ProfileManager::applyBinding(const QJsonObject &binding, EventRouter &router)
{
    auto handler = instantiateHandler(binding, router);
    if (!handler) {
        return false;
    }

    const QString mode = binding.value(QLatin1String("mode")).toString(EventRouter::kGlobalMode);
    const QString sourceDevice = binding.value(QLatin1String("sourceDevice")).toString();

    if (binding.contains(QLatin1String("sourceAxis"))) {
        const int sourceAxis = binding.value(QLatin1String("sourceAxis")).toInt();
        router.addAxisRoute(sourceDevice, sourceAxis, std::move(handler), mode);
        return true;
    }
    if (binding.contains(QLatin1String("sourceButton"))) {
        const int sourceButton = binding.value(QLatin1String("sourceButton")).toInt();
        router.addButtonRoute(sourceDevice, sourceButton, std::move(handler), mode);
        return true;
    }

    qWarning() << "ProfileManager: binding has neither \"sourceAxis\" nor \"sourceButton\" - skipping";
    return false;
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateHandler(const QJsonObject &binding, EventRouter &router,
                                                                     int depth)
{
    // Security hardening: refuses to build another level of wrapper once
    // depth exceeds kMaxHandlerNestingDepth, rather than recursing into
    // instantiateHandlerImpl() again - see that constant's own docs. A
    // corrupted/malicious profile this deeply nested just loses everything
    // past this point (logged, not silently), the same "skip the bad part,
    // keep the rest of the profile usable" policy every other malformed-
    // binding check in this file already follows.
    if (depth > kMaxHandlerNestingDepth) {
        qWarning() << "ProfileManager: binding nesting exceeds" << kMaxHandlerNestingDepth
                    << "levels - refusing to recurse further (corrupt/malicious profile?)";
        return nullptr;
    }

    auto handler = instantiateHandlerImpl(binding, router, depth);
    if (handler) {
        handler->setNote(noteFromActionJson(binding));
    }
    return handler;
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateHandlerImpl(const QJsonObject &binding, EventRouter &router,
                                                                         int depth)
{
    const QString actionType = binding.value(QLatin1String("actionType")).toString();

    if (actionType == QLatin1String("CurveHandler")) {
        return instantiateCurveHandler(binding, router);
    }
    if (actionType == QLatin1String("ModeSwitch")) {
        return instantiateModeSwitchHandler(binding, router);
    }
    if (actionType == QLatin1String("ButtonRemapHandler")) {
        return instantiateButtonRemapHandler(binding, router);
    }
    if (actionType == QLatin1String("HatRemapHandler")) {
        return instantiateHatRemapHandler(binding, router);
    }
    if (actionType == QLatin1String("KeyboardHandler")) {
        return instantiateKeyboardHandler(binding);
    }
    if (actionType == QLatin1String("TemporaryModeSwitch")) {
        return instantiateTemporaryModeSwitchHandler(binding, router);
    }
    if (actionType == QLatin1String("TrimHandler")) {
        return instantiateTrimHandler(binding, router);
    }
    if (actionType == QLatin1String("MouseRelativeAxis")) {
        return instantiateMouseRelativeAxisHandler(binding, router);
    }
    if (actionType == QLatin1String("MouseButton")) {
        return instantiateMouseButtonHandler(binding);
    }
    if (actionType == QLatin1String("ConditionHandler")) {
        return instantiateConditionHandler(binding, router, depth);
    }
    if (actionType == QLatin1String("ToggleHandler")) {
        return instantiateToggleHandler(binding, router, depth);
    }
    if (actionType == QLatin1String("TempoHandler")) {
        return instantiateTempoHandler(binding, router, depth);
    }
    if (actionType == QLatin1String("AxisToButtonHandler")) {
        return instantiateAxisToButtonHandler(binding, router, depth);
    }
    if (actionType == QLatin1String("ButtonToAxisHandler")) {
        return instantiateButtonToAxisHandler(binding, router, depth);
    }
    if (actionType == QLatin1String("MergeAxisHandler")) {
        return instantiateMergeAxisHandler(binding, router);
    }
    if (actionType == QLatin1String("SplitAxisHandler")) {
        return instantiateSplitAxisHandler(binding, router);
    }
    if (actionType == QLatin1String("MacroHandler")) {
        return instantiateMacroHandler(binding, router);
    }
    if (actionType == QLatin1String("AxisSplitterHandler")) {
        return instantiateAxisSplitterHandler(binding, router);
    }
    if (actionType == QLatin1String("SequenceHandler")) {
        return instantiateSequenceHandler(binding, router, depth);
    }
    if (actionType == QLatin1String("SmoothingHandler")) {
        return instantiateSmoothingHandler(binding, router, depth);
    }
    if (actionType == QLatin1String("DelayAction")) {
        return instantiateDelayAction(binding);
    }
    if (actionType == QLatin1String("AudioAction")) {
        return instantiateAudioAction(binding);
    }
    if (actionType == QLatin1String("TTSAction")) {
        return instantiateTTSAction(binding);
    }

    qWarning() << "ProfileManager: unsupported actionType" << actionType << "- skipping binding";
    return nullptr;
}

std::shared_ptr<IVirtualOutputDevice> ProfileManager::resolveTargetDevice(const QJsonObject &binding,
                                                                            const char *handlerName,
                                                                            const char *idKey)
{
    if (!binding.contains(QLatin1String(idKey))) {
        qWarning() << "ProfileManager:" << handlerName << "binding is missing \"" << idKey << "\" - skipping";
        return nullptr;
    }
    const int targetOutputId = binding.value(QLatin1String(idKey)).toInt();
    const bool isVigem = binding.value(QLatin1String("targetDeviceType")).toString(QLatin1String("vjoy"))
                         == QLatin1String("vigem");
    const int maxId = isVigem ? 4 : 16;
    if (targetOutputId < 1 || targetOutputId > maxId) {
        qWarning() << "ProfileManager:" << handlerName << idKey << targetOutputId << "is outside"
                    << (isVigem ? "ViGEm's valid range [1, 4]" : "vJoy's valid range [1, 16]") << "- skipping binding";
        return nullptr;
    }

    auto device = isVigem ? VirtualOutputManager::instance().getViGEmDevice(static_cast<uint>(targetOutputId))
                          : VirtualOutputManager::instance().getVJoyDevice(static_cast<uint>(targetOutputId));
    if (!device->acquire()) {
        qWarning() << "ProfileManager: could not acquire" << (isVigem ? "ViGEm" : "vJoy") << "device"
                    << targetOutputId << "- this binding will stage values but nothing will reach the driver";
    }
    return device;
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateCurveHandler(const QJsonObject &binding,
                                                                          EventRouter &router)
{
    auto device = resolveTargetDevice(binding, "CurveHandler");
    if (!device) {
        return nullptr;
    }
    router.registerOutputDevice(device);

    const int targetAxis = binding.value(QLatin1String("targetAxis")).toInt();

    const QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();
    int inputMin = parameters.value(QLatin1String("inputMin")).toInt(0);
    int inputMax = parameters.value(QLatin1String("inputMax")).toInt(65535);
    // A ViGEm target's own native output range is NOT vJoy's [0, 32767] -
    // thumbstick axes (targetAxis 0-3: LX/LY/RX/RY) are signed
    // [-32768, 32767] and trigger axes (4-5: LT/RT) are [0, 255] (see
    // ViGEmDevice::setAxis()'s own int16_t/uint8_t casts, and getAxisName()
    // in ProfileEditorViewModel.cpp for the same axis-index convention).
    // Falling back to vJoy's range here for a ViGEm binding with no explicit
    // outputMin/outputMax left every ViGEm-targeted curve centered on the
    // wrong value - a physically-centered stick produced XInput ~+16384
    // instead of the true center 0, confirmed via Device Tester 2026-07-23.
    // Only affects this fallback when a binding's own JSON omits these keys;
    // an explicitly-saved custom range is always respected as-is.
    int defaultOutputMin = 0;
    int defaultOutputMax = 32767;
    if (device->isViGEmDevice()) {
        if (targetAxis >= 4) {
            defaultOutputMin = 0;
            defaultOutputMax = 255;
        } else {
            defaultOutputMin = -32768;
            defaultOutputMax = 32767;
        }
    }
    const int outputMin = parameters.value(QLatin1String("outputMin")).toInt(defaultOutputMin);
    const int outputMax = parameters.value(QLatin1String("outputMax")).toInt(defaultOutputMax);
    const double deadzone = parameters.value(QLatin1String("deadzone")).toDouble(0.05);
    const double sensitivity = parameters.value(QLatin1String("sensitivity")).toDouble(1.0);
    const double smoothingFactor = parameters.value(QLatin1String("smoothingFactor")).toDouble(0.0);
    const bool invert = parameters.value(QLatin1String("invert")).toBool(false);

    const QString sourceDevice = binding.value(QLatin1String("sourceDevice")).toString();
    const int sourceAxis = binding.value(QLatin1String("sourceAxis")).toInt(-1);
    applyAxisLogicalRange(sourceDevice, sourceAxis, inputMin, inputMax);
    if (sourceAxis >= 0) {
        int calMin = 0, calMax = 0;
        if (getAxisCalibration(sourceDevice, sourceAxis, calMin, calMax)) {
            inputMin = calMin;
            inputMax = calMax;
        }
    }

    std::vector<QPointF> curvePoints;
    const QJsonValue curvePointsValue = parameters.value(QLatin1String("curvePoints"));
    if (curvePointsValue.isArray()) {
        const QJsonArray curvePointsArray = curvePointsValue.toArray();
        curvePoints.reserve(static_cast<std::size_t>(curvePointsArray.size()));
        for (const QJsonValue &pointValue : curvePointsArray) {
            const QJsonObject pointObject = pointValue.toObject();
            curvePoints.emplace_back(pointObject.value(QLatin1String("x")).toDouble(),
                                      pointObject.value(QLatin1String("y")).toDouble());
        }
    }

    // x1/y1/x2/y2 reproduce CurveHandler's own default identity Bezier
    // curve - the JSON schema does not (yet) expose custom Bezier control
    // points, only the multi-point curvePoints path above, which - when
    // non-empty - overrides these anyway (see CurveHandler's class docs).
    return std::make_shared<CurveHandler>(device, targetAxis, inputMin, inputMax, outputMin, outputMax, deadzone,
                                           sensitivity, smoothingFactor, 1.0 / 3.0, 1.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0,
                                           curvePoints, invert);
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateModeSwitchHandler(const QJsonObject &binding,
                                                                               EventRouter &router)
{
    const QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();
    if (!parameters.contains(QLatin1String("targetMode"))) {
        qWarning() << "ProfileManager: ModeSwitch binding is missing \"parameters.targetMode\" - skipping";
        return nullptr;
    }
    const QString targetMode = parameters.value(QLatin1String("targetMode")).toString();

    return std::make_shared<ModeSwitchHandler>(router, targetMode);
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateButtonRemapHandler(const QJsonObject &binding,
                                                                                EventRouter &router)
{
    auto device = resolveTargetDevice(binding, "ButtonRemapHandler");
    if (!device) {
        return nullptr;
    }
    router.registerOutputDevice(device);

    const int targetButton = binding.value(QLatin1String("targetButton")).toInt();

    return std::make_shared<ButtonRemapHandler>(device, targetButton);
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateHatRemapHandler(const QJsonObject &binding,
                                                                             EventRouter &router)
{
    auto device = resolveTargetDevice(binding, "HatRemapHandler");
    if (!device) {
        return nullptr;
    }
    router.registerOutputDevice(device);

    const int targetHat = binding.value(QLatin1String("targetHat")).toInt();
    const int targetDirection = binding.value(QLatin1String("targetDirection")).toInt();

    return std::make_shared<HatRemapHandler>(device, targetHat, targetDirection);
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateKeyboardHandler(const QJsonObject &binding)
{
    const QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();
    if (!parameters.contains(QLatin1String("scanCode"))) {
        qWarning() << "ProfileManager: KeyboardHandler binding is missing \"parameters.scanCode\" - skipping";
        return nullptr;
    }
    const int scanCode = parameters.value(QLatin1String("scanCode")).toInt();

    return std::make_shared<KeyboardHandler>(keyboardBackend(), static_cast<uint16_t>(scanCode));
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateDelayAction(const QJsonObject &binding)
{
    const QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();
    if (!parameters.contains(QLatin1String("delayMs"))) {
        qWarning() << "ProfileManager: DelayAction binding is missing \"parameters.delayMs\" - skipping";
        return nullptr;
    }
    const int delayMs = parameters.value(QLatin1String("delayMs")).toInt();

    return std::make_shared<DelayAction>(delayMs);
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateAudioAction(const QJsonObject &binding)
{
    const QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();
    if (!parameters.contains(QLatin1String("filePath"))) {
        qWarning() << "ProfileManager: AudioAction binding is missing \"parameters.filePath\" - skipping";
        return nullptr;
    }
    const QString filePath = parameters.value(QLatin1String("filePath")).toString();

    return std::make_shared<AudioAction>(filePath);
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateTTSAction(const QJsonObject &binding)
{
    const QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();
    if (!parameters.contains(QLatin1String("text"))) {
        qWarning() << "ProfileManager: TTSAction binding is missing \"parameters.text\" - skipping";
        return nullptr;
    }
    const QString text = parameters.value(QLatin1String("text")).toString();

    return std::make_shared<TTSAction>(text);
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateTemporaryModeSwitchHandler(const QJsonObject &binding,
                                                                                        EventRouter &router)
{
    const QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();
    if (!parameters.contains(QLatin1String("targetMode"))) {
        qWarning() << "ProfileManager: TemporaryModeSwitch binding is missing \"parameters.targetMode\" - skipping";
        return nullptr;
    }
    const QString targetMode = parameters.value(QLatin1String("targetMode")).toString();

    return std::make_shared<TemporaryModeSwitchHandler>(router, targetMode);
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateTrimHandler(const QJsonObject &binding,
                                                                         EventRouter &router)
{
    if (!binding.contains(QLatin1String("targetOutputId"))) {
        qWarning() << "ProfileManager: TrimHandler binding is missing \"targetOutputId\" - skipping";
        return nullptr;
    }
    const int targetOutputId = binding.value(QLatin1String("targetOutputId")).toInt();
    if (targetOutputId < 1 || targetOutputId > 16) {
        qWarning() << "ProfileManager: targetOutputId" << targetOutputId
                    << "is outside vJoy's valid range [1, 16] - skipping binding";
        return nullptr;
    }

    auto device = VirtualOutputManager::instance().getVJoyDevice(static_cast<uint>(targetOutputId));
    if (!device->acquire()) {
        qWarning() << "ProfileManager: could not acquire vJoy device" << targetOutputId
                    << "- this binding will stage values but nothing will reach the driver";
    }
    router.registerOutputDevice(device);

    const int targetAxis = binding.value(QLatin1String("targetAxis")).toInt();

    const QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();
    if (!parameters.contains(QLatin1String("stepValue"))) {
        qWarning() << "ProfileManager: TrimHandler binding is missing \"parameters.stepValue\" - skipping";
        return nullptr;
    }
    const int stepValue = parameters.value(QLatin1String("stepValue")).toInt();
    const int initialValue = parameters.value(QLatin1String("initialValue")).toInt(16383);

    return std::make_shared<TrimHandler>(device, targetAxis, stepValue, initialValue);
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateMouseRelativeAxisHandler(const QJsonObject &binding,
                                                                                       EventRouter &router)
{
    const QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();
    if (!parameters.contains(QLatin1String("targetMouseAxis"))) {
        qWarning() << "ProfileManager: MouseRelativeAxis binding is missing \"parameters.targetMouseAxis\" - skipping";
        return nullptr;
    }
    const QString axisName = parameters.value(QLatin1String("targetMouseAxis")).toString();

    MouseAxis axis;
    if (axisName == QLatin1String("X")) {
        axis = MouseAxis::X;
    } else if (axisName == QLatin1String("Y")) {
        axis = MouseAxis::Y;
    } else {
        qWarning() << "ProfileManager: MouseRelativeAxis binding has unrecognized \"parameters.targetMouseAxis\""
                    << axisName << "- skipping";
        return nullptr;
    }

    // Fase (bugfix): MouseRelativeAxisHandler's own class docs already
    // documented normalizing against the source axis' real HID logical
    // range (same convention CurveHandler/AxisSplitterHandler take) - but
    // this constructor call never actually resolved that range, so every
    // Mouse Axis binding silently stayed at the [0, 65535] default,
    // regardless of the source device's true bit depth. A 12-bit stick
    // (0-4095) bound this way could never reach full cursor speed in one
    // direction. Wired up now, same pattern as every other axis handler.
    int inputMin = parameters.value(QLatin1String("inputMin")).toInt(0);
    int inputMax = parameters.value(QLatin1String("inputMax")).toInt(65535);
    const QString sourceDevice = binding.value(QLatin1String("sourceDevice")).toString();
    const int sourceAxis = binding.value(QLatin1String("sourceAxis")).toInt(-1);
    applyAxisLogicalRange(sourceDevice, sourceAxis, inputMin, inputMax);
    if (sourceAxis >= 0) {
        int calMin = 0, calMax = 0;
        if (getAxisCalibration(sourceDevice, sourceAxis, calMin, calMax)) {
            inputMin = calMin;
            inputMax = calMax;
        }
    }

    const double sensitivity = parameters.value(QLatin1String("sensitivity")).toDouble(20.0);
    const double deadzone = parameters.value(QLatin1String("deadzone")).toDouble(0.1);

    // Shared by every MouseRelativeAxis binding this router applies - see
    // EventRouter::mouseWorker()'s own docs on why it's router-owned rather
    // than lazily created here the way keyboardBackend() is: its
    // start()/stop() lifecycle must track the router's own, not "whenever
    // the first Mouse Axis binding happens to load".
    return std::make_shared<MouseRelativeAxisHandler>(router.mouseWorker(), axis, inputMin, inputMax, sensitivity,
                                                        deadzone);
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateMouseButtonHandler(const QJsonObject &binding)
{
    const QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();
    if (!parameters.contains(QLatin1String("targetAction"))) {
        qWarning() << "ProfileManager: MouseButton binding is missing \"parameters.targetAction\" - skipping";
        return nullptr;
    }
    const QString targetAction = parameters.value(QLatin1String("targetAction")).toString();

    if (targetAction == QLatin1String("Left") || targetAction == QLatin1String("Right") ||
        targetAction == QLatin1String("Middle") || targetAction == QLatin1String("ScrollUp") ||
        targetAction == QLatin1String("ScrollDown")) {
        return std::make_shared<MouseButtonHandler>(targetAction);
    }

    qWarning() << "ProfileManager: MouseButton binding has unrecognized \"parameters.targetAction\""
                << targetAction << "- skipping";
    return nullptr;
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateConditionHandler(const QJsonObject &binding,
                                                                              EventRouter &router, int depth)
{
    const QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();
    if (!parameters.contains(QLatin1String("modButtonIndex"))) {
        qWarning() << "ProfileManager: ConditionHandler binding is missing \"parameters.modButtonIndex\" - skipping";
        return nullptr;
    }
    const int modButtonIndex = parameters.value(QLatin1String("modButtonIndex")).toInt();
    const QString modSystemPath = parameters.value(QLatin1String("modSystemPath")).toString();
    const bool requirePressed = parameters.value(QLatin1String("requirePressed")).toBool(true);

    if (!binding.contains(QLatin1String("wrappedAction"))) {
        qWarning() << "ProfileManager: ConditionHandler binding is missing \"wrappedAction\" - skipping";
        return nullptr;
    }
    auto wrapped = instantiateHandler(binding.value(QLatin1String("wrappedAction")).toObject(), router, depth + 1);
    if (!wrapped) {
        qWarning() << "ProfileManager: ConditionHandler's \"wrappedAction\" failed to instantiate - skipping";
        return nullptr;
    }

    return std::make_shared<ConditionHandler>(router, modSystemPath, modButtonIndex, requirePressed,
                                               std::move(wrapped));
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateToggleHandler(const QJsonObject &binding,
                                                                           EventRouter &router, int depth)
{
    if (!binding.contains(QLatin1String("wrappedAction"))) {
        qWarning() << "ProfileManager: ToggleHandler binding is missing \"wrappedAction\" - skipping";
        return nullptr;
    }
    auto wrapped = instantiateHandler(binding.value(QLatin1String("wrappedAction")).toObject(), router, depth + 1);
    if (!wrapped) {
        qWarning() << "ProfileManager: ToggleHandler's \"wrappedAction\" failed to instantiate - skipping";
        return nullptr;
    }

    return std::make_shared<ToggleHandler>(std::move(wrapped));
}

std::vector<std::shared_ptr<IActionHandler>> ProfileManager::instantiateActionList(const QJsonObject &binding,
                                                                                      const QString &arrayKey,
                                                                                      const QString &legacySingularKey,
                                                                                      EventRouter &router, int depth)
{
    std::vector<std::shared_ptr<IActionHandler>> actions;

    if (binding.contains(arrayKey)) {
        const QJsonArray actionsArray = binding.value(arrayKey).toArray();
        for (const QJsonValue &actionValue : actionsArray) {
            auto action = instantiateHandler(actionValue.toObject(), router, depth + 1);
            if (!action) {
                qWarning() << "ProfileManager: TempoHandler's" << arrayKey << "entry failed to instantiate"
                               " - skipping that action";
                continue;
            }
            actions.push_back(std::move(action));
        }
    } else if (binding.contains(legacySingularKey)) {
        // Pre-cascade schema (a single action object, not an array) - still
        // accepted so profiles saved before TempoHandler supported multiple
        // actions per gesture keep working instead of silently losing this
        // gesture's action on next load.
        auto action = instantiateHandler(binding.value(legacySingularKey).toObject(), router, depth + 1);
        if (action) {
            actions.push_back(std::move(action));
        }
    }

    return actions;
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateTempoHandler(const QJsonObject &binding,
                                                                          EventRouter &router, int depth)
{
    // Each of shortActions/longActions/doubleActions is individually
    // optional (TempoHandler simply never fires a gesture whose list is
    // empty), but at least one must have a usable action or the binding can
    // never do anything.
    std::vector<std::shared_ptr<IActionHandler>> shortHandlers =
        instantiateActionList(binding, QStringLiteral("shortActions"), QStringLiteral("shortAction"), router, depth);
    std::vector<std::shared_ptr<IActionHandler>> longHandlers =
        instantiateActionList(binding, QStringLiteral("longActions"), QStringLiteral("longAction"), router, depth);
    std::vector<std::shared_ptr<IActionHandler>> doubleHandlers =
        instantiateActionList(binding, QStringLiteral("doubleActions"), QStringLiteral("doubleAction"), router, depth);

    if (shortHandlers.empty() && longHandlers.empty() && doubleHandlers.empty()) {
        qWarning() << "ProfileManager: TempoHandler binding has no usable shortActions/longActions/doubleActions"
                       " - skipping";
        return nullptr;
    }

    const QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();
    const int longPressMs = parameters.value(QLatin1String("longPressMs")).toInt(500);
    const int doubleTapMs = parameters.value(QLatin1String("doubleTapMs")).toInt(250);
    const int pulseDurationMs = parameters.value(QLatin1String("pulseDurationMs")).toInt(50);

    return std::make_shared<TempoHandler>(std::move(shortHandlers), std::move(longHandlers),
                                           std::move(doubleHandlers), longPressMs, doubleTapMs, pulseDurationMs);
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateAxisToButtonHandler(const QJsonObject &binding,
                                                                                 EventRouter &router, int depth)
{
    const QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();
    if (!parameters.contains(QLatin1String("threshold"))) {
        qWarning() << "ProfileManager: AxisToButtonHandler binding is missing \"parameters.threshold\" - skipping";
        return nullptr;
    }
    const int threshold = parameters.value(QLatin1String("threshold")).toInt();
    const bool invert = parameters.value(QLatin1String("invert")).toBool(false);

    if (!binding.contains(QLatin1String("wrappedAction"))) {
        qWarning() << "ProfileManager: AxisToButtonHandler binding is missing \"wrappedAction\" - skipping";
        return nullptr;
    }
    auto wrapped = instantiateHandler(binding.value(QLatin1String("wrappedAction")).toObject(), router, depth + 1);
    if (!wrapped) {
        qWarning() << "ProfileManager: AxisToButtonHandler's \"wrappedAction\" failed to instantiate - skipping";
        return nullptr;
    }

    // threshold above is always authored/compared on the canonical
    // [0, 65535] scale - resolve the source axis' real HID logical range
    // (falling back to the JSON-stored / [0, 65535] default, then a manual
    // calibration override if one exists) so AxisToButtonHandler::
    // processAxis() can normalize into that scale before comparing, same
    // pattern instantiateCurveHandler()/instantiateAxisSplitterHandler()
    // already use.
    int inputMin = parameters.value(QLatin1String("inputMin")).toInt(0);
    int inputMax = parameters.value(QLatin1String("inputMax")).toInt(65535);
    const QString sourceDevice = binding.value(QLatin1String("sourceDevice")).toString();
    const int sourceAxis = binding.value(QLatin1String("sourceAxis")).toInt(-1);
    applyAxisLogicalRange(sourceDevice, sourceAxis, inputMin, inputMax);
    if (sourceAxis >= 0) {
        int calMin = 0, calMax = 0;
        if (getAxisCalibration(sourceDevice, sourceAxis, calMin, calMax)) {
            inputMin = calMin;
            inputMax = calMax;
        }
    }

    return std::make_shared<AxisToButtonHandler>(threshold, invert, std::move(wrapped), inputMin, inputMax);
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateButtonToAxisHandler(const QJsonObject &binding,
                                                                                  EventRouter &router, int depth)
{
    if (!binding.contains(QLatin1String("wrappedAction"))) {
        qWarning() << "ProfileManager: ButtonToAxisHandler binding is missing \"wrappedAction\" - skipping";
        return nullptr;
    }
    auto wrapped = instantiateHandler(binding.value(QLatin1String("wrappedAction")).toObject(), router, depth + 1);
    if (!wrapped) {
        qWarning() << "ProfileManager: ButtonToAxisHandler's \"wrappedAction\" failed to instantiate - skipping";
        return nullptr;
    }

    return std::make_shared<ButtonToAxisHandler>(std::move(wrapped));
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateMergeAxisHandler(const QJsonObject &binding,
                                                                              EventRouter &router)
{
    auto device = resolveTargetDevice(binding, "MergeAxisHandler");
    if (!device) {
        return nullptr;
    }
    router.registerOutputDevice(device);

    const int targetAxis = binding.value(QLatin1String("targetAxis")).toInt();

    const QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();
    if (!parameters.contains(QLatin1String("otherAxisIndex"))) {
        qWarning() << "ProfileManager: MergeAxisHandler binding is missing \"parameters.otherAxisIndex\" - skipping";
        return nullptr;
    }
    const int otherAxisIndex = parameters.value(QLatin1String("otherAxisIndex")).toInt();
    const QString otherSystemPath = parameters.value(QLatin1String("otherSystemPath")).toString();
    const bool isSubtraction = parameters.value(QLatin1String("isSubtraction")).toBool(true);

    // Each side of the merge is normalized from its OWN device's true HID
    // logical range (see MergeAxisHandler's own class docs) rather than
    // assumed to already be [0, 65535] - "self" comes from this binding's
    // own sourceDevice/sourceAxis (same fields/helper instantiateCurveHandler()
    // already uses), "other" from the otherSystemPath/otherAxisIndex parsed
    // above. Falls back to whatever was last saved in the JSON (defaulting
    // to the old unnormalized [0, 65535] assumption) if that device isn't
    // currently connected to query live.
    int selfInputMin = parameters.value(QLatin1String("inputMin")).toInt(0);
    int selfInputMax = parameters.value(QLatin1String("inputMax")).toInt(65535);
    const QString sourceDevice = binding.value(QLatin1String("sourceDevice")).toString();
    const int sourceAxis = binding.value(QLatin1String("sourceAxis")).toInt(-1);
    applyAxisLogicalRange(sourceDevice, sourceAxis, selfInputMin, selfInputMax);

    int otherInputMin = parameters.value(QLatin1String("otherInputMin")).toInt(0);
    int otherInputMax = parameters.value(QLatin1String("otherInputMax")).toInt(65535);
    applyAxisLogicalRange(otherSystemPath, otherAxisIndex, otherInputMin, otherInputMax);

    return std::make_shared<MergeAxisHandler>(router, device, targetAxis, otherSystemPath, otherAxisIndex,
                                               isSubtraction, selfInputMin, selfInputMax, otherInputMin,
                                               otherInputMax);
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateSplitAxisHandler(const QJsonObject &binding,
                                                                              EventRouter &router)
{
    const QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();

    const int lowerTargetOutputId = parameters.value(QLatin1String("lowerTargetOutputId")).toInt();
    const int upperTargetOutputId = parameters.value(QLatin1String("upperTargetOutputId")).toInt();
    if (lowerTargetOutputId < 1 || lowerTargetOutputId > 16 || upperTargetOutputId < 1 || upperTargetOutputId > 16) {
        qWarning() << "ProfileManager: SplitAxisHandler binding has an out-of-range targetOutputId (lower:"
                    << lowerTargetOutputId << "upper:" << upperTargetOutputId << ") - skipping";
        return nullptr;
    }

    auto lowerDevice = VirtualOutputManager::instance().getVJoyDevice(static_cast<uint>(lowerTargetOutputId));
    if (!lowerDevice->acquire()) {
        qWarning() << "ProfileManager: could not acquire vJoy device" << lowerTargetOutputId
                    << "- this binding will stage values but nothing will reach the driver";
    }
    router.registerOutputDevice(lowerDevice);

    auto upperDevice = VirtualOutputManager::instance().getVJoyDevice(static_cast<uint>(upperTargetOutputId));
    if (!upperDevice->acquire()) {
        qWarning() << "ProfileManager: could not acquire vJoy device" << upperTargetOutputId
                    << "- this binding will stage values but nothing will reach the driver";
    }
    router.registerOutputDevice(upperDevice);

    const int lowerTargetAxis = parameters.value(QLatin1String("lowerTargetAxis")).toInt();
    const bool lowerInvert = parameters.value(QLatin1String("lowerInvert")).toBool(false);
    const int upperTargetAxis = parameters.value(QLatin1String("upperTargetAxis")).toInt();
    const bool upperInvert = parameters.value(QLatin1String("upperInvert")).toBool(false);
    const auto splitMode =
        static_cast<SplitAxisHandler::SplitMode>(parameters.value(QLatin1String("splitMode")).toInt(0));

    // BUG-002 fix: resolve the source axis' real HID logical range (falling
    // back to the JSON-stored / [0, 65535] default if the device isn't
    // currently connected) so the split normalizes across the axis' full
    // travel - same helper CurveHandler/MergeAxisHandler already use.
    int inputMin = parameters.value(QLatin1String("inputMin")).toInt(0);
    int inputMax = parameters.value(QLatin1String("inputMax")).toInt(65535);
    const QString sourceDevice = binding.value(QLatin1String("sourceDevice")).toString();
    const int sourceAxis = binding.value(QLatin1String("sourceAxis")).toInt(-1);
    applyAxisLogicalRange(sourceDevice, sourceAxis, inputMin, inputMax);

    return std::make_shared<SplitAxisHandler>(lowerDevice, lowerTargetAxis, lowerInvert, upperDevice, upperTargetAxis,
                                               upperInvert, splitMode, inputMin, inputMax);
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateMacroHandler(const QJsonObject &binding,
                                                                          EventRouter &router)
{
    // "targetOutputId" is optional (Fase 10.9): a pure-keyboard macro (all
    // PressKey/ReleaseKey/Wait steps, e.g. the Macro Editor's Record button)
    // never touches a vJoy device at all - only acquire/register one when a
    // step sequence might actually need it.
    std::shared_ptr<IVirtualOutputDevice> device;
    if (binding.contains(QLatin1String("targetOutputId"))) {
        device = resolveTargetDevice(binding, "MacroHandler");
        if (!device) {
            return nullptr;
        }
        router.registerOutputDevice(device);
    }

    const QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();

    std::vector<MacroStep> steps;
    if (parameters.contains(QLatin1String("steps"))) {
        const QJsonArray stepsArray = parameters.value(QLatin1String("steps")).toArray();
        for (const QJsonValue &stepValue : stepsArray) {
            const QJsonObject stepObject = stepValue.toObject();
            const QString type = stepObject.value(QLatin1String("type")).toString();
            MacroStep step;
            if (type == QLatin1String("PressButton")) {
                step.type = MacroStep::Type::PressButton;
                step.buttonIndex = stepObject.value(QLatin1String("buttonIndex")).toInt();
            } else if (type == QLatin1String("ReleaseButton")) {
                step.type = MacroStep::Type::ReleaseButton;
                step.buttonIndex = stepObject.value(QLatin1String("buttonIndex")).toInt();
            } else if (type == QLatin1String("PressKey")) {
                step.type = MacroStep::Type::PressKey;
                step.scanCode = static_cast<uint16_t>(stepObject.value(QLatin1String("scanCode")).toInt());
            } else if (type == QLatin1String("ReleaseKey")) {
                step.type = MacroStep::Type::ReleaseKey;
                step.scanCode = static_cast<uint16_t>(stepObject.value(QLatin1String("scanCode")).toInt());
            } else if (type == QLatin1String("PressMouseButton")) {
                step.type = MacroStep::Type::PressMouseButton;
                step.mouseAction = stepObject.value(QLatin1String("targetAction")).toString();
            } else if (type == QLatin1String("ReleaseMouseButton")) {
                step.type = MacroStep::Type::ReleaseMouseButton;
                step.mouseAction = stepObject.value(QLatin1String("targetAction")).toString();
            } else if (type == QLatin1String("MouseScroll")) {
                step.type = MacroStep::Type::MouseScroll;
                step.mouseAction = stepObject.value(QLatin1String("targetAction")).toString();
            } else if (type == QLatin1String("Wait")) {
                step.type = MacroStep::Type::Wait;
                step.waitMs = stepObject.value(QLatin1String("waitMs")).toInt();
            } else {
                qWarning() << "ProfileManager: MacroHandler step has unrecognized \"type\"" << type
                            << "- skipping step";
                continue;
            }
            steps.push_back(step);
        }
    } else if (binding.contains(QLatin1String("targetButton"))) {
        // Action Picker "quick timed press" shorthand: press targetButton,
        // hold for parameters.waitMs (default 100), release - no explicit
        // "steps" array required.
        const int targetButton = binding.value(QLatin1String("targetButton")).toInt();
        const int waitMs = parameters.value(QLatin1String("waitMs")).toInt(100);
        steps.push_back(MacroStep{MacroStep::Type::PressButton, targetButton, 0, 0});
        steps.push_back(MacroStep{MacroStep::Type::Wait, 0, waitMs, 0});
        steps.push_back(MacroStep{MacroStep::Type::ReleaseButton, targetButton, 0, 0});
    }

    if (steps.empty()) {
        qWarning() << "ProfileManager: MacroHandler binding has no usable steps"
                       " (\"parameters.steps\" or \"targetButton\") - skipping";
        return nullptr;
    }

    return std::make_shared<MacroHandler>(std::move(device), keyboardBackend(), std::move(steps));
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateAxisSplitterHandler(const QJsonObject &binding,
                                                                                  EventRouter &router)
{
    auto device = resolveTargetDevice(binding, "AxisSplitterHandler");
    if (!device) {
        return nullptr;
    }
    router.registerOutputDevice(device);

    const QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();
    int inputMin = parameters.value(QLatin1String("inputMin")).toInt(0);
    int inputMax = parameters.value(QLatin1String("inputMax")).toInt(65535);

    const QString sourceDevice = binding.value(QLatin1String("sourceDevice")).toString();
    const int sourceAxis = binding.value(QLatin1String("sourceAxis")).toInt(-1);
    applyAxisLogicalRange(sourceDevice, sourceAxis, inputMin, inputMax);
    if (sourceAxis >= 0) {
        int calMin = 0, calMax = 0;
        if (getAxisCalibration(sourceDevice, sourceAxis, calMin, calMax)) {
            inputMin = calMin;
            inputMax = calMax;
        }
    }

    if (!parameters.contains(QLatin1String("zones"))) {
        qWarning() << "ProfileManager: AxisSplitterHandler binding is missing \"parameters.zones\" - skipping";
        return nullptr;
    }

    std::vector<AxisSplitterHandler::Zone> zones;
    const QJsonArray zonesArray = parameters.value(QLatin1String("zones")).toArray();
    for (const QJsonValue &zoneValue : zonesArray) {
        const QJsonObject zoneObject = zoneValue.toObject();
        AxisSplitterHandler::Zone zone;
        zone.minFraction = zoneObject.value(QLatin1String("min")).toDouble(0.0);
        zone.maxFraction = zoneObject.value(QLatin1String("max")).toDouble(1.0);
        zone.targetButton = zoneObject.value(QLatin1String("targetButton")).toInt();
        zones.push_back(zone);
    }

    if (zones.empty()) {
        qWarning() << "ProfileManager: AxisSplitterHandler binding has an empty \"parameters.zones\" - skipping";
        return nullptr;
    }

    return std::make_shared<AxisSplitterHandler>(std::move(device), std::move(zones), inputMin, inputMax);
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateSequenceHandler(const QJsonObject &binding,
                                                                             EventRouter &router, int depth)
{
    const QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();
    if (!parameters.contains(QLatin1String("actions"))) {
        qWarning() << "ProfileManager: SequenceHandler binding is missing \"parameters.actions\" - skipping";
        return nullptr;
    }

    std::vector<std::shared_ptr<IActionHandler>> actions;
    const QJsonArray actionsArray = parameters.value(QLatin1String("actions")).toArray();
    for (const QJsonValue &actionValue : actionsArray) {
        auto action = instantiateHandler(actionValue.toObject(), router, depth + 1);
        if (!action) {
            qWarning() << "ProfileManager: SequenceHandler's action failed to instantiate - skipping that action";
            continue;
        }
        actions.push_back(std::move(action));
    }

    if (actions.empty()) {
        qWarning() << "ProfileManager: SequenceHandler binding has no usable actions - skipping";
        return nullptr;
    }

    return std::make_shared<SequenceHandler>(std::move(actions));
}

std::shared_ptr<IActionHandler> ProfileManager::instantiateSmoothingHandler(const QJsonObject &binding,
                                                                              EventRouter &router, int depth)
{
    const QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();
    const double smoothingFactor = parameters.value(QLatin1String("smoothingFactor")).toDouble(0.0);

    if (!binding.contains(QLatin1String("wrappedAction"))) {
        qWarning() << "ProfileManager: SmoothingHandler binding is missing \"wrappedAction\" - skipping";
        return nullptr;
    }
    auto wrapped = instantiateHandler(binding.value(QLatin1String("wrappedAction")).toObject(), router, depth + 1);
    if (!wrapped) {
        qWarning() << "ProfileManager: SmoothingHandler's \"wrappedAction\" failed to instantiate - skipping";
        return nullptr;
    }

    return std::make_shared<SmoothingHandler>(smoothingFactor, std::move(wrapped));
}

QJsonObject ProfileManager::serializeProfile(const EventRouter &router, const QString &profileName) const
{
    QJsonObject root;
    root[QLatin1String("profileName")] = profileName;

    // Fase (Swap To suggestion): paired with each route's own sourceDevice
    // below when the device happens to be connected right now - lets a
    // future load recognize the same physical device reconnecting under a
    // different systemPath (e.g. after a VID/PID change from a calibration
    // tool) purely by name, even though its offline placeholder by then has
    // nothing but the old systemPath to go on otherwise. A device that's
    // already offline at save time just doesn't get this field written -
    // no different from before this existed.
    QHash<QString, QString> deviceNamesByPath;
    for (const DeviceInfo &device : DeviceManager::instance().getConnectedDevices()) {
        deviceNamesByPath.insert(device.systemPath, device.deviceName);
    }

    QJsonArray bindings;
    for (const EventRouter::RouteDescriptor &route : router.allRoutes()) {
        if (!route.handler) {
            continue;
        }

        QJsonObject binding = route.handler->toJson();
        if (!binding.contains(QLatin1String("actionType"))) {
            // IActionHandler's default toJson() (see its docs) - this
            // handler kind hasn't been taught to describe itself yet.
            // Skipping (rather than writing a binding with no actionType)
            // means the rest of the profile still saves/reloads cleanly;
            // only this one route's binding is lost.
            qWarning() << "ProfileManager: route" << route.systemPath << route.index << "in mode" << route.mode
                        << "has a handler that does not support toJson() - omitting it from the saved profile";
            continue;
        }

        binding[QLatin1String("sourceDevice")] = route.systemPath;
        const QString deviceName = deviceNamesByPath.value(route.systemPath);
        if (!deviceName.isEmpty()) {
            binding[QLatin1String("sourceDeviceName")] = deviceName;
        }
        if (route.isAxis) {
            binding[QLatin1String("sourceAxis")] = route.index;
        } else {
            binding[QLatin1String("sourceButton")] = route.index;
        }
        binding[QLatin1String("mode")] = route.mode;

        // Sprint QoL Part 2: route.handler->toJson() itself never mentions
        // "note" (see IActionHandler::note()'s own docs - it lives on the
        // base class, not in any concrete handler's own serialization
        // logic), so it has to be merged in here rather than already being
        // part of `binding` above. Round-trips back in via
        // ProfileManager::instantiateHandler()'s own thin wrapper around
        // instantiateHandlerImpl(), which reads this exact
        // "parameters.note" key back off the loaded JSON.
        const QString note = route.handler->note();
        if (!note.isEmpty()) {
            QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();
            parameters[QLatin1String("note")] = note;
            binding[QLatin1String("parameters")] = parameters;
        }

        bindings.append(binding);
    }

    root[QLatin1String("bindings")] = bindings;

    // Sprint 5 (Familias de Modos): round-trips the live mode-inheritance
    // tree the same way "bindings" above round-trips the live routing table
    // - read straight from the router (allModeParents()), not a cached copy,
    // so a hierarchy edited via ModeManagerPopup this session always saves
    // exactly what's currently active. Global itself never has an entry
    // (see EventRouter::setModeParent()'s docs), so it never appears here.
    QJsonObject modeHierarchy;
    const QHash<QString, QString> modeParents = router.allModeParents();
    for (auto it = modeParents.constBegin(); it != modeParents.constEnd(); ++it) {
        modeHierarchy[it.key()] = it.value();
    }
    root[QLatin1String("modeHierarchy")] = modeHierarchy;

    // Fase 11: round-trips whatever the Calibration Wizard has captured this
    // session (or loaded from a profile earlier - see loadProfile()) back
    // out, in the same schema loadProfile() reads.
    QJsonArray calibrationsArray;
    for (auto it = m_calibrations.constBegin(); it != m_calibrations.constEnd(); ++it) {
        QJsonObject calibration;
        calibration[QLatin1String("systemPath")] = it.key().first;
        calibration[QLatin1String("axisIndex")] = it.key().second;
        calibration[QLatin1String("calibratedMin")] = it.value().min;
        calibration[QLatin1String("calibratedMax")] = it.value().max;
        calibrationsArray.append(calibration);
    }
    root[QLatin1String("calibrations")] = calibrationsArray;

    return root;
}

bool ProfileManager::renameMode(const QString &oldName, const QString &newName, EventRouter &router)
{
    if (oldName.isEmpty() || newName.isEmpty() || oldName == newName) {
        return false;
    }
    if (oldName == EventRouter::kGlobalMode) {
        qWarning() << "ProfileManager: refusing to rename the Global mode";
        return false;
    }

    const QJsonObject currentProfile = serializeProfile(router);
    const QJsonArray bindings = currentProfile.value(QLatin1String("bindings")).toArray();

    bool oldNameInUse = false;
    bool newNameAlreadyInUse = false;
    for (const QJsonValue &bindingValue : bindings) {
        const QString mode = bindingValue.toObject().value(QLatin1String("mode")).toString();
        if (mode == oldName) {
            oldNameInUse = true;
        }
        if (mode == newName) {
            newNameAlreadyInUse = true;
        }
    }
    if (newNameAlreadyInUse) {
        qWarning() << "ProfileManager: refusing to rename mode" << oldName << "->" << newName
                    << "- that name is already in use by another mode";
        return false;
    }
    if (!oldNameInUse) {
        qInfo() << "ProfileManager: mode" << oldName << "has no bindings yet - nothing to rename in the routing table";
    }

    QJsonArray renamedBindings;
    for (const QJsonValue &bindingValue : bindings) {
        QJsonObject binding = bindingValue.toObject();
        renameModeInBinding(binding, oldName, newName);
        renamedBindings.append(binding);
    }

    // Sprint 5 (Familias de Modos): clearRoutes() below wipes m_modeParents
    // too (see its own docs) - snapshot the hierarchy first and rename
    // oldName wherever it appears, the same way every "mode": oldName
    // binding field above just got rewritten: as a mode's own key (its
    // "Inherits from" edge), and as another mode's stored parent (a mode
    // that inherited FROM oldName must keep inheriting from newName, not
    // silently fall back to being its own root).
    QHash<QString, QString> modeParents = router.allModeParents();
    if (modeParents.contains(oldName)) {
        modeParents[newName] = modeParents.take(oldName);
    }
    for (auto it = modeParents.begin(); it != modeParents.end(); ++it) {
        if (it.value() == oldName) {
            it.value() = newName;
        }
    }

    router.clearRoutes();
    int applied = 0;
    for (const QJsonValue &bindingValue : renamedBindings) {
        if (applyBinding(bindingValue.toObject(), router)) {
            ++applied;
        }
    }
    for (auto it = modeParents.constBegin(); it != modeParents.constEnd(); ++it) {
        router.setModeParent(it.key(), it.value());
    }

    if (router.currentMode() == oldName) {
        router.setMode(newName);
    }

    qInfo() << "ProfileManager: renamed mode" << oldName << "->" << newName << "-" << applied << "of"
            << renamedBindings.size() << "binding(s) reapplied";
    return true;
}

std::shared_ptr<IKeyboardBackend> ProfileManager::keyboardBackend()
{
    if (!m_keyboardBackend) {
        m_keyboardBackend = std::make_shared<SendInputKeyboardBackend>();
    }
    return m_keyboardBackend;
}

bool ProfileManager::getAxisCalibration(const QString &systemPath, int axisIndex, int &outMin, int &outMax) const
{
    const auto key = qMakePair(systemPath, axisIndex);
    if (m_calibrations.contains(key)) {
        const AxisCalibration &cal = m_calibrations[key];
        outMin = cal.min;
        outMax = cal.max;
        return true;
    }
    return false;
}

void ProfileManager::setAxisCalibration(const QString &systemPath, int axisIndex, int calibratedMin, int calibratedMax)
{
    const auto key = qMakePair(systemPath, axisIndex);
    m_calibrations[key] = AxisCalibration{calibratedMin, calibratedMax};
    emit profileChanged();
}

namespace {

/// vJoy Auditor: recursively walks one binding-shaped JSON fragment (as
/// returned by IActionHandler::toJson(), and everything nested under it)
/// marking every vJoy axis/button it targets - see
/// ProfileManager::vjoyOccupancy()'s own docs for exactly which nested
/// shapes this covers and why. inheritedOutputId is the nearest enclosing
/// "targetOutputId" a nested node without its own falls back to (every
/// shape here except SplitAxisHandler's self-contained parameters pair
/// always shares its parent's targetOutputId rather than repeating it).
void scanVjoyOccupancy(const QJsonObject &node, int targetOutputId, int inheritedOutputId,
                        ProfileManager::VJoyOccupancy &occupancy)
{
    const QString deviceType = node.value(QLatin1String("targetDeviceType")).toString(QLatin1String("vjoy"));
    const int outputId = node.contains(QLatin1String("targetOutputId"))
        ? node.value(QLatin1String("targetOutputId")).toInt()
        : inheritedOutputId;

    if (deviceType == QLatin1String("vjoy") && outputId == targetOutputId) {
        if (node.contains(QLatin1String("targetAxis"))) {
            const int axis = node.value(QLatin1String("targetAxis")).toInt();
            if (axis >= 0 && axis < occupancy.axes.size()) {
                occupancy.axes[axis] = true;
            }
        }
        if (node.contains(QLatin1String("targetButton"))) {
            const int button = node.value(QLatin1String("targetButton")).toInt();
            if (button >= 0 && button < occupancy.buttons.size()) {
                occupancy.buttons[button] = true;
            }
        }
    }

    const QJsonValue wrapped = node.value(QLatin1String("wrappedAction"));
    if (wrapped.isObject()) {
        scanVjoyOccupancy(wrapped.toObject(), targetOutputId, outputId, occupancy);
    }

    // TempoHandler's three gesture cascades.
    static const QStringList kActionListKeys = {QStringLiteral("shortActions"), QStringLiteral("longActions"),
                                                   QStringLiteral("doubleActions")};
    for (const QString &key : kActionListKeys) {
        const QJsonArray list = node.value(key).toArray();
        for (const QJsonValue &value : list) {
            if (value.isObject()) {
                scanVjoyOccupancy(value.toObject(), targetOutputId, outputId, occupancy);
            }
        }
    }

    const QJsonObject parameters = node.value(QLatin1String("parameters")).toObject();
    if (parameters.isEmpty()) {
        return;
    }

    // SequenceHandler's own rotary action list.
    const QJsonArray actions = parameters.value(QLatin1String("actions")).toArray();
    for (const QJsonValue &value : actions) {
        if (value.isObject()) {
            scanVjoyOccupancy(value.toObject(), targetOutputId, outputId, occupancy);
        }
    }

    // MacroHandler's PressButton/ReleaseButton steps - share this binding's
    // own top-level targetOutputId, not one of their own.
    if (outputId == targetOutputId) {
        const QJsonArray steps = parameters.value(QLatin1String("steps")).toArray();
        for (const QJsonValue &value : steps) {
            const QJsonObject step = value.toObject();
            if (step.contains(QLatin1String("buttonIndex"))) {
                const int button = step.value(QLatin1String("buttonIndex")).toInt();
                if (button >= 0 && button < occupancy.buttons.size()) {
                    occupancy.buttons[button] = true;
                }
            }
        }

        // AxisSplitterHandler's per-zone targetButton - also shares the
        // top-level targetOutputId.
        const QJsonArray zones = parameters.value(QLatin1String("zones")).toArray();
        for (const QJsonValue &value : zones) {
            const QJsonObject zone = value.toObject();
            if (zone.contains(QLatin1String("targetButton"))) {
                const int button = zone.value(QLatin1String("targetButton")).toInt();
                if (button >= 0 && button < occupancy.buttons.size()) {
                    occupancy.buttons[button] = true;
                }
            }
        }
    }

    // SplitAxisHandler: two fully self-contained (output, axis) pairs of its
    // own, neither one sharing this node's own targetOutputId/inheritedOutputId.
    if (parameters.contains(QLatin1String("lowerTargetAxis"))
        && parameters.value(QLatin1String("lowerTargetOutputId")).toInt() == targetOutputId) {
        const int axis = parameters.value(QLatin1String("lowerTargetAxis")).toInt();
        if (axis >= 0 && axis < occupancy.axes.size()) {
            occupancy.axes[axis] = true;
        }
    }
    if (parameters.contains(QLatin1String("upperTargetAxis"))
        && parameters.value(QLatin1String("upperTargetOutputId")).toInt() == targetOutputId) {
        const int axis = parameters.value(QLatin1String("upperTargetAxis")).toInt();
        if (axis >= 0 && axis < occupancy.axes.size()) {
            occupancy.axes[axis] = true;
        }
    }
}

} // namespace

ProfileManager::VJoyOccupancy ProfileManager::vjoyOccupancy(int targetOutputId, const EventRouter &router) const
{
    VJoyOccupancy occupancy;

    if (targetOutputId < 1 || targetOutputId > 16) {
        return occupancy;
    }

    for (const EventRouter::RouteDescriptor &route : router.allRoutes()) {
        if (!route.handler) {
            continue;
        }
        const QJsonObject binding = route.handler->toJson();
        if (!binding.contains(QLatin1String("actionType"))) {
            continue; // IActionHandler's default toJson() - nothing to scan.
        }
        scanVjoyOccupancy(binding, targetOutputId, /*inheritedOutputId=*/0, occupancy);
    }

    return occupancy;
}

bool ProfileManager::exportCheatsheetPdf(const QString &filePath, const EventRouter &router,
                                           const QString &profileName) const
{
    QPdfWriter writer(filePath);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);

    QPainter painter(&writer);
    if (!painter.isActive()) {
        qWarning() << "ProfileManager: could not open" << filePath << "for PDF writing";
        return false;
    }

    QFont titleFont = painter.font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);

    QFont headerFont = painter.font();
    headerFont.setPointSize(11);
    headerFont.setBold(true);

    QFont rowFont = painter.font();
    rowFont.setPointSize(10);
    rowFont.setBold(false);

    // Row/margin metrics are derived from each font's own line spacing,
    // measured against this exact QPdfWriter (not the screen) - QPdfWriter's
    // device units scale with its resolution() (DPI), so a fixed pixel
    // constant that happened to look right at one resolution silently
    // overlaps every row into an unreadable smear at another. The 1.5x
    // factor leaves air between lines instead of packing them edge to edge.
    const QFontMetrics titleMetrics(titleFont, &writer);
    const QFontMetrics rowMetrics(rowFont, &writer);
    const int rowHeight = static_cast<int>(rowMetrics.lineSpacing() * 1.5);
    const int topMargin = rowHeight;
    const int bottomMargin = rowHeight;

    const int pageWidth = painter.window().width();
    const int pageBottom = painter.window().height() - bottomMargin;
    const int col1 = 0;
    const int col2 = static_cast<int>(pageWidth * 0.45);
    const int col3 = static_cast<int>(pageWidth * 0.68);
    // Small gap kept clear before the next column starts, so an elided
    // "…" never sits flush against the following column's text.
    const int colGap = rowHeight / 3;
    const int col1Width = col2 - col1 - colGap;
    const int col2Width = col3 - col2 - colGap;
    const int col3Width = pageWidth - col3 - colGap;

    // systemPath (a raw, very long Windows device path - see
    // DeviceInfo::systemPath's own docs) is meaningless to a human reading
    // a printed cheatsheet and, worse, was wide enough to overrun the
    // "Input" column into "Action Type"/"Details" - swap in the same
    // human-readable device name the Profiles screen itself shows.
    QHash<QString, QString> deviceNamesByPath;
    for (const DeviceInfo &device : DeviceManager::instance().getConnectedDevices()) {
        deviceNamesByPath.insert(device.systemPath, device.deviceName);
    }

    int y = topMargin;
    painter.setFont(titleFont);
    painter.drawText(col1, y, profileName + QStringLiteral(" - Binding Cheatsheet"));
    y += static_cast<int>(titleMetrics.lineSpacing() * 1.5);

    const auto drawHeaderRow = [&]() {
        painter.setFont(headerFont);
        painter.drawText(col1, y, QStringLiteral("Input"));
        painter.drawText(col2, y, QStringLiteral("Action Type"));
        painter.drawText(col3, y, QStringLiteral("Details"));
        y += static_cast<int>(rowHeight * 0.6);
        painter.drawLine(col1, y, pageWidth, y);
        y += static_cast<int>(rowHeight * 0.6);
    };

    drawHeaderRow();
    painter.setFont(rowFont);

    for (const EventRouter::RouteDescriptor &route : router.allRoutes()) {
        if (y > pageBottom) {
            writer.newPage();
            y = topMargin;
            drawHeaderRow();
            painter.setFont(rowFont);
        }

        const QString deviceLabel = route.systemPath.isEmpty()
            ? QStringLiteral("(any device)")
            : deviceNamesByPath.value(route.systemPath, route.systemPath);
        const QString inputLabel = QStringLiteral("%1 \xC2\xB7 %2 %3 (mode: %4)")
                                        .arg(deviceLabel)
                                        .arg(route.isAxis ? QStringLiteral("Axis") : QStringLiteral("Button"))
                                        .arg(route.index)
                                        .arg(route.mode);

        QString actionType = QStringLiteral("(unsupported)");
        QString details;
        if (route.handler) {
            const QJsonObject binding = route.handler->toJson();
            actionType = binding.value(QLatin1String("actionType")).toString(QStringLiteral("(unsupported)"));

            QStringList detailParts;
            if (binding.contains(QLatin1String("targetOutputId"))) {
                detailParts << QStringLiteral("vJoy %1").arg(binding.value(QLatin1String("targetOutputId")).toInt());
            }
            if (binding.contains(QLatin1String("targetAxis"))) {
                detailParts << QStringLiteral("Axis %1").arg(binding.value(QLatin1String("targetAxis")).toInt());
            }
            if (binding.contains(QLatin1String("targetButton"))) {
                detailParts << QStringLiteral("Button %1").arg(binding.value(QLatin1String("targetButton")).toInt());
            }
            if (binding.contains(QLatin1String("targetHat"))) {
                static const QStringList kDirections{
                    QStringLiteral("Up"), QStringLiteral("Right"), QStringLiteral("Down"), QStringLiteral("Left")};
                const int direction = binding.value(QLatin1String("targetDirection")).toInt();
                const QString directionLabel = direction >= 0 && direction < kDirections.size()
                    ? kDirections.at(direction) : QString::number(direction);
                detailParts << QStringLiteral("Hat %1 %2")
                                   .arg(binding.value(QLatin1String("targetHat")).toInt() + 1)
                                   .arg(directionLabel);
            }
            details = detailParts.join(QStringLiteral(", "));
        }

        // Elided (not just clipped) so a still-too-long value shows a
        // trailing "…" instead of running straight into the next column -
        // same defensive measure regardless of how long a device name,
        // action type, or details string happens to be.
        painter.drawText(col1, y, rowMetrics.elidedText(inputLabel, Qt::ElideRight, col1Width));
        painter.drawText(col2, y, rowMetrics.elidedText(actionType, Qt::ElideRight, col2Width));
        painter.drawText(col3, y, rowMetrics.elidedText(details, Qt::ElideRight, col3Width));
        y += rowHeight;
    }

    painter.end();
    return true;
}

bool ProfileManager::saveProfile(const QString &filePath, const QJsonObject &profileData)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "ProfileManager: could not open" << filePath << "for writing:" << file.errorString();
        return false;
    }

    const QJsonDocument doc(profileData);
    return file.write(doc.toJson(QJsonDocument::Indented)) >= 0;
}
