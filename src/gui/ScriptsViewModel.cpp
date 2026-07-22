#include "ScriptsViewModel.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QUrl>
#include <QVariantMap>

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

QVariantList ScriptsViewModel::scripts() const
{
    QVariantList result;
    result.reserve(static_cast<int>(m_scripts.size()));
    for (const auto &entry : m_scripts) {
        QVariantMap map;
        map[QStringLiteral("name")] = entry->name;
        map[QStringLiteral("scriptPath")] = entry->scriptPath;
        map[QStringLiteral("status")] = statusToString(entry->status);
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
    entry->scriptPath = scriptPath;
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

    auto *process = new QProcess(this);
    entry.process = process;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("NEXUS_BRIDGE_HOST"), QStringLiteral("127.0.0.1"));
    env.insert(QStringLiteral("NEXUS_BRIDGE_PORT"), QString::number(m_bridgeServer.port()));
    env.insert(QStringLiteral("NEXUS_BRIDGE_TOKEN"), entry.token);
    // Lets `import nexus_bridge` resolve with nothing for the script author
    // to install - see MasterPlan.md's own "SDK Python" decision.
    env.insert(QStringLiteral("PYTHONPATH"), ScriptsModuleLocator::bridgeSdkDir());
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
    entry.process->terminate();
    // Gives the script's own Python interpreter a chance to shut down
    // cleanly (e.g. close its own bridge socket) before escalating -
    // teardownProcess() itself (called from the finished() handler this
    // triggers, or here directly if it doesn't exit in time) is what
    // actually clears entry.process/token.
    if (!entry.process->waitForFinished(2000)) {
        entry.process->kill();
    }
}

void ScriptsViewModel::teardownProcess(ScriptEntry &entry, bool crashed)
{
    if (!entry.process) {
        return; // Already torn down - see errorOccurred()'s own docs on why this can be called twice.
    }

    m_bridgeServer.unregisterScriptToken(entry.token);
    entry.token.clear();

    entry.process->deleteLater();
    entry.process = nullptr;

    entry.status = crashed ? Status::Crashed : Status::Stopped;
    emit scriptsChanged();
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
        array.append(obj);
    }

    QFile file(configFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "ScriptsViewModel: could not write" << file.fileName() << ":" << file.errorString();
        return;
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
}
