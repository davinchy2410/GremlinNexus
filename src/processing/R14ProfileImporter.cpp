#include "R14ProfileImporter.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonValue>
#include <QXmlStreamReader>

namespace {
/// Reference-cycle guard for resolveAction()'s recursion - a corrupted/
/// hand-edited profile describing e.g. two roots pointing at each other must
/// not hang the import. No real R14 export nests anywhere close to this
/// deep (root -> tempo -> leaf is the deepest shape this importer was built
/// against).
constexpr int kMaxResolveDepth = 32;
}

QJsonObject R14ProfileImporter::importFromXml(const QString &xmlFilePath, QStringList *outRootModes)
{
    QFile file(xmlFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return makeError(QStringLiteral("could not open \"%1\": %2").arg(xmlFilePath, file.errorString()));
    }

    QXmlStreamReader xml(&file);
    if (!xml.readNextStartElement() || xml.name() != QLatin1String("profile")) {
        return makeError(
            QStringLiteral("\"%1\" has no <profile> root element (not a Joystick Gremlin profile)").arg(xmlFilePath));
    }

    // <inputs> is read (and fully resolved) before <library>/<modes>/
    // <devices> even exist in memory, since it appears first in file order
    // and QXmlStreamReader can't rewind - see class docs for why this is a
    // two-pass import (collect everything, THEN resolve) rather than a
    // single streaming pass like LegacyProfileImporter's own <devices> walk.
    QList<RawInput> rawInputs;
    QHash<QString, ActionNode> actionsById;
    QHash<QString, QString> modeParents;
    QHash<QString, QString> deviceNames;

    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("inputs")) {
            parseInputs(xml, rawInputs);
        } else if (xml.name() == QLatin1String("library")) {
            parseLibrary(xml, actionsById);
        } else if (xml.name() == QLatin1String("modes")) {
            parseModes(xml, modeParents);
        } else if (xml.name() == QLatin1String("devices")) {
            parseDevices(xml, deviceNames);
        } else {
            // <settings>, <logical-device>, <scripts>, or anything else this
            // importer doesn't know about.
            xml.skipCurrentElement();
        }
    }

    if (xml.hasError()) {
        return makeError(QStringLiteral("XML error in \"%1\" at line %2, column %3: %4")
                              .arg(xmlFilePath)
                              .arg(xml.lineNumber())
                              .arg(xml.columnNumber())
                              .arg(xml.errorString()));
    }

    // See this method's own header docs: a "root" mode here is any mode name
    // an <input> references that never appears as a KEY in modeParents (i.e.
    // <modes> never declared a parent for it) - the same definition
    // ProfileManager::loadProfile()'s backward-compat fallback uses when
    // defaulting a parent-less mode's hierarchy parent to Global.
    if (outRootModes) {
        outRootModes->clear();
        for (const RawInput &input : rawInputs) {
            if (input.mode.isEmpty() || input.mode == QLatin1String("Global")) {
                continue;
            }
            if (modeParents.contains(input.mode)) {
                continue; // Has a known parent elsewhere in <modes> - not a root.
            }
            if (!outRootModes->contains(input.mode)) {
                outRootModes->append(input.mode);
            }
        }
    }

    QJsonArray bindings;
    for (const RawInput &input : rawInputs) {
        // Fase R14: hats are always skipped, whatever their root-action
        // resolves to - see class docs for why there's no reliable mapping
        // from R14's own per-hat-direction input-id numbering onto this
        // engine's synthetic per-hat-direction button indices.
        if (input.inputType != QLatin1String("button") && input.inputType != QLatin1String("axis")) {
            continue;
        }

        QJsonObject resolved = resolveAction(input.rootActionId, actionsById, 0);
        if (resolved.isEmpty()) {
            continue; // Unbound, or a graph shape this importer doesn't understand yet.
        }

        resolved[QLatin1String("sourceDevice")] =
            QStringLiteral("[Legacy] ") + deviceNames.value(input.deviceId, input.deviceId);
        resolved[QLatin1String("mode")] = input.mode;
        if (input.inputType == QLatin1String("axis")) {
            resolved[QLatin1String("sourceAxis")] = input.inputId - 1;
        } else {
            resolved[QLatin1String("sourceButton")] = input.inputId - 1;
        }
        bindings.append(resolved);
    }

    QJsonObject profile;
    profile[QLatin1String("profileName")] =
        QStringLiteral("Imported: %1").arg(QFileInfo(xmlFilePath).completeBaseName());
    profile[QLatin1String("bindings")] = bindings;

    if (!modeParents.isEmpty()) {
        QJsonObject modeHierarchy;
        for (auto it = modeParents.constBegin(); it != modeParents.constEnd(); ++it) {
            modeHierarchy[it.key()] = it.value();
        }
        profile[QLatin1String("modeHierarchy")] = modeHierarchy;
    }

    return profile;
}

