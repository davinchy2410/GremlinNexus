#include "HidHideManager.h"

#include <windows.h>

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QProcess>

namespace {
QString toInstanceId(const QString &rawPath)
{
    // Convierte "\\?\hid#vid_3344&pid_01fa&mi_00#a&143322e5&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}"
    // a "HID\VID_3344&PID_01FA&MI_00\a&143322e5&0&0000" (formato que usa HidHideCLI)
    QString id = rawPath;
    if (id.startsWith(QLatin1String("\\\\?\\"))) {
        id = id.mid(4);
    }
    int guidIndex = id.indexOf(QLatin1String("#{"));
    if (guidIndex > 0) {
        id = id.left(guidIndex);
    }
    id.replace(QLatin1Char('#'), QLatin1Char('\\'));
    
    // El prefijo "hid\vid..." debe ser "HID\VID..." para que coincida visualmente y estructuralmente, 
    // aunque Windows no sea case-sensitive, HidHide puede ser quisquilloso en su UI.
    // Capitalizamos solo el Vendor y Product ID (la primera parte hasta el último \).
    int lastSlash = id.lastIndexOf(QLatin1Char('\\'));
    if (lastSlash > 0) {
        QString prefix = id.left(lastSlash).toUpper();
        QString suffix = id.mid(lastSlash);
        id = prefix + suffix;
    }
    return id;
}

const QString kCliPath =
    QStringLiteral("C:\\Program Files\\Nefarius Software Solutions\\HidHide\\x64\\HidHideCLI.exe");

/// Milliseconds to wait for one HidHideCLI.exe invocation before giving up
/// on a clean exit. HidHideCLI.exe finishes its actual work (the driver
/// IOCTL and any stdout) in well under 100ms, but - independent of Qt,
/// confirmed with a bare Win32 CreateProcess/WaitForSingleObject launch too
/// (see runCli()'s docs below) - it does not reliably exit on its own once
/// its stdout is redirected to a pipe rather than an interactive console.
/// So this is not "how long the command needs" but "how much margin to give
/// its actual work before deciding it's in that lingering state and moving
/// on" - short, since runCli() already treats hitting this as the expected
/// case, not a failure.
constexpr int kProcessTimeoutMs = 800;
} // namespace

HidHideManager &HidHideManager::instance()
{
    static HidHideManager instance;
    return instance;
}

HidHideManager::HidHideManager(QObject *parent) : QObject(parent)
{
}

bool HidHideManager::isInstalled() const
{
    return QFile::exists(kCliPath);
}

bool HidHideManager::runCli(const QStringList &arguments, QString *stdOut) const
{
    if (!isInstalled()) {
        return false;
    }

    if (!stdOut) {
        // If we don't need the output, run the CLI detached. This completely bypasses
        // the pipe lingering issue, meaning it executes and exits instantly in the 
        // background without blocking our main GUI thread for kProcessTimeoutMs.
        return QProcess::startDetached(kCliPath, arguments);
    }

    QProcess process;
    // GremblingEx.exe runs as a windowless (WIN32-subsystem) GUI app, so it
    // has no console of its own - without this, spawning a console-
    // subsystem child like HidHideCLI.exe makes Windows allocate it a brand
    // new console, which can stall process creation (observed hanging past
    // several seconds during testing) and would briefly flash a black
    // window on screen even when it doesn't. CREATE_NO_WINDOW skips that
    // allocation entirely; stdout/stderr are still redirected through
    // QProcess' own pipes regardless; a console is not needed for that.
    process.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
    process.start(kCliPath, arguments);

    // See kProcessTimeoutMs's docs: HidHideCLI.exe reliably does its actual
    // work almost immediately but can linger past that without exiting when
    // launched non-interactively, so a timeout here is read as "the command
    // already ran, it just hasn't exited yet" rather than a failure - stdout
    // is read regardless of which branch below is taken, since whatever the
    // CLI already wrote is sitting in the pipe either way.
    const bool exitedCleanly = process.waitForFinished(kProcessTimeoutMs);
    if (stdOut) {
        *stdOut = QString::fromLocal8Bit(process.readAllStandardOutput());
    }

    if (!exitedCleanly) {
        qInfo() << "HidHideManager: HidHideCLI.exe did not exit within" << kProcessTimeoutMs << "ms running"
                << arguments << "- treating as completed (known to linger post-work when piped) and killing it";
        process.kill();
        process.waitForFinished(500);
        return true;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        qWarning() << "HidHideManager: HidHideCLI.exe" << arguments << "exited with code" << process.exitCode();
    }

    return process.exitStatus() == QProcess::NormalExit;
}

void HidHideManager::whitelistApplication()
{
    runCli({QStringLiteral("--app-reg"), QCoreApplication::applicationFilePath()});
}

void HidHideManager::setGlobalCloak(bool enabled)
{
    runCli({enabled ? QStringLiteral("--cloak-on") : QStringLiteral("--cloak-off")});
}

void HidHideManager::cloakDevice(const QString &rawSystemPath)
{
    const QString instanceId = toInstanceId(rawSystemPath);
    runCli({QStringLiteral("--dev-hide"), instanceId});
    setGlobalCloak(true);
    
    // We compare case-insensitively just in case
    bool found = false;
    for (const QString &c : m_cloakedCache) {
        if (c.compare(instanceId, Qt::CaseInsensitive) == 0) {
            found = true;
            break;
        }
    }
    if (m_cacheValid && !found) {
        m_cloakedCache.append(instanceId);
    }
}

void HidHideManager::uncloakDevice(const QString &rawSystemPath)
{
    const QString instanceId = toInstanceId(rawSystemPath);
    runCli({QStringLiteral("--dev-unhide"), instanceId});
    
    if (m_cacheValid) {
        for (int i = 0; i < m_cloakedCache.size(); ++i) {
            if (m_cloakedCache[i].compare(instanceId, Qt::CaseInsensitive) == 0) {
                m_cloakedCache.removeAt(i);
                break;
            }
        }
    }
}

bool HidHideManager::isDeviceCloaked(const QString &rawSystemPath) const
{
    const QString instanceId = toInstanceId(rawSystemPath);
    const QStringList cloaked = getCloakedDevices();
    for (const QString &c : cloaked) {
        if (c.compare(instanceId, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QStringList HidHideManager::getCloakedDevices() const
{
    if (m_cacheValid) {
        return m_cloakedCache;
    }

    QString output;
    if (!runCli({QStringLiteral("--dev-list")}, &output)) {
        return {};
    }

    // Real HidHideCLI output is not a bare list of paths - each line is a
    // ready-to-replay command, e.g.:
    //   --dev-hide "HID\VID_045E&PID_028E\9&a2ba05b&1&0000"
    // so the instance path has to be pulled out from between the quotes
    // rather than treated as the whole line (a previous version of this
    // parser did the latter, which meant isDeviceCloaked()'s exact-string
    // match against a bare systemPath could never succeed).
    QStringList cloaked;
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const int firstQuote = line.indexOf(QLatin1Char('"'));
        const int lastQuote = line.lastIndexOf(QLatin1Char('"'));
        if (firstQuote < 0 || lastQuote <= firstQuote) {
            continue;
        }
        const QString path = line.mid(firstQuote + 1, lastQuote - firstQuote - 1).trimmed();
        if (!path.isEmpty()) {
            cloaked.append(path);
        }
    }
    
    m_cloakedCache = cloaked;
    m_cacheValid = true;
    return cloaked;
}
