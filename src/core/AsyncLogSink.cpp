#include "AsyncLogSink.h"

#include <cstdio>

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QSettings>
#include <QTextStream>
#include <QThread>

namespace {
const auto kEnabledSettingsKey = QStringLiteral("DebugLogging/Enabled");
}

AsyncLogSink &AsyncLogSink::instance()
{
    static AsyncLogSink sink;
    return sink;
}

AsyncLogSink::AsyncLogSink(QObject *parent)
    : QObject(parent)
{
}

AsyncLogSink::~AsyncLogSink() = default;

void AsyncLogSink::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
    m_enabled = QSettings().value(kEnabledSettingsKey, false).toBool();

    m_thread = new QThread();
    moveToThread(m_thread);
    connect(m_thread, &QThread::started, this, [this]() {
        if (m_enabled) {
            openSessionFile();
        }
    });
    m_thread->start();
}

void AsyncLogSink::setEnabled(bool enabled)
{
    if (enabled == m_enabled) {
        return;
    }
    m_enabled = enabled;
    QSettings().setValue(kEnabledSettingsKey, enabled);
    emit enabledChanged(enabled);

    if (!m_started) {
        return; // start() itself will read the persisted value once it runs.
    }
    if (enabled) {
        QMetaObject::invokeMethod(this, &AsyncLogSink::openSessionFile, Qt::QueuedConnection);
    } else {
        QMetaObject::invokeMethod(this, &AsyncLogSink::closeSessionFile, Qt::QueuedConnection);
    }
}

void AsyncLogSink::shutdown()
{
    if (!m_started || !m_thread) {
        return;
    }

    QMetaObject::invokeMethod(this, &AsyncLogSink::closeSessionFile, Qt::BlockingQueuedConnection);

    m_thread->quit();
    m_thread->wait(5000);
    m_thread->deleteLater();
    m_thread = nullptr;
    m_started = false;
}

void AsyncLogSink::openSessionFile()
{
    const QString dirPath = QCoreApplication::applicationDirPath() + QStringLiteral("/Logs");
    QDir().mkpath(dirPath);

    const QString datePart = QDate::currentDate().toString(QStringLiteral("yy.MM.dd"));
    const QString prefix = QStringLiteral("Nexus.Log.%1-").arg(datePart);

    int session = 1;
    const QStringList existing = QDir(dirPath).entryList(QStringList{prefix + QStringLiteral("*.txt")}, QDir::Files);
    for (const QString &name : existing) {
        const QString numberPart = name.mid(prefix.size(), 3);
        bool ok = false;
        const int n = numberPart.toInt(&ok);
        if (ok && n >= session) {
            session = n + 1;
        }
    }

    const QString fileName = QStringLiteral("%1%2.txt").arg(prefix).arg(session, 3, 10, QLatin1Char('0'));
    m_file = new QFile(dirPath + QLatin1Char('/') + fileName, this);
    if (m_file->open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_stream = new QTextStream(m_file);
    } else {
        delete m_file;
        m_file = nullptr;
    }
}

void AsyncLogSink::closeSessionFile()
{
    if (m_stream) {
        m_stream->flush();
        delete m_stream;
        m_stream = nullptr;
    }
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
}

void AsyncLogSink::writeLine(const QString &line)
{
    if (m_stream) {
        *m_stream << line << '\n';
        m_stream->flush();
    }
    fprintf(stderr, "%s\n", qPrintable(line));
}
