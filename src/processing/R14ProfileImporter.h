#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

class QXmlStreamReader;
class QJsonArray;

/**
 * @brief Best-effort importer for the "R14" Joystick Gremlin profile
 *        variant - a graph-of-UUIDs action tree, structurally unrelated to
 *        the classic `<devices><device><mode><button>` shape
 *        LegacyProfileImporter already handles despite both declaring the
 *        same `<profile version="16">` root (see LegacyMigrator's own
 *        docs for the `<inputs>` tag signature this distinguishes on).
 *
 * A real R14 export ("JG[R14].xml", used to build this importer) is
 * structured as five independent top-level sections under `<profile>`:
 *  - `<inputs>`: one `<input>` per physical control bound to *something*
 *    (device-id, input-type [button/axis/hat], mode, input-id, and an
 *    `<action-configuration><root-action>` UUID pointing into `<library>`).
 *  - `<library>`: every `<action id=".." type="..">` node in the profile,
 *    keyed by its own UUID - the actual graph. A "root" node is a
 *    transparent wrapper (its own `<actions><action-id>` children are the
 *    *real* action(s) - exactly one for a button, two for an axis: a
 *    "map-to-vjoy" plus a parallel "response-curve" sibling transforming the
 *    same physical axis before it reaches vjoy). A "tempo" node is a tap/
 *    hold split ("short-actions"/"long-actions", each a list of further
 *    action-ids) - this engine's own TempoHandler binding shape already
 *    matches it almost exactly. "map-to-vjoy" and "change-mode" are the
 *    actual leaves this importer knows how to translate.
 *  - `<modes>`: `<mode parent="X">Y</mode>` parent-child pairs - the real
 *    mode-inheritance tree, straight into this profile's own "modeHierarchy"
 *    JSON (see ProfileManager::loadProfile()).
 *  - `<devices>`: `<device-id>` UUID -> `<device-name>` pairs, NOT a
 *    Windows RawInput systemPath - same "no reliable mapping to real
 *    hardware" situation LegacyProfileImporter documents, so every binding's
 *    "sourceDevice" is likewise a synthetic "[Legacy] <device name>" string
 *    (ProfileEditorViewModel already synthesizes an offline placeholder for
 *    any such string, so "Swap Devices" onto real hardware works
 *    identically to a classic import - no separate UI work needed).
 *  - `<scripts>`/`<settings>`/`<logical-device>`: no equivalent in the
 *    current JSON schema, skipped entirely.
 *
 * Because `<inputs>` appears *before* `<library>`/`<modes>`/`<devices>` in
 * file order and QXmlStreamReader is forward-only, this can't resolve a
 * binding while still reading `<inputs>` - the whole file is read once into
 * plain in-memory structures (rawInputs/actionsById/modeParents/deviceNames)
 * first, and only then is every `<input>`'s root-action graph walked and
 * translated, mirroring LegacyProfileImporter's own two-pass
 * `<devices>`+`<vjoy-devices>` merge for the same forward-only-reader
 * reason.
 *
 * Known, deliberate limitations of this best-effort translation (same
 * "skip what isn't understood instead of guessing" philosophy as
 * LegacyProfileImporter):
 *  - Hat inputs (`<input-type>hat</input-type>`) are always skipped - R14's
 *    own per-hat-direction input-id numbering has no reliable mapping onto
 *    this engine's synthetic per-hat-direction button indices, the same
 *    limitation LegacyProfileImporter documents for classic `<hat>` remaps.
 *  - `<action type="change-mode">` with `change-type` "Cycle"/"Previous"
 *    (a permanent switch, potentially stepping through *several* modes in
 *    sequence on repeated presses) is imported as a plain permanent
 *    ModeSwitch to whichever single `<target-mode>` the node lists - every
 *    sample this importer was built against only ever names one, making
 *    that the closest faithful translation; only "Temporary" maps to
 *    TemporaryModeSwitch's hold-to-shift/revert-on-release behavior.
 *  - "macro", "map-to-mouse", "key", "pause", and any other action type
 *    with no equivalent in the current JSON binding schema are skipped, same
 *    as LegacyProfileImporter's own unsupported cases.
 */