void R14ProfileImporter::parseInputs(QXmlStreamReader &xml, QList<RawInput> &outInputs)
{
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("input")) {
            xml.skipCurrentElement();
            continue;
        }

        RawInput input;
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("device-id")) {
                input.deviceId = xml.readElementText();
            } else if (xml.name() == QLatin1String("input-type")) {
                input.inputType = xml.readElementText();
            } else if (xml.name() == QLatin1String("mode")) {
                input.mode = xml.readElementText();
            } else if (xml.name() == QLatin1String("input-id")) {
                input.inputId = xml.readElementText().toInt();
            } else if (xml.name() == QLatin1String("action-configuration")) {
                while (xml.readNextStartElement()) {
                    if (xml.name() == QLatin1String("root-action")) {
                        input.rootActionId = xml.readElementText();
                    } else {
                        xml.skipCurrentElement(); // <behavior> - redundant with input-type.
                    }
                }
            } else {
                xml.skipCurrentElement();
            }
        }

        if (!input.deviceId.isEmpty() && !input.rootActionId.isEmpty() && input.inputId >= 1) {
            outInputs.append(input);
        }
    }
}

void R14ProfileImporter::parseLibrary(QXmlStreamReader &xml, QHash<QString, ActionNode> &outActions)
{
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("action")) {
            xml.skipCurrentElement();
            continue;
        }
        const QString id = xml.attributes().value(QLatin1String("id")).toString();
        ActionNode node = parseActionNode(xml);
        if (!id.isEmpty()) {
            outActions.insert(id, std::move(node));
        }
    }
}

R14ProfileImporter::ActionNode R14ProfileImporter::parseActionNode(QXmlStreamReader &xml)
{
    ActionNode node;
    node.type = xml.attributes().value(QLatin1String("type")).toString();

    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("actions")) {
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("action-id")) {
                    node.childActionIds.append(xml.readElementText());
                } else {
                    xml.skipCurrentElement();
                }
            }
        } else if (xml.name() == QLatin1String("short-actions")) {
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("action-id")) {
                    node.shortActionIds.append(xml.readElementText());
                } else {
                    xml.skipCurrentElement();
                }
            }
        } else if (xml.name() == QLatin1String("long-actions")) {
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("action-id")) {
                    node.longActionIds.append(xml.readElementText());
                } else {
                    xml.skipCurrentElement();
                }
            }
        } else if (xml.name() == QLatin1String("property")) {
            const QPair<QString, QString> nameValue = parseProperty(xml);
            if (!nameValue.first.isEmpty()) {
                node.properties.insert(nameValue.first, nameValue.second);
            }
        } else if (xml.name() == QLatin1String("target-mode")) {
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("property")) {
                    const QPair<QString, QString> nameValue = parseProperty(xml);
                    if (nameValue.first == QLatin1String("name")) {
                        node.targetModeName = nameValue.second;
                    }
                } else {
                    xml.skipCurrentElement();
                }
            }
        } else if (xml.name() == QLatin1String("deadzone")) {
            // response-curve's own deadzone - center-high/center-low are the
            // pair LegacyProfileImporter's parseVjoyAxisCurve() already uses
            // (the "dead" band around rest position); low/high (the curve's
            // outer clamp points) have no equivalent slot in this engine's
            // CurveHandler parameters and are intentionally not captured.
            node.hasDeadzone = true;
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("property")) {
                    const QPair<QString, QString> nameValue = parseProperty(xml);
                    if (nameValue.first == QLatin1String("center-high")) {
                        node.deadzoneCenterHigh = nameValue.second.toDouble();
                    } else if (nameValue.first == QLatin1String("center-low")) {
                        node.deadzoneCenterLow = nameValue.second.toDouble();
                    }
                } else {
                    xml.skipCurrentElement();
                }
            }
        } else if (xml.name() == QLatin1String("control-points")) {
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("property")) {
                    const QPair<QString, QString> nameValue = parseProperty(xml);
                    // Fase R14: a control-point's own value is "x,y" (one
                    // string, comma-separated) rather than two separate
                    // properties - split it directly instead of a third
                    // <name>/<value> pair like every other property.
                    const QStringList parts = nameValue.second.split(QLatin1Char(','));
                    if (nameValue.first == QLatin1String("point") && parts.size() == 2) {
                        node.curvePoints.append(qMakePair(parts.at(0).toDouble(), parts.at(1).toDouble()));
                    }
                } else {
                    xml.skipCurrentElement();
                }
            }
        } else {
            // <macro-action> (inside a <macro> action) or anything else
            // this importer doesn't understand - best-effort skip.
            xml.skipCurrentElement();
        }
    }

    return node;
}

