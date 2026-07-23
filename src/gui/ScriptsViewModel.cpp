#include "ScriptsViewModel.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include "DeviceInfo.h"
#include "DeviceManager.h"
#include "EventRouter.h"
#include "InputNaming.h"
#include "ScriptBridgeServer.h"
#include "ScriptsModuleLocator.h"

QString ScriptsViewModel::statusToString(Status status)
{
    switch (status) {
        case Status::Running: return QStringLiteral("Running");
        case Status::Crashed: return QStringLiteral("Crashed");
        case Status::Stopped:
        default: return QStringLiteral("Stopped");
    }
}

ScriptsViewModel::ScriptsViewModel(ScriptBridgeServer &bridgeServer, QObject *parent)
    : QObject(parent)
    , m_bridgeServer(bridgeServer)
{
    loadScripts();
    connect(&m_bridgeServer, &ScriptBridgeServer::messageReceived, this, &ScriptsViewModel::onScriptMessageReceived);
    connect(&DeviceManager::instance(), &DeviceManager::axisMoved, this, &ScriptsViewModel::onDeviceAxisMoved);
    connect(&DeviceManager::instance(), &DeviceManager::buttonPressed, this, &ScriptsViewModel::onDeviceButtonPressed);
    connect(&DeviceManager::instance(), &DeviceManager::deviceAdded, this, &ScriptsViewModel::onDeviceListChanged);
    connect(&DeviceManager::instance(), &DeviceManager::deviceRemoved, this, &ScriptsViewModel::onDeviceListChanged);
}

ScriptsViewModel::~ScriptsViewModel()
{
    // Every running script's QProcess is parented to `this` (see
    // startScript()), so Qt's own parent-child cascade would eventually
    // kill them anyway - stopScript()-ing each explicitly first instead
    // means teardownProcess() also releases its ScriptBridgeServer token
    // cleanly rather than leaving a stale one registered past this
    // ViewModel's own lifetime.
    for (auto &entry : m_scripts) {
        if (entry->process) {
            teardownProcess(*entry, /*crashed=*/false);
        }
    }
}

QVariantList ScriptsViewModel::aliasesToVariantList(const std::vector<AliasEntry> &aliases)
{
    QVariantList result;
    result.reserve(static_cast<int>(aliases.size()));
    for (const auto &alias : aliases) {
        QVariantMap map;
        map[QStringLiteral("name")] = alias.name;
        map[QStringLiteral("devicePath")] = alias.devicePath;
        map[QStringLiteral("channelIndex")] = alias.channelIndex;
        map[QStringLiteral("isAxis")] = alias.isAxis;
        result.append(map);
    }
    return result;
}

QVariantList ScriptsViewModel::scripts() const
{
    QVariantList result;
    result.reserve(static_cast<int>(m_scripts.size()));
    for (const auto &entry : m_scripts) {
        QVariantMap map;
        map[QStringLiteral("name")] = entry->name;
        map[QStringLiteral("scriptPath")] = entry->scriptPath;
        map[QStringLiteral("status")] = statusToString(entry->status);
        map[QStringLiteral("inputAliases")] = aliasesToVariantList(entry->inputAliases);
        map[QStringLiteral("outputAliases")] = aliasesToVariantList(entry->outputAliases);
        result.append(map);
    }
    return result;
}

