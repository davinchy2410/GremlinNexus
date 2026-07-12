#include "LogModel.h"

#include "AsyncLogSink.h"

#include <QDateTime>
#include <QMetaObject>
#include <QTimer>

LogModel &LogModel::instance()
{
    static LogModel model;
    return model;
}

LogModel::LogModel(QObject *parent)
    : QStringListModel(parent)
{
    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(150);
    connect(m_flushTimer, &QTimer::timeout, this, &LogModel::flushPendingUpdate);
    m_flushTimer->start();
}

void LogModel::install()
{
    if (m_installed) {
        return;
    }
    m_installed = true;
    // Deliberately does NOT start AsyncLogSink here - this runs before
    // QGuiApplication is constructed (see main.cpp), and AsyncLogSink's
    // openSessionFile() needs QCoreApplication::applicationDirPath(), which
    // isn't valid yet at this point. main() calls AsyncLogSink::instance().
    // start() itself, right after constructing `app`.
    qInstallMessageHandler(&LogModel::messageHandler);
}

void LogModel::messageHandler(QtMsgType type, const QMessageLogContext & /*context*/, const QString &message)
{
    const char *levelLabel = "DEBUG";
    switch (type) {
    case QtDebugMsg: levelLabel = "DEBUG"; break;
    case QtInfoMsg: levelLabel = "INFO"; break;
    case QtWarningMsg: levelLabel = "WARN"; break;
    case QtCriticalMsg: levelLabel = "ERROR"; break;
    case QtFatalMsg: levelLabel = "FATAL"; break;
    }

    const QString line = QStringLiteral("[%1] %2: %3")
                              .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")))
                              .arg(QString::fromLatin1(levelLabel))
                              .arg(message);

    // Both calls below are Qt::QueuedConnection/AutoConnection - O(1) on
    // whichever thread is logging (just posts an event), never blocks it.
    // appendLine() still runs on the GUI thread (LogModel wasn't moved off
    // it) but only does a cheap insertRow(); the actual console/file I/O
    // happens on AsyncLogSink's own dedicated thread - see its docs for why
    // that split matters (both threads used to be the same one that also
    // runs EventRouter's routing/vJoy tick).
    QMetaObject::invokeMethod(&LogModel::instance(), "appendLine", Qt::AutoConnection, Q_ARG(QString, line));
    QMetaObject::invokeMethod(&AsyncLogSink::instance(), "writeLine", Qt::QueuedConnection, Q_ARG(QString, line));
}

QString LogModel::getAllText() const
{
    return stringList().join(QLatin1Char('\n'));
}

QString LogModel::getHtmlText() const
{
    QStringList html;
    html.reserve(rowCount());
    for (const QString &line : stringList()) {
        QString escaped = line.toHtmlEscaped();
        if (escaped.contains(QLatin1String("WARN"))) {
            html.append(QStringLiteral("<font color=\"#ff9500\">") + escaped + QStringLiteral("</font>"));
        } else if (escaped.contains(QLatin1String("ERROR")) || escaped.contains(QLatin1String("FATAL"))) {
            html.append(QStringLiteral("<font color=\"#ff3b30\">") + escaped + QStringLiteral("</font>"));
        } else {
            html.append(QStringLiteral("<font color=\"#7891ab\">") + escaped + QStringLiteral("</font>"));
        }
    }
    return html.join(QLatin1String("<br>"));
}

void LogModel::appendLine(const QString &line)
{
    const int row = rowCount();
    insertRow(row);
    setData(index(row), line);

    if (rowCount() > kMaxLines) {
        removeRows(0, rowCount() - kMaxLines);
    }
    m_dirty = true;
}

void LogModel::flushPendingUpdate()
{
    if (m_dirty) {
        m_dirty = false;
        emit logUpdated();
    }
}

void LogModel::clearLogs()
{
    setStringList({});
    m_dirty = false;
    emit logUpdated();
}