QPair<QString, QString> R14ProfileImporter::parseProperty(QXmlStreamReader &xml)
{
    QString name;
    QString value;
    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("name")) {
            name = xml.readElementText();
        } else if (xml.name() == QLatin1String("value")) {
            value = xml.readElementText();
        } else {
            xml.skipCurrentElement();
        }
    }
    return qMakePair(name, value);
}

void R14ProfileImporter::parseModes(QXmlStreamReader &xml, QHash<QString, QString> &outModeParents)
{
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("mode")) {
            xml.skipCurrentElement();
            continue;
        }
        const QString parent = xml.attributes().value(QLatin1String("parent")).toString();
        const QString name = xml.readElementText();
        if (!name.isEmpty() && !parent.isEmpty()) {
            outModeParents.insert(name, parent);
        }
    }
}

void R14ProfileImporter::parseDevices(QXmlStreamReader &xml, QHash<QString, QString> &outDeviceNames)
{
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("device")) {
            xml.skipCurrentElement();
            continue;
        }
        QString deviceId;
        QString deviceName;
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("device-id")) {
                deviceId = xml.readElementText();
            } else if (xml.name() == QLatin1String("device-name")) {
                deviceName = xml.readElementText().trimmed();
            } else {
                xml.skipCurrentElement();
            }
        }
        if (!deviceId.isEmpty() && !deviceName.isEmpty()) {
            outDeviceNames.insert(deviceId, deviceName);
        }
    }
}

QJsonObject R14ProfileImporter::extractCurveData(const ActionNode &responseCurveNode)
{
    QJsonObject result;
    if (responseCurveNode.hasDeadzone) {
        result[QLatin1String("deadzone")] =
            qMax(qAbs(responseCurveNode.deadzoneCenterHigh), qAbs(responseCurveNode.deadzoneCenterLow));
    }
    if (!responseCurveNode.curvePoints.isEmpty()) {
        QJsonArray points;
        for (const QPair<double, double> &p : responseCurveNode.curvePoints) {
            QJsonObject point;
            point[QLatin1String("x")] = p.first;
            point[QLatin1String("y")] = p.second;
            points.append(point);
        }
        result[QLatin1String("curvePoints")] = points;
    }
    return result;
}