void ScriptsViewModel::addScript(const QString &name, const QString &scriptPath)
{
    // scriptPath arrives as a "file:///..." URL string when called from a
    // QML FileDialog's selectedFile (its own type is QUrl; QML stringifies
    // it crossing into this QString-typed Q_INVOKABLE) - same normalization
    // ProfileEditorViewModel::loadProfileFromPath() already does for the
    // exact same reason, so QProcess::start() below gets a real local path,
    // not a URL string it wouldn't know how to launch.
    const QString localPath = QUrl(scriptPath).isLocalFile() ? QUrl(scriptPath).toLocalFile() : scriptPath;

    if (name.trimmed().isEmpty() || localPath.trimmed().isEmpty()) {
        return;
    }
    // Names identify a script entry across sessions (there is no other
    // stable id a user-facing "which script is this" reference could use),
    // so silently reject a duplicate rather than creating two entries
    // nothing downstream could tell apart.
    const bool nameTaken = std::any_of(m_scripts.begin(), m_scripts.end(),
                                        [&](const auto &entry) { return entry->name == name; });
    if (nameTaken) {
        qWarning() << "ScriptsViewModel: a script named" << name << "already exists - ignoring";
        return;
    }

    auto entry = std::make_unique<ScriptEntry>();
    entry->name = name;
    entry->scriptPath = localPath;
    m_scripts.push_back(std::move(entry));
    saveScripts();
    emit scriptsChanged();
}

void ScriptsViewModel::removeScript(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= m_scripts.size()) {
        return;
    }
    // Never leave an orphaned running process/registered token behind - stop
    // it first if it's currently running.
    if (m_scripts[static_cast<std::size_t>(index)]->process) {
        teardownProcess(*m_scripts[static_cast<std::size_t>(index)], /*crashed=*/false);
    }
    m_scripts.erase(m_scripts.begin() + index);
    saveScripts();
    emit scriptsChanged();
}

void ScriptsViewModel::addInputAlias(int scriptIndex, const QString &name, const QString &devicePath, int channelIndex, bool isAxis)
{
    if (scriptIndex < 0 || static_cast<std::size_t>(scriptIndex) >= m_scripts.size()) {
        return;
    }
    if (name.trimmed().isEmpty() || devicePath.trimmed().isEmpty() || channelIndex < 0) {
        return;
    }
    ScriptEntry &entry = *m_scripts[static_cast<std::size_t>(scriptIndex)];
    const bool nameTaken = std::any_of(entry.inputAliases.begin(), entry.inputAliases.end(),
                                        [&](const auto &alias) { return alias.name == name; });
    if (nameTaken) {
        qWarning() << "ScriptsViewModel:" << entry.name << "already has an input alias named" << name << "- ignoring";
        return;
    }
    entry.inputAliases.push_back(AliasEntry{name, devicePath, channelIndex, isAxis});
    saveScripts();
    emit scriptsChanged();
}

void ScriptsViewModel::removeInputAlias(int scriptIndex, int aliasIndex)
{
    if (scriptIndex < 0 || static_cast<std::size_t>(scriptIndex) >= m_scripts.size()) {
        return;
    }
    ScriptEntry &entry = *m_scripts[static_cast<std::size_t>(scriptIndex)];
    if (aliasIndex < 0 || static_cast<std::size_t>(aliasIndex) >= entry.inputAliases.size()) {
        return;
    }
    entry.inputAliases.erase(entry.inputAliases.begin() + aliasIndex);
    saveScripts();
    emit scriptsChanged();
}

void ScriptsViewModel::addOutputAlias(int scriptIndex, const QString &name, int channelIndex, bool isAxis)
{
    if (scriptIndex < 0 || static_cast<std::size_t>(scriptIndex) >= m_scripts.size()) {
        return;
    }
    if (name.trimmed().isEmpty() || channelIndex < 0) {
        return;
    }
    ScriptEntry &entry = *m_scripts[static_cast<std::size_t>(scriptIndex)];
    const bool nameTaken = std::any_of(entry.outputAliases.begin(), entry.outputAliases.end(),
                                        [&](const auto &alias) { return alias.name == name; });
    if (nameTaken) {
        qWarning() << "ScriptsViewModel:" << entry.name << "already has an output alias named" << name << "- ignoring";
        return;
    }
    // devicePath left empty - an output alias always targets the one shared
    // "Nexus Scripts" virtual device, never a per-alias device (see
    // AliasEntry's own docs).
    entry.outputAliases.push_back(AliasEntry{name, QString(), channelIndex, isAxis});
    saveScripts();
    emit scriptsChanged();
}

