#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>

class QXmlStreamReader;
class QJsonArray;

/**
 * @brief Best-effort importer for Joystick Gremlin profile XML files,
 *        converting them into the JSON schema ProfileManager::loadProfile()
 *        expects.
 *
 * Fase SC-7: the root element this validates is `<profile version="...">`,
 * Joystick Gremlin's own real export format - an earlier phase of this
 * importer assumed a `<devices version="1">` root instead (a hypothetical
 * "legacy Grembling v1" shape guessed at before any real sample file was
 * available); that guess never matched an actual exported profile, so this
 * phase replaces it outright rather than trying to support both roots.
 *
 * Uses QXmlStreamReader (forward-only streaming) rather than a DOM parser,
 * to keep memory use flat regardless of how large the source profile is.
 * Unknown/unsupported elements are skipped via
 * QXmlStreamReader::skipCurrentElement() rather than treated as errors, so
 * a profile using features this importer doesn't understand still yields
 * whatever it *can* understand instead of failing outright.
 *
 * Known, deliberate limitations of this best-effort translation:
 *  - Axis remaps (`<axis id=".."><remap axis=".." vjoy=".."/></axis>`) are
 *    imported as CurveHandler bindings; button remaps
 *    (`<button id=".."><remap button=".." vjoy=".."/></button>`) as
 *    ButtonRemapHandler bindings; and a button's keyboard-injection child
 *    (`<button id=".."><keyboard key=".."/></button>`, where "key" is
 *    already a hardware scan code, not a symbolic name) as a KeyboardHandler
 *    binding driven by SendInputKeyboardBackend. Hat remaps and `<macro>`
 *    blocks have no equivalent slot in the current JSON binding schema (or
 *    an equivalent complex enough that a best-effort line-for-line XML
 *    translation isn't a faithful one) and are therefore still skipped.
 *  - The legacy per-device identifier (a DirectInput-style numeric id /
 *    windows_id pair) has no reliable mapping to the RawInput HID device
 *    path (systemPath) the current engine uses, so every imported binding's
 *    "sourceDevice" is instead a synthetic "[Legacy] <device name>" string
 *    (Fase SC-7.3) - distinct per <device name="..">, but never a real
 *    systemPath - rather than guessing a mapping that could silently bind
 *    the wrong hardware. ProfileEditorViewModel synthesizes an offline
 *    placeholder device entry for each such string so the user can Swap
 *    Devices it onto their real hardware afterward.
 *  - The profile's own `<axis>`/`<button>` remaps carry no deadzone/curve
 *    data - that instead lives separately under `<vjoy-devices>` (Fase SC-7,
 *    see parseVjoyDevices()) and gets merged into the matching CurveHandler
 *    binding after both sections are read. A vJoy axis with no
 *    `<response-curve>` there still falls back to CurveHandler's own safe
 *    defaults.
 *  - Only one mode is imported per device — "Global" if present, otherwise
 *    whichever mode is encountered first — since the current engine has no
 *    concept of modes yet; importing every mode's bindings into one flat
 *    routing table would silently mix mutually-exclusive configurations.
 */
class LegacyProfileImporter
{
public:
    /// Parses xmlFilePath and returns a JSON object matching
    /// ProfileManager's expected schema ({"profileName", "bindings": [...]}).
    /// On failure (file unreadable, malformed XML, or an unrecognized root
    /// element) returns a JSON object with a single "error" string property
    /// describing what went wrong, instead of throwing or crashing.
    static QJsonObject importFromXml(const QString &xmlFilePath);

private:
    /// Parses a <device>...</device> subtree, appending whichever mode's
    /// axis bindings were chosen (see class docs) onto outBindings. deviceName
    /// (Fase SC-7.3: the <device>'s own "name" attribute) is threaded down
    /// into every binding's "sourceDevice" as "[Legacy] <deviceName>" - see
    /// parseAxis()/parseButton() - so bindings imported from different
    /// physical devices stay distinguishable instead of collapsing onto the
    /// single wildcard source device every prior phase used.
    static void parseDevice(QXmlStreamReader &xml, QJsonArray &outBindings);

    /// Parses a <mode>...</mode> subtree into its axis/button bindings.
    static QJsonArray parseMode(QXmlStreamReader &xml, const QString &deviceName);

    /// Parses a single <axis id=".."> element. Returns an empty QJsonObject
    /// if the axis has no (or a malformed) <remap> child, i.e. it was never
    /// bound in the legacy profile.
    static QJsonObject parseAxis(QXmlStreamReader &xml, const QString &deviceName);

    /// Parses a single <button id=".."> element. Returns an empty
    /// QJsonObject if the button has neither a <remap> nor a <keyboard>
    /// child, i.e. it was never bound in the legacy profile. A <remap>
    /// child takes precedence over a <keyboard> child if (unusually) both
    /// are present, since a button can drive one handler, not two.
    static QJsonObject parseButton(QXmlStreamReader &xml, const QString &deviceName);

    /// Fase SC-7: parses a <vjoy-devices>...</vjoy-devices> subtree.
    /// Joystick Gremlin's own <vjoy-device> elements carry no reliable
    /// "this is vJoy 1" id of their own, so the first one encountered is
    /// assumed to be vJoy 1, the second vJoy 2, and so on. Populates
    /// outCurveDataByVjoy[vJoyId][targetAxis (0-based)] with whatever
    /// parseVjoyAxisCurve() found for each <axis id=".."> that actually had
    /// a <response-curve> - an axis with none simply has no entry.
    static void parseVjoyDevices(QXmlStreamReader &xml, QMap<int, QMap<int, QJsonObject>> &outCurveDataByVjoy);

    /// Parses a single <axis id=".."> element under <vjoy-devices> (distinct
    /// from parseAxis() above, which parses a <devices> axis's <remap>
    /// instead). Returns an empty QJsonObject if the axis has no
    /// <response-curve> child; otherwise returns an object with "deadzone"
    /// (double - the larger-magnitude of <deadzone>'s center-high/center-low)
    /// and/or "curvePoints" (a QJsonArray of {"x","y"} objects from a
    /// <mapping type="cubic-spline">'s <control-point> children), whichever
    /// of the two were actually present.
    static QJsonObject parseVjoyAxisCurve(QXmlStreamReader &xml);

    static QJsonObject makeError(const QString &message);
};
