#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>

/**
 * @brief Diagnostic-only tracer for the "HID enumeration loses devices after
 *        Quit -> relaunch" Heisenbug investigation.
 *
 * qDebug() alone cannot prove what happens during shutdown: LogModel's
 * in-app Log Console lives in the QML UI, which is long gone by the time
 * DeviceManager::~DeviceManager() runs - DeviceManager::instance() is a
 * Meyer's singleton, so its destructor fires via atexit() strictly *after*
 * main()'s own QGuiApplication (and every ViewModel/the QML engine) has
 * already unwound. Anything qDebug()'d from that point on is invisible
 * in-app, and on this WIN32-subsystem build with no console attached, it
 * only reaches OutputDebugString - useless without a debugger/DebugView
 * already attached at the exact moment of reproduction. This appends
 * straight to a plain text file instead, so it survives past process exit
 * unconditionally.
 *
 * Not meant to stay forever - remove once the shutdown-order Heisenbug above
 * is confirmed and fixed.
 */
inline void logShutdownTrace(const QString &message)
{
    qDebug().noquote() << message;

    static const QString path = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/GremlinNexus_shutdown_trace.log");
    QFile file(path);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
               << " [tid " << reinterpret_cast<quintptr>(QThread::currentThreadId()) << "] "
               << " [qApp " << (QCoreApplication::instance() ? "alive" : "NULL") << "] "
               << message << Qt::endl;
    }
}