void ScriptsViewModel::removeOutputAlias(int scriptIndex, int aliasIndex)
{
    if (scriptIndex < 0 || static_cast<std::size_t>(scriptIndex) >= m_scripts.size()) {
        return;
    }
    ScriptEntry &entry = *m_scripts[static_cast<std::size_t>(scriptIndex)];
    if (aliasIndex < 0 || static_cast<std::size_t>(aliasIndex) >= entry.outputAliases.size()) {
        return;
    }
    entry.outputAliases.erase(entry.outputAliases.begin() + aliasIndex);
    saveScripts();
    emit scriptsChanged();
}

QVariantList ScriptsViewModel::availableInputDevices() const
{
    QVariantList result;
    const QString scriptsPath = EventRouter::scriptsSystemPath();
    for (const DeviceInfo &device : DeviceManager::instance().getConnectedDevices()) {
        if (device.systemPath == scriptsPath) {
            continue; // "Nexus Scripts" is an output target, not a sensible input source.
        }
        QVariantMap map;
        map[QStringLiteral("deviceName")] = device.deviceName;
        map[QStringLiteral("systemPath")] = device.systemPath;
        map[QStringLiteral("numAxes")] = device.numAxes;
        map[QStringLiteral("numButtons")] = device.numButtons;
        result.append(map);
    }
    return result;
}

QStringList ScriptsViewModel::channelNamesForDevice(const QString &systemPath, bool isAxis) const
{
    for (const DeviceInfo &device : DeviceManager::instance().getConnectedDevices()) {
        if (device.systemPath != systemPath) {
            continue;
        }
        QStringList names;
        if (isAxis) {
            names.reserve(device.numAxes);
            for (int i = 0; i < device.numAxes; ++i) {
                names.append(InputNaming::axisDisplayName(i));
            }
        } else {
            names.reserve(device.numButtons);
            for (int i = 0; i < device.numButtons; ++i) {
                names.append(InputNaming::buttonDisplayName(i, device.numButtons, device.numHats));
            }
        }
        return names;
    }
    return {}; // Not currently connected.
}

QString ScriptsViewModel::deviceDisplayName(const QString &systemPath) const
{
    for (const DeviceInfo &device : DeviceManager::instance().getConnectedDevices()) {
        if (device.systemPath == systemPath) {
            return device.deviceName;
        }
    }
    return systemPath; // Not currently connected - best we can show is its raw path.
}

bool ScriptsViewModel::isDeviceConnected(const QString &systemPath) const
{
    for (const DeviceInfo &device : DeviceManager::instance().getConnectedDevices()) {
        if (device.systemPath == systemPath) {
            return true;
        }
    }
    return false;
}

QString ScriptsViewModel::readScriptSource(const QString &scriptPath) const
{
    const QString localPath = QUrl(scriptPath).isLocalFile() ? QUrl(scriptPath).toLocalFile() : scriptPath;
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    constexpr qint64 kMaxPreviewBytes = 262144; // 256 KiB - see this method's own docs.
    const QByteArray data = file.read(kMaxPreviewBytes);
    QString text = QString::fromUtf8(data);
    if (!file.atEnd()) {
        text += QStringLiteral("\n\n[... truncated - file is larger than 256 KB ...]");
    }
    return text;
}

QStringList ScriptsViewModel::suggestedAliasNames(const QString &scriptPath, bool forInput) const
{
    const QString source = readScriptSource(scriptPath);
    if (source.isEmpty()) {
        return {};
    }

    static const QRegularExpression kInputPattern(QStringLiteral(R"(\bon_(?:axis|button)\s*\(\s*["']([^"']+)["'])"));
    static const QRegularExpression kOutputPattern(QStringLiteral(R"(\bset_(?:axis|button)\s*\(\s*["']([^"']+)["'])"));
    const QRegularExpression &pattern = forInput ? kInputPattern : kOutputPattern;

    QStringList names;
    QRegularExpressionMatchIterator it = pattern.globalMatch(source);
    while (it.hasNext()) {
        const QString name = it.next().captured(1);
        if (!names.contains(name)) {
            names.append(name);
        }
    }
    return names;
}

