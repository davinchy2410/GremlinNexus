#include "LogModel.h"

#include <cstdio>

#include <QDateTime>
#include <QMetaObject>

LogModel &LogModel::instance()
{
    static LogModel model;
    return model;
}

LogModel::LogModel(QObject *parent)
    : QStringListModel(parent)
{
}

void LogModel::install()
{
    if (m_installed) {
        return;
    }
    m_installed = true;
    m_previousHandler = qInstallMessageHandler(&LogModel::messageHandler);
}

void LogModel::messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
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

    QMetaObject::invokeMethod(&LogModel::instance(), "appendLine", Qt::AutoConnection, Q_ARG(QString, line));

    // Still forward to whatever handler (if any) was installed before this
    // one - capturing logs for the in-app console must not silence the
    // console/debugger output every existing qWarning()/qInfo() call site
    // already relies on.
    if (LogModel::instance().m_previousHandler) {
        LogModel::instance().m_previousHandler(type, context, message);
    } else {
        fprintf(stderr, "%s\n", qPrintable(line));
    }
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
    emit logUpdated();
}

void LogModel::clearLogs()
{
    setStringList({});
    emit logUpdated();
}
