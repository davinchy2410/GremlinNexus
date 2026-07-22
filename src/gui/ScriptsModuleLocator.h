#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QString>

/// Fase 19 (Script Bridge): where the separately-downloaded Scripts module
/// (see MasterPlan.md's own "Distribución de Python" entry) is documented to
/// be extracted, next to the executable - shared between SettingsViewModel
/// (scriptsModuleDetected(), just checks the folder exists) and
/// ScriptsViewModel (which actually launches python.exe from inside it), so
/// the folder name/layout can't drift between the two.
namespace ScriptsModuleLocator {

inline QString moduleDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/ScriptsModule");
}

inline bool isModuleDetected()
{
    return QDir(moduleDir()).exists();
}

/// The embedded Python interpreter the module ships - always this path,
/// never a system-installed Python (see MasterPlan.md: "el QProcess siempre
/// lanza ESE intérprete, nunca depende de que el usuario tenga Python
/// instalado o en el PATH").
inline QString pythonExecutablePath()
{
    return moduleDir() + QStringLiteral("/python-embed/python.exe");
}

/// Where the module's own nexus_bridge SDK lives - handed to a launched
/// script via the PYTHONPATH environment variable so `import nexus_bridge`
/// just works with nothing for the script author to install (Fase 19's own
/// "SDK Python" decision).
inline QString bridgeSdkDir()
{
    return moduleDir();
}

} // namespace ScriptsModuleLocator