void ScriptsViewModel::onDeviceListChanged()
{
    ++m_deviceListVersion;
    emit deviceListVersionChanged();
}

void ScriptsViewModel::onScriptMessageReceived(const QString &token, const QJsonObject &message)
{
    ScriptEntry *entry = nullptr;
    for (auto &candidate : m_scripts) {
        if (candidate->token == token) {
            entry = candidate.get();
            break;
        }
    }
    if (!entry) {
        return; // Stale/unknown token - nothing to route this to.
    }

    const QString type = message.value(QStringLiteral("type")).toString();
    const QString name = message.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
        return;
    }

    const AliasEntry *alias = nullptr;
    for (const auto &candidate : entry->outputAliases) {
        if (candidate.name == name) {
            alias = &candidate;
            break;
        }
    }
    if (!alias) {
        // Script wrote to a name with no output alias configured - most
        // often a typo, or (as found via real user testing) an alias that
        // got renamed in the panel without updating the script's own
        // set_axis()/set_button() call to match, since the *name* is the
        // literal handshake between the two, not just a display label.
        // Warned once per (script, name) rather than once per message, since
        // a script writing on a timer would otherwise spam this every tick.
        if ((type == QStringLiteral("setAxis") || type == QStringLiteral("setButton"))
            && !entry->warnedMissingOutputAliases.contains(name)) {
            entry->warnedMissingOutputAliases.insert(name);
            qWarning() << "ScriptsViewModel:" << entry->name << "wrote to" << name
                       << "but no output alias with that exact name is configured for it - dropped."
                       << "Check the script's own set_axis()/set_button() calls (View code) against"
                       << "the alias names in the Scripts panel - they must match exactly.";
        }
        return;
    }

    const QString scriptsPath = EventRouter::scriptsSystemPath();
    if (type == QStringLiteral("setAxis") && alias->isAxis) {
        const double normalized = qBound(0.0, message.value(QStringLiteral("value")).toDouble(), 1.0);
        // [0, 65535] - the project-wide default raw-range fallback for a
        // device with no real HID logical min/max (see DeviceManager::
        // injectAxisValue()'s own docs).
        const int rawValue = qRound(normalized * 65535.0);
        DeviceManager::instance().injectAxisValue(scriptsPath, alias->channelIndex, rawValue);
    } else if (type == QStringLiteral("setButton") && !alias->isAxis) {
        const bool pressed = message.value(QStringLiteral("pressed")).toBool();
        DeviceManager::instance().injectButtonPress(scriptsPath, alias->channelIndex, pressed);
    }
}

namespace {
/// Normalizes a raw HID axis reading to [0.0, 1.0] using systemPath's own
/// real logical range if DeviceManager currently knows it, else the same
/// [0, 65535] project-wide default fallback ProfileManager.cpp's
/// applyAxisLogicalRange() uses (see this class' own docs on step 4/6).
double normalizeAxisValue(const QString &systemPath, int axisIndex, int rawValue)
{
    int inputMin = 0;
    int inputMax = 65535;
    for (const DeviceInfo &device : DeviceManager::instance().getConnectedDevices()) {
        if (device.systemPath == systemPath && axisIndex >= 0 && axisIndex < device.axisLogicalMin.size()) {
            inputMin = device.axisLogicalMin[axisIndex];
            inputMax = device.axisLogicalMax[axisIndex];
            break;
        }
    }
    if (inputMax <= inputMin) {
        return 0.0; // Degenerate/unknown range - avoid a divide-by-zero.
    }
    return qBound(0.0, static_cast<double>(rawValue - inputMin) / static_cast<double>(inputMax - inputMin), 1.0);
}
} // namespace

