#include "LegacyMigrator.h"

#include <QDebug>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QUrl>
#include <QXmlStreamReader>

#include "LegacyProfileImporter.h"
#include "R14ProfileImporter.h"

namespace {
/// Duck-typing signature (Fase R14): both profile variants declare the
/// identical `<profile version="16">` root, so the version number alone
/// can't route between them - see R14ProfileImporter's own class docs for
/// the full structural comparison. A real R14 export's top-level `<inputs>`
/// container has no equivalent in the classic format (bindings there live
/// nested directly under `<devices><device><mode><button>`) - checked
/// instead of "does this file have a <devices> tag", since an R14 export
/// ALSO has its own (structurally different) `<devices>` block, just a flat
/// `<device-id>`/`<device-name>` list further down the file rather than the
/// classic nested-bindings shape; using <devices> as the signature would
/// have misrouted every R14 profile straight into LegacyProfileImporter.
/// A cheap, separate top-level-only scan (skipCurrentElement() on every
/// child that isn't <inputs>) rather than something the real importer could
/// reuse mid-parse, since each importer opens the file itself from a path.
bool isR14Profile(const QString &localPath)
{
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false; // Let the real importer's own open() report the actual error.
    }

    QXmlStreamReader xml(&file);
    if (!xml.readNextStartElement() || xml.name() != QLatin1String("profile")) {
        return false;
    }

    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("inputs")) {
            return true;
        }
        xml.skipCurrentElement();
    }
    return false;
}
} // namespace

LegacyMigrator::LegacyMigrator(QObject *parent) : QObject(parent)
{
}

bool LegacyMigrator::importLegacyProfile(const QString &legacyFilePath, const QString &saveAsPath)
{
    const QString localLegacyPath =
        QUrl(legacyFilePath).isLocalFile() ? QUrl(legacyFilePath).toLocalFile() : legacyFilePath;
    const QString localSavePath = QUrl(saveAsPath).isLocalFile() ? QUrl(saveAsPath).toLocalFile() : saveAsPath;

    const QJsonObject profile = isR14Profile(localLegacyPath) ? R14ProfileImporter::importFromXml(localLegacyPath)
                                                                : LegacyProfileImporter::importFromXml(localLegacyPath);
    if (profile.contains(QLatin1String("error"))) {
        // The chosen importer's own makeError() already logged the specifics.
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
