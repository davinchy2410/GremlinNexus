#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class QXmlStreamReader;

/**
 * @brief Star Citizen integration engine (Fase SC-1/SC-2 of the Master
 *        Plan): detects a local Star Citizen install and reads the control
 *        scheme it exports as `actionmaps.xml`, translating the game's raw
 *        internal category/action identifiers into human-readable (Spanish)
 *        labels, and cross-references a Star Citizen input against a live
 *        GremblingEx profile to report which physical device/input actually
 *        drives it.
 *
 * A Meyer's-singleton QObject (Fase SC-3), the same pattern DeviceManager
 * already uses - exposed to QML as the "SCManager" root-context property
 * (see main.cpp), the same setContextProperty() convention every other
 * app-wide ViewModel/manager here uses (this project has no precedent for
 * qmlRegisterSingletonInstance()-style registration, so this follows the
 * established one instead of introducing a second, inconsistent mechanism).
 *
 * Uses QXmlStreamReader (forward-only streaming), the same
 * readNextStartElement()/skipCurrentElement() recursive-descent idiom
 * LegacyProfileImporter already uses for its own XML import, rather than a
 * DOM parser - actionmaps.xml can be sizable (every bindable action in the
 * game, whether the player touched it or not), so this keeps memory use
 * flat regardless of file size.
 */
class SCIntegrationManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList scBinds READ scBindsAsVariantList NOTIFY bindsChanged)

public:
    /// One parsed <rebind> entry from actionmaps.xml.
    struct SCActionBind
    {
        QString category; ///< Enclosing <actionmap name="..">, e.g. "spaceship_movement".
        QString actionId; ///< <action name="..">, e.g. "v_afterburner".
        QString input;    ///< <rebind input="..">, e.g. "js2_button5".
    };

    static SCIntegrationManager &instance();

    SCIntegrationManager(const SCIntegrationManager &) = delete;
    SCIntegrationManager &operator=(const SCIntegrationManager &) = delete;
    SCIntegrationManager(SCIntegrationManager &&) = delete;
    SCIntegrationManager &operator=(SCIntegrationManager &&) = delete;

    /// Best-effort guess at the local Star Citizen LIVE install's default
    /// user profile folder, following the standard RSI install layout.
    /// Returns an empty string if that path doesn't exist on this machine
    /// (a different install drive/edition, or the game isn't installed at all).
    QString detectInstallPath() const;

    /// Fase SC-3: best-effort guess at the folder actionmaps.xml exports
    /// actually land in - `LIVE\user\client\0\controls\mappings`, distinct
    /// from detectInstallPath()'s `Profiles\default` (an earlier, less
    /// precise guess from Fase SC-1). Returns an empty string if that folder
    /// doesn't exist on this machine.
    Q_INVOKABLE QString detectMappingsFolder() const;

    /// Every "*.xml" file directly inside folderPath (Fase SC-3) - a user's
    /// mappings folder typically holds several exports (one per in-game
    /// "Save" under a different name), so the caller picks which one to load
    /// rather than this guessing. Returns an empty list if folderPath
    /// doesn't exist or holds no .xml files.
    Q_INVOKABLE QStringList getAvailableXmlFiles(const QString &folderPath) const;

    /// Parses filePath (a Star Citizen-exported actionmaps.xml) and merges
    /// its <rebind> entries into actionBinds() (Fase SC-4): an action already
    /// present (matched by actionId - e.g. one of the master DB's seeded
    /// entries, see the constructor) has its input updated in place; an
    /// action not yet known is appended. Nothing is cleared out first, so a
    /// fresh export that only touches a few actions doesn't blank out
    /// everything else already known. Emits bindsChanged() on success so
    /// QML's "scBinds"-bound views refresh. Returns false if the file can't
    /// be opened or isn't well-formed XML - actionBinds() is left at its
    /// prior value in that case, rather than being modified from under a
    /// caller still reading it.
    Q_INVOKABLE bool loadProfile(const QString &filePath);

    /// Fase SC-4: writes filePath as a Star Citizen-compatible actionmaps.xml
    /// - the same format loadProfile() reads - built from the current
    /// actionBinds(). Actions whose input is still empty (never bound, or
    /// the master DB entry hasn't been assigned yet) are omitted entirely
    /// rather than written with a blank <rebind>, since Star Citizen treats
    /// a present-but-unassigned action the same way. Binds are grouped into
    /// one <actionmap> per distinct category. Returns false if filePath
    /// can't be opened for writing.
    Q_INVOKABLE bool exportProfile(const QString &filePath);

    /// Every SCActionBind read by the most recent successful loadProfile().
    const QList<SCActionBind> &actionBinds() const;

    /// scBinds' READ function (Fase SC-3): actionBinds() converted to a
    /// QML-friendly QVariantList of QVariantMap, one per SCActionBind, with
    /// keys "category"/"actionId"/"actionName" (translateName(actionId)) /
    /// "input" / "formattedInput" (Fase SC-5.2: formatExpectedVJoyInput(input),
    /// e.g. "vJoy 1 (Eje Y)") - QObject::property()-style maps are what a
    /// plain QML ListView delegate reads via "modelData.xxx", unlike the
    /// C++-only SCActionBind struct itself.
    QVariantList scBindsAsVariantList() const;

    /// Human-readable (Spanish) label for a raw category/action identifier
    /// (e.g. "v_afterburner" -> "Postquemador (Boost)") - falls back to the
    /// raw id itself if no translation is known yet, so an untranslated
    /// entry is still legible rather than silently blank.
    Q_INVOKABLE QString translateName(const QString &rawId) const;

    /// Fase SC-5.2: formats one Star Citizen <rebind input="..">/SCActionBind
    /// ::input value (the same "js<vJoyId>_<control>" shape
    /// resolvePhysicalSource() parses) into what the user actually needs to
    /// go bind in GremblingEx's own Profiles tab - e.g. "js1_y" ->
    /// "vJoy 1 (Eje Y)", "js2_button5" -> "vJoy 2 (Botón 5)" - independent of
    /// whether any physical device is currently driving it. Returns an empty
    /// string if scInput is empty or doesn't parse as "js<N>_<control>" at all.
    Q_INVOKABLE QString formatExpectedVJoyInput(const QString &scInput) const;

    /// Fase SC-2: cross-references one Star Citizen <rebind input="..">
    /// value (e.g. "js2_button5", "js1_rotx" - the "js<vJoyId>_<control>"
    /// shape every SCActionBind::input has) against gremblingProfile (a
    /// *live GremblingEx profile JSON*, the exact {"profileName",
    /// "bindings": [...]} shape ProfileManager::loadProfile()/
    /// serializeProfile() already use - see ProfileManager.h's own schema
    /// docs), to find which physical device/input is actually driving that
    /// vJoy target.
    ///
    /// Only matches a plain 1:1 "ButtonRemapHandler" (button) or
    /// "CurveHandler" (axis) binding, by "targetOutputId" plus
    /// "targetButton"/"targetAxis" - a target reached through a wrapper
    /// (SmoothingHandler's "wrappedAction", a Sequence/Condition/Tempo step,
    /// ...) is deliberately not unwrapped here (see class docs on why this
    /// phase keeps the cross-reference basic); such a binding is reported as
    /// no match (empty string) rather than guessed at.
    ///
    /// Returns "<friendly device name> (Botón N)" / "(Eje <name>)" (N/name
    /// both 1-based/human, per that physical device's own numbering - not
    /// Star Citizen's or vJoy's) for whichever binding matches, or an empty
    /// string if scInput doesn't parse as "js<N>_<control>", the control
    /// isn't a recognized button/axis shape, or no binding in
    /// gremblingProfile targets that exact vJoy output.
    QString resolvePhysicalSource(const QString &scInput, const QJsonObject &gremblingProfile) const;

    /// Fase SC-6: records that the game reads "js<jsIndex>" as vJoy device
    /// vJoyId - a user-editable override for parseScInput()'s vJoyId, needed
    /// because two vJoy devices share the same VID/PID and product string,
    /// making Windows' own enumeration order (and therefore which "jsN" the
    /// game assigns each one) unpredictable/unfixable from the XML alone.
    /// Emits bindsChanged() when the mapping actually changes, so scBinds'
    /// formattedInput/hardware-resolution both refresh against the new
    /// translation immediately.
    Q_INVOKABLE void setJsTranslation(int jsIndex, int vJoyId);

    /// Currently configured vJoy device for jsIndex, or jsIndex itself if no
    /// override has been set yet (Windows' enumeration order happening to
    /// match is as good a default guess as any).
    Q_INVOKABLE int getJsTranslation(int jsIndex) const;