void ScriptsViewModel::onDeviceAxisMoved(const QString &systemPath, int axisIndex, int value)
{
    if (m_scripts.empty()) {
        return; // Cheap early-out - this fires on every axis report from every physical device.
    }
    const double normalized = normalizeAxisValue(systemPath, axisIndex, value);
    for (const auto &entry : m_scripts) {
        if (entry->token.isEmpty()) {
            continue; // Not currently running/authenticated - nothing to send to.
        }
        for (const AliasEntry &alias : entry->inputAliases) {
            if (alias.isAxis && alias.devicePath == systemPath && alias.channelIndex == axisIndex) {
                const QJsonObject message{
                    {QStringLiteral("type"), QStringLiteral("axisState")},
                    {QStringLiteral("name"), alias.name},
                    {QStringLiteral("value"), normalized},
                };
                m_bridgeServer.sendToScript(entry->token, message);
            }
        }
    }
}

void ScriptsViewModel::onDeviceButtonPressed(const QString &systemPath, int buttonIndex, bool pressed)
{
    if (m_scripts.empty()) {
        return;
    }
    for (const auto &entry : m_scripts) {
        if (entry->token.isEmpty()) {
            continue;
        }
        for (const AliasEntry &alias : entry->inputAliases) {
            if (!alias.isAxis && alias.devicePath == systemPath && alias.channelIndex == buttonIndex) {
                const QJsonObject message{
                    {QStringLiteral("type"), QStringLiteral("buttonState")},
                    {QStringLiteral("name"), alias.name},
                    {QStringLiteral("pressed"), pressed},
                };
                m_bridgeServer.sendToScript(entry->token, message);
            }
        }
    }
}

void ScriptsViewModel::startScript(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= m_scripts.size()) {
        return;
    }
    ScriptEntry &entry = *m_scripts[static_cast<std::size_t>(index)];
    if (entry.process) {
        return; // Already running.
    }

    if (!ScriptsModuleLocator::isModuleDetected()) {
        qWarning() << "ScriptsViewModel: cannot start" << entry.name << "- Scripts module not detected";
        return;
    }

    entry.token = m_bridgeServer.registerScriptToken();
    entry.stopRequested = false;

    auto *process = new QProcess(this);
    entry.process = process;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("NEXUS_BRIDGE_HOST"), QStringLiteral("127.0.0.1"));
    env.insert(QStringLiteral("NEXUS_BRIDGE_PORT"), QString::number(m_bridgeServer.port()));
    env.insert(QStringLiteral("NEXUS_BRIDGE_TOKEN"), entry.token);
    // Lets `import nexus_bridge` resolve with nothing for the script author
    // to install - see MasterPlan.md's own "SDK Python" decision.
    env.insert(QStringLiteral("PYTHONPATH"), ScriptsModuleLocator::bridgeSdkDir());
    // Python block-buffers stdout/stderr whenever they aren't a real
    // terminal (true here - they're piped straight into this QProcess), so
    // without this a script's own print() output would sit in Python's
    // internal buffer and never reach readyReadStandardOutput below until
    // that buffer filled or the process exited - making the Log Console
    // look silent for a script that's actually running fine.
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    process->setProcessEnvironment(env);

    // this-based lookup (not an index capture) - entry's own storage is
    // stable (see ScriptEntry's own docs on unique_ptr), and this stays
    // correct even if other scripts are added/removed while this one runs.
    connect(process, &QProcess::readyReadStandardOutput, this, [this, &entry, process]() {
        const QString text = QString::fromLocal8Bit(process->readAllStandardOutput());
        for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            qInfo().noquote() << QStringLiteral("[Script: %1] %2").arg(entry.name, line.trimmed());
        }
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, &entry, process]() {
        const QString text = QString::fromLocal8Bit(process->readAllStandardError());
        for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            qWarning().noquote() << QStringLiteral("[Script: %1] %2").arg(entry.name, line.trimmed());
        }
    });
    connect(process, &QProcess::errorOccurred, this, [this, &entry](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            qWarning() << "ScriptsViewModel:" << entry.name << "failed to start (missing python.exe or script file?)";
            teardownProcess(entry, /*crashed=*/true);
        }
        // Other QProcess::ProcessError values (Crashed, Timedout, ...) are
        // always followed by finished() too (Qt guarantees this since a
        // process that got as far as starting always eventually finishes,
        // one way or another) - handled there instead, so a crash isn't
        // torn down twice.
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, &entry](int exitCode, QProcess::ExitStatus exitStatus) {
        Q_UNUSED(exitCode);
        teardownProcess(entry, /*crashed=*/exitStatus == QProcess::CrashExit);
    });

    entry.status = Status::Running;
    process->start(ScriptsModuleLocator::pythonExecutablePath(), {entry.scriptPath});
    emit scriptsChanged();
}

