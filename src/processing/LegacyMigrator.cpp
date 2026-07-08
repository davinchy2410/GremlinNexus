#include "LegacyMigrator.h"

#include <QDebug>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QUrl>

#include "LegacyProfileImporter.h"

LegacyMigrator::LegacyMigrator(QObject *parent) : QObject(parent)
{
}

bool LegacyMigrator::importLegacyProfile(const QString &legacyFilePath, const QString &saveAsPath)
{
    const QString localLegacyPath =
        QUrl(legacyFilePath).isLocalFile() ? QUrl(legacyFilePath).toLocalFile() : legacyFilePath;
    const QString localSavePath = QUrl(saveAsPath).isLocalFile() ? QUrl(saveAsPath).toLocalFile() : saveAsPath;

    const QJsonObject profile = LegacyProfileImporter::importFromXml(localLegacyPath);
    if (profile.contains(QLatin1String("error"))) {
        // LegacyProfileImporter::makeError() already logged the specifics.
        return false;
    }

    QFile file(localSavePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "LegacyMigrator: could not write" << localSavePath << ":" << file.errorString();
        return false;
    }
    file.write(QJsonDocument(profile).toJson(QJsonDocument::Indented));
    return true;
}