QJsonObject R14ProfileImporter::resolveAction(const QString &actionId, const QHash<QString, ActionNode> &actionsById,
                                               int depth)
{
    if (actionId.isEmpty() || depth > kMaxResolveDepth) {
        return QJsonObject();
    }
    const auto it = actionsById.constFind(actionId);
    if (it == actionsById.constEnd()) {
        return QJsonObject();
    }
    const ActionNode &node = it.value();

    if (node.type == QLatin1String("root")) {
        // Transparent wrapper - see class docs. An axis root's two children
        // are a "map-to-vjoy" (the real binding) and a parallel
        // "response-curve" sibling (deadzone/curve data merged onto that
        // binding's "parameters" afterward, same shape
        // LegacyProfileImporter's own vjoy-devices merge produces); a
        // button root has exactly one child, which is used directly.
        QJsonObject resolved;
        QJsonObject curveData;
        for (const QString &childId : node.childActionIds) {
            const auto childIt = actionsById.constFind(childId);
            if (childIt != actionsById.constEnd() && childIt.value().type == QLatin1String("response-curve")) {
                curveData = extractCurveData(childIt.value());
                continue;
            }
            if (resolved.isEmpty()) {
                resolved = resolveAction(childId, actionsById, depth + 1);
            }
        }
        if (!resolved.isEmpty() && !curveData.isEmpty() &&
            resolved.value(QLatin1String("actionType")).toString() == QLatin1String("CurveHandler")) {
            QJsonObject parameters = resolved.value(QLatin1String("parameters")).toObject();
            if (curveData.contains(QLatin1String("deadzone"))) {
                parameters[QLatin1String("deadzone")] = curveData.value(QLatin1String("deadzone"));
            }
            if (curveData.contains(QLatin1String("curvePoints"))) {
                parameters[QLatin1String("curvePoints")] = curveData.value(QLatin1String("curvePoints"));
            }
            resolved[QLatin1String("parameters")] = parameters;
        }
        return resolved;
    }

    if (node.type == QLatin1String("tempo")) {
        // R14's own tap/hold split - matches this engine's TempoHandler
        // binding shape (see ProfileManager's schema) almost exactly:
        // shortActions/longActions are lists of further resolved action
        // objects, not leaf bindings themselves.
        QJsonArray shortActions;
        for (const QString &id : node.shortActionIds) {
            const QJsonObject child = resolveAction(id, actionsById, depth + 1);
            if (!child.isEmpty()) {
                shortActions.append(child);
            }
        }
        QJsonArray longActions;
        for (const QString &id : node.longActionIds) {
            const QJsonObject child = resolveAction(id, actionsById, depth + 1);
            if (!child.isEmpty()) {
                longActions.append(child);
            }
        }
        if (shortActions.isEmpty() && longActions.isEmpty()) {
            return QJsonObject();
        }

        QJsonObject parameters;
        bool thresholdOk = false;
        const double thresholdSeconds = node.properties.value(QStringLiteral("threshold")).toDouble(&thresholdOk);
        parameters[QLatin1String("longPressMs")] = qRound((thresholdOk ? thresholdSeconds : 0.5) * 1000.0);
        // R14's tempo has no double-tap concept of its own - matches
        // ProfileManager's own TempoHandler default.
        parameters[QLatin1String("doubleTapMs")] = 250;
        parameters[QLatin1String("pulseDurationMs")] = 50;

        QJsonObject binding;
        binding[QLatin1String("actionType")] = QStringLiteral("TempoHandler");
        binding[QLatin1String("shortActions")] = shortActions;
        binding[QLatin1String("longActions")] = longActions;
        binding[QLatin1String("parameters")] = parameters;
        return binding;
    }

    if (node.type == QLatin1String("map-to-vjoy")) {
        bool vjoyIdOk = false;
        bool inputIdOk = false;
        const int vjoyDeviceId = node.properties.value(QStringLiteral("vjoy-device-id")).toInt(&vjoyIdOk);
        const int vjoyInputId = node.properties.value(QStringLiteral("vjoy-input-id")).toInt(&inputIdOk);
        const QString vjoyInputType = node.properties.value(QStringLiteral("vjoy-input-type"));

        if (!vjoyIdOk || !inputIdOk || vjoyDeviceId < 1 || vjoyDeviceId > 16 || vjoyInputId < 1) {
            return QJsonObject();
        }

        QJsonObject binding;
        if (vjoyInputType == QLatin1String("button")) {
            binding[QLatin1String("actionType")] = QStringLiteral("ButtonRemapHandler");
            binding[QLatin1String("targetButton")] = vjoyInputId - 1;
            binding[QLatin1String("targetOutputId")] = vjoyDeviceId;
        } else if (vjoyInputType == QLatin1String("axis")) {
            binding[QLatin1String("actionType")] = QStringLiteral("CurveHandler");
            binding[QLatin1String("targetAxis")] = vjoyInputId - 1;
            binding[QLatin1String("targetOutputId")] = vjoyDeviceId;

            // No <response-curve> sibling was found (or this map-to-vjoy was
            // reached some other way) - same safe defaults
            // LegacyProfileImporter::parseAxis() falls back to.
            QJsonObject parameters;
            parameters[QLatin1String("deadzone")] = 0.05;
            parameters[QLatin1String("sensitivity")] = 1.0;
            parameters[QLatin1String("inputMin")] = 0;
            parameters[QLatin1String("inputMax")] = 65535;
            parameters[QLatin1String("outputMin")] = 0;
            parameters[QLatin1String("outputMax")] = 32767;
            binding[QLatin1String("parameters")] = parameters;
        } else {
            // "hat": see class docs - no reliable mapping, best-effort skip.
            return QJsonObject();
        }
        return binding;
    }

    if (node.type == QLatin1String("change-mode")) {
        if (node.targetModeName.isEmpty()) {
            return QJsonObject();
        }
        QJsonObject parameters;
        parameters[QLatin1String("targetMode")] = node.targetModeName;

        QJsonObject binding;
        const QString changeType = node.properties.value(QStringLiteral("change-type"));
        // "Temporary" = hold-to-shift, matches TemporaryModeSwitchHandler's
        // revert-on-release behavior; "Cycle"/"Previous" (or anything else)
        // = a permanent switch - see class docs for the "Cycle" caveat.
        binding[QLatin1String("actionType")] =
            changeType == QLatin1String("Temporary") ? QStringLiteral("TemporaryModeSwitch") : QStringLiteral("ModeSwitch");
        binding[QLatin1String("parameters")] = parameters;
        return binding;
    }

    // response-curve reached directly (not as a root's sibling - no
    // standalone binding equivalent), macro, map-to-mouse, key, pause, or
    // any other action type this importer doesn't understand yet.
    return QJsonObject();
}

QJsonObject R14ProfileImporter::makeError(const QString &message)
{
    qWarning() << "R14ProfileImporter:" << message;
    QJsonObject error;
    error[QLatin1String("error")] = message;
    return error;
}