void ScriptsViewModel::stopScript(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= m_scripts.size()) {
        return;
    }
    ScriptEntry &entry = *m_scripts[static_cast<std::size_t>(index)];
    if (!entry.process) {
        return; // Already stopped.
    }
    entry.stopRequested = true;
    entry.process->terminate();
    // Gives the script's own Python interpreter a chance to shut down
    // cleanly (e.g. close its own bridge socket) before escalating to a
    // hard kill() - via a timer, not QProcess::waitForFinished(), which
    // blocks this (the GUI) thread synchronously and freezes the whole UI
    // for up to the full timeout. teardownProcess() (called from the
    // finished() handler this triggers, one way or another) is what
    // actually clears entry.process/token - this timer only ever escalates.
    // QPointer so this safely no-ops if the process already finished on its
    // own by then (deleteLater()'d from teardownProcess()) or the script
    // entry was removed entirely (removeScript() torn its process down too).
    QPointer<QProcess> processGuard(entry.process);
    QTimer::singleShot(2000, this, [processGuard]() {
        if (processGuard) {
            processGuard->kill();
        }
    });
}

void ScriptsViewModel::teardownProcess(ScriptEntry &entry, bool crashed)
{
    if (!entry.process) {
        return; // Already torn down - see errorOccurred()'s own docs on why this can be called twice.
    }

    m_bridgeServer.unregisterScriptToken(entry.token);
    entry.token.clear();

    // Disconnects every signal this connected on entry.process first - the
    // process can still emit readyReadStandardOutput/errorOccurred/finished
    // right up until its actual (deferred) destruction below, and every one
    // of those lambdas (see startScript()) captures &entry by reference.
    // removeScript() calls this then immediately erases the very ScriptEntry
    // that reference points at - without disconnecting first, a
    // late-arriving signal from a process that's still shutting down would
    // touch already-freed memory.
    entry.process->disconnect(this);
    // Safe no-op if the process already exited on its own (the usual
    // finished()-triggered path here, going through stopScript()'s own
    // terminate()-then-escalate first) - only matters for removeScript()'s
    // "delete a still-running script outright" path, so the process
    // actually dies now instead of QProcess's own deleteLater() destructor
    // abruptly (and noisily - "QProcess: Destroyed while process is still
    // running") killing it later.
    entry.process->kill();
    entry.process->deleteLater();
    entry.process = nullptr;

    // A Stop-requested forced kill() reports as QProcess::CrashExit too
    // (Windows has no graceful way to end a console-less child that isn't
    // already listening for it) - stopRequested means this exit was asked
    // for, so it belongs in Status::Stopped, not the misleading Crashed.
    const bool actuallyCrashed = crashed && !entry.stopRequested;
    entry.status = actuallyCrashed ? Status::Crashed : Status::Stopped;
    entry.stopRequested = false;

    // Fase 19.6 step 5/6 (safety): whether this script stopped cleanly or
    // crashed, every channel of "Nexus Scripts" it was writing to must not
    // stay stuck at its last value - MasterPlan.md's own Fase 19 design
    // calls this out explicitly (e.g. a nose-wheel brake output frozen at
    // full deflection because the script that was driving it died). Axis
    // neutral is 0 (matches nexus_bridge's own set_axis() convention, where
    // 0.0 is "idle/released" - not the center of a bipolar stick), button
    // neutral is released.
    const QString scriptsPath = EventRouter::scriptsSystemPath();
    for (const AliasEntry &alias : entry.outputAliases) {
        if (alias.isAxis) {
            DeviceManager::instance().injectAxisValue(scriptsPath, alias.channelIndex, 0);
        } else {
            DeviceManager::instance().injectButtonPress(scriptsPath, alias.channelIndex, false);
        }
    }

    emit scriptsChanged();
}