signals:
    /// Emitted whenever loadProfile() succeeds - QML's "scBinds" binding
    /// re-reads scBindsAsVariantList() in response.
    void bindsChanged();

private:
    explicit SCIntegrationManager(QObject *parent = nullptr);

    /// Parses filePath into m_binds - loadProfile()'s own implementation,
    /// kept separate so the private parsing logic doesn't have to live
    /// behind a Q_INVOKABLE-friendly public signature.
    bool loadActionMaps(const QString &filePath);

    /// Parses a <actionmap name="..">...</actionmap> subtree, appending
    /// every <rebind> found under its <action> children onto outBinds.
    static void parseActionMap(QXmlStreamReader &xml, QList<SCActionBind> &outBinds);

    /// Parses a single <action name="..">...</action> subtree - one
    /// SCActionBind per non-empty <rebind input=".."> child (Star Citizen
    /// allows more than one input bound to the same action).
    static void parseAction(QXmlStreamReader &xml, const QString &category, QList<SCActionBind> &outBinds);

    /// A "js<vJoyId>_<control>" input string, broken down - shared by
    /// resolvePhysicalSource() and formatExpectedVJoyInput() so both read
    /// the exact same "js1_y"/"js2_button5" shape the same way rather than
    /// keeping two independent regexes/axis tables in sync by hand.
    struct ParsedScInput
    {
        bool valid = false; ///< False if scInput didn't parse as "js<N>_<control>" at all,
                             ///< or control wasn't a recognized button/axis shape (a hat/pov/
                             ///< keyboard/mouse input).
        int vJoyId = 0;
        bool isButton = false;
        /// 0-based vJoy index either way (GremblingEx's own targetButton/
        /// targetAxis numbering) - Star Citizen's own 1-based button count
        /// is a display-only concern callers add back in themselves.
        int index = -1;
    };
    ParsedScInput parseScInput(const QString &scInput) const;

    /// actionId/category -> Spanish label, seeded in the constructor.
    QHash<QString, QString> m_actionNames;

    /// Fase SC-6: js index (1-based, "jsN" from actionmaps.xml) -> vJoy
    /// device id the user has told us it actually corresponds to - see
    /// setJsTranslation()/getJsTranslation().
    QHash<int, int> m_jsTranslations;

    /// Fase SC-4/SC-5.3: seeded in the constructor with the ~15 most
    /// critical Star Citizen actions, each given a sensible default vJoy
    /// input, so a brand-new install with no prior actionmaps.xml export
    /// still has a populated, translated, ready-to-map scBinds list instead
    /// of one full of blanks - loadProfile()/exportProfile() both read and
    /// update this same master list rather than a freshly-parsed one.
    QList<SCActionBind> m_binds;
};