class R14ProfileImporter
{
public:
    /// Parses xmlFilePath (a real `<inputs>`-based R14 profile) and returns
    /// a JSON object matching ProfileManager's expected schema
    /// ({"profileName", "bindings": [...], "modeHierarchy": {...}}). On
    /// failure (file unreadable, malformed XML, or no `<profile>` root)
    /// returns a JSON object with a single "error" string property, same
    /// convention as LegacyProfileImporter::importFromXml().
    static QJsonObject importFromXml(const QString &xmlFilePath);

private:
    /// One `<action id=".." type="..">` node from `<library>`, decoded into
    /// a plain in-memory shape resolveAction() can walk without touching the
    /// XML reader again - see class docs for what each field maps to.
    struct ActionNode
    {
        QString type;
        QStringList childActionIds;  ///< <actions><action-id>... (root nodes).
        QStringList shortActionIds;  ///< <short-actions><action-id>... (tempo).
        QStringList longActionIds;   ///< <long-actions><action-id>... (tempo).
        QHash<QString, QString> properties; ///< Flat <property><name>/<value> children, by name.
        QString targetModeName;      ///< <target-mode>'s own "name" property (change-mode).

        bool hasDeadzone = false;
        double deadzoneCenterHigh = 0.0;
        double deadzoneCenterLow = 0.0;
        QVector<QPair<double, double>> curvePoints; ///< <control-points>, in <x, y> order (response-curve).
    };

    /// One `<input>` element from `<inputs>`, decoded into a plain
    /// in-memory shape - see class docs for the two-pass reasoning.
    struct RawInput
    {
        QString deviceId;
        QString inputType; ///< "button", "axis", or "hat".
        QString mode;
        int inputId = 0; ///< 1-based, matching R14's own convention.
        QString rootActionId;
    };

    static void parseInputs(QXmlStreamReader &xml, QList<RawInput> &outInputs);
    static void parseLibrary(QXmlStreamReader &xml, QHash<QString, ActionNode> &outActions);
    static ActionNode parseActionNode(QXmlStreamReader &xml);
    static void parseModes(QXmlStreamReader &xml, QHash<QString, QString> &outModeParents);
    static void parseDevices(QXmlStreamReader &xml, QHash<QString, QString> &outDeviceNames);

    /// Reads a <property type="..">...</property> element (already
    /// positioned at its start tag) and returns its <name>/<value> pair.
    static QPair<QString, QString> parseProperty(QXmlStreamReader &xml);

    /// Extracts response-curve node's deadzone/curvePoints into the same
    /// CurveHandler "parameters" shape LegacyProfileImporter's own
    /// parseVjoyAxisCurve() produces - deadzone is the larger-magnitude of
    /// center-high/center-low, matching that method's own convention.
    static QJsonObject extractCurveData(const ActionNode &responseCurveNode);

    /// Recursively walks actionId's node (and, for "root"/"tempo" nodes, its
    /// children) into a binding JSON object with no source fields set yet
    /// ("actionType"/target*/"parameters"/"shortActions"/"longActions" only)
    /// - the caller (importFromXml()) fills in "sourceDevice"/"sourceButton"
    /// or "sourceAxis"/"mode" once for whichever `<input>` this was resolved
    /// from. Returns an empty QJsonObject for an unsupported/unresolvable
    /// action type, a missing id, or once depth exceeds a small guard limit
    /// (a corrupted/hand-edited profile describing a reference cycle must
    /// not hang the import).
    static QJsonObject resolveAction(const QString &actionId, const QHash<QString, ActionNode> &actionsById,
                                      int depth);

    static QJsonObject makeError(const QString &message);
};