QJsonArray ScriptsViewModel::aliasesToJson(const std::vector<AliasEntry> &aliases)
{
    QJsonArray array;
    for (const auto &alias : aliases) {
        QJsonObject obj;
        obj[QStringLiteral("name")] = alias.name;
        obj[QStringLiteral("devicePath")] = alias.devicePath;
        obj[QStringLiteral("channelIndex")] = alias.channelIndex;
        obj[QStringLiteral("isAxis")] = alias.isAxis;
        array.append(obj);
    }
    return array;
}

std::vector<ScriptsViewModel::AliasEntry> ScriptsViewModel::aliasesFromJson(const QJsonArray &array)
{
    std::vector<AliasEntry> aliases;
    aliases.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();
        const QString name = obj.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) {
            continue;
        }
        AliasEntry alias;
        alias.name = name;
        alias.devicePath = obj.value(QStringLiteral("devicePath")).toString();
        alias.channelIndex = obj.value(QStringLiteral("channelIndex")).toInt();
        alias.isAxis = obj.value(QStringLiteral("isAxis")).toBool(true);
        aliases.push_back(std::move(alias));
    }
    return aliases;
}

QString ScriptsViewModel::configFilePath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/scripts_config.json");
}

void ScriptsViewModel::loadScripts()
{
    QFile file(configFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return; // Not an error - a fresh install simply has no scripts configured yet.
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isArray()) {
        qWarning() << "ScriptsViewModel: ignoring" << file.fileName() << "- not a valid JSON array";
        return;
    }

    for (const QJsonValue &value : doc.array()) {
        const QJsonObject obj = value.toObject();
        const QString name = obj.value(QStringLiteral("name")).toString();
        const QString scriptPath = obj.value(QStringLiteral("scriptPath")).toString();
        if (name.isEmpty() || scriptPath.isEmpty()) {
            continue;
        }
        auto entry = std::make_unique<ScriptEntry>();
        entry->name = name;
        entry->scriptPath = scriptPath;
        entry->inputAliases = aliasesFromJson(obj.value(QStringLiteral("inputAliases")).toArray());
        entry->outputAliases = aliasesFromJson(obj.value(QStringLiteral("outputAliases")).toArray());
        m_scripts.push_back(std::move(entry));
    }
}

void ScriptsViewModel::saveScripts() const
{
    QJsonArray array;
    for (const auto &entry : m_scripts) {
        QJsonObject obj;
        obj[QStringLiteral("name")] = entry->name;
        obj[QStringLiteral("scriptPath")] = entry->scriptPath;
        obj[QStringLiteral("inputAliases")] = aliasesToJson(entry->inputAliases);
        obj[QStringLiteral("outputAliases")] = aliasesToJson(entry->outputAliases);
        array.append(obj);
    }

    QFile file(configFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "ScriptsViewModel: could not write" << file.fileName() << ":" << file.errorString();
        return;
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
}
