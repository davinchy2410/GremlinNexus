#include "AutoSwitchManager.h"

#include <windows.h>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {
constexpr int kPollIntervalMs = 1000;
} // namespace

AutoSwitchManager::AutoSwitchManager(QObject *parent) : QObject(parent)
{
    loadRules();

    m_pollTimer.setInterval(kPollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &AutoSwitchManager::poll);
    if (m_enabled) {
        m_pollTimer.start();
    }
}

void AutoSwitchManager::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    if (enabled) {
        m_pollTimer.start();
    } else {
        m_pollTimer.stop();
    }
    saveRules();
}

void AutoSwitchManager::addRule(const QString &exeName, const QString &profilePath)
{
    m_rules.insert(exeName.toLower(), profilePath);
    saveRules();
}

void AutoSwitchManager::removeRule(const QString &exeName)
{
    m_rules.remove(exeName.toLower());
    saveRules();
}

void AutoSwitchManager::setDefaultProfile(const QString &profilePath)
{
    m_defaultProfilePath = profilePath;
    saveRules();
}

void AutoSwitchManager::poll()
{
    const HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        // No window has focus at all (shutdown, lock screen, desktop with
        // nothing selected, ...) - nothing to switch to, and not a reason
        // to forget m_currentExe either (the same app is almost certainly
        // still what regains focus next).
        return;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        return;
    }

    const HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!processHandle) {
        // Protected/system process (e.g. some elevated or OS-owned windows)
        // this unprivileged process can't query - silently skip rather than
        // spamming a warning every single tick it stays in the foreground.
        return;
    }

    wchar_t pathBuffer[MAX_PATH];
    DWORD pathLength = MAX_PATH;
    const bool ok = QueryFullProcessImageNameW(processHandle, 0, pathBuffer, &pathLength);
    CloseHandle(processHandle);
    if (!ok) {
        return;
    }

    const QString exeName =
        QFileInfo(QString::fromWCharArray(pathBuffer, static_cast<int>(pathLength))).fileName().toLower();
    if (exeName == m_currentExe) {
        // Still the same foreground app as last tick - nothing to do, and
        // (more importantly) nothing to re-emit.
        return;
    }
    m_currentExe = exeName;

    const auto ruleIt = m_rules.constFind(exeName);
    if (ruleIt != m_rules.constEnd()) {
        emit profileSwitchRequested(ruleIt.value());
    } else if (!m_defaultProfilePath.isEmpty()) {
        emit profileSwitchRequested(m_defaultProfilePath);
    }
}

QString AutoSwitchManager::rulesFilePath()
{
    // NOT applicationDirPath() (bug fixed 2026-07-28): a standard
    // (non-elevated) install lands under Program Files, which a normal
    // user process cannot write to - saveRules() below was silently
    // failing every time (only a qWarning(), never surfaced to the user),
    // so "enabled" and every configured rule quietly reverted to whatever
    // loadRules() last found - "true" if the file couldn't be read yet -
    // on the very next launch. AppLocalDataLocation resolves to
    // %LOCALAPPDATA%\Antigravity\GremblingEx (see main.cpp's
    // setOrganizationName/setApplicationName), always writable by the
    // user who's running the app regardless of install location.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/autoswitch_rules.json");
}

void AutoSwitchManager::loadRules()
{
    QFile file(rulesFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        // Not an error - a fresh install simply has no rules configured yet.
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        qWarning() << "AutoSwitchManager: ignoring" << file.fileName() << "- not a valid JSON object";
        return;
    }

    const QJsonObject root = doc.object();
    m_defaultProfilePath = root.value(QStringLiteral("defaultProfile")).toString();
    m_enabled = root.value(QStringLiteral("enabled")).toBool(true);

    const QJsonObject rules = root.value(QStringLiteral("rules")).toObject();
    for (auto it = rules.constBegin(); it != rules.constEnd(); ++it) {
        m_rules.insert(it.key().toLower(), it.value().toString());
    }
}

void AutoSwitchManager::saveRules() const
{
    QJsonObject rules;
    for (auto it = m_rules.constBegin(); it != m_rules.constEnd(); ++it) {
        rules[it.key()] = it.value();
    }

    QJsonObject root;
    root[QStringLiteral("defaultProfile")] = m_defaultProfilePath;
    root[QStringLiteral("enabled")] = m_enabled;
    root[QStringLiteral("rules")] = rules;

    QFile file(rulesFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "AutoSwitchManager: could not write" << file.fileName() << ":" << file.errorString();
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}
