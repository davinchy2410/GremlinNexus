#pragma once

#include <QObject>
#include <QString>

/**
 * @brief QML-facing "Import Legacy (XML)" entry point (Fase 15).
 *
 * Deliberately thin: LegacyProfileImporter (Phase 9-era) already implements
 * a real best-effort Grembling v1 profile-XML -> GremblingEx JSON
 * conversion (see its own class docs for exactly what it does/doesn't
 * translate - axis/button remaps and keyboard bindings, not hats/macros).
 * This class exists only to expose that existing conversion as a
 * Q_INVOKABLE QML can call and to write the result to saveAsPath, rather
 * than re-implementing XML parsing from scratch with a mock/TODO bindings
 * array.
 */
class LegacyMigrator : public QObject
{
    Q_OBJECT

public:
    explicit LegacyMigrator(QObject *parent = nullptr);

    /// Converts legacyFilePath (a local path or "file:///..." URL, as
    /// produced by QML's FileDialog.selectedFile) via
    /// LegacyProfileImporter::importFromXml(), then writes the resulting
    /// JSON to saveAsPath (same URL-or-local-path handling). Returns false
    /// (after logging why - see LegacyProfileImporter::makeError() for the
    /// parse-side cases, or this method's own qWarning() for a write
    /// failure) if the source XML can't be read/parsed or saveAsPath can't
    /// be written.
    Q_INVOKABLE bool importLegacyProfile(const QString &legacyFilePath, const QString &saveAsPath);
};
