#include "LegacyProfileImporter.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>

QJsonObject LegacyProfileImporter::importFromXml(const QString &xmlFilePath)
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

    QJsonArray bindings;
    QMap<int, QMap<int, QJsonObject>> curveDataByVjoy;
    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("devices")) {
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("device")) {
                    parseDevice(xml, bindings);
                } else {
                    xml.skipCurrentElement();
                }
            }
        } else if (xml.name() == QLatin1String("vjoy-devices")) {
            parseVjoyDevices(xml, curveDataByVjoy);
        } else {
            // <settings>, <options>, or anything else this importer doesn't
            // know about.
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

    // Fase SC-7: <devices> and <vjoy-devices> are two independent sibling
    // sections read above in whatever order the file happens to list them
    // in - only now that both are fully read can each CurveHandler binding
    // actually be matched up against the vjoy-devices curve/deadzone data
    // for its own (targetOutputId, targetAxis).
    if (!curveDataByVjoy.isEmpty()) {
        for (int i = 0; i < bindings.size(); ++i) {
            QJsonObject binding = bindings[i].toObject();
            if (binding.value(QLatin1String("actionType")).toString() != QLatin1String("CurveHandler")) {
                continue;
            }

            const int vJoyId = binding.value(QLatin1String("targetOutputId")).toInt();
            const int targetAxis = binding.value(QLatin1String("targetAxis")).toInt();
            const auto vjoyIt = curveDataByVjoy.constFind(vJoyId);
            if (vjoyIt == curveDataByVjoy.constEnd()) {
                continue;
            }
            const auto axisIt = vjoyIt->constFind(targetAxis);
            if (axisIt == vjoyIt->constEnd()) {
                continue;
            }

            QJsonObject parameters = binding.value(QLatin1String("parameters")).toObject();
            const QJsonObject &curveData = axisIt.value();
            if (curveData.contains(QLatin1String("deadzone"))) {
                parameters[QLatin1String("deadzone")] = curveData.value(QLatin1String("deadzone"));
            }
            if (curveData.contains(QLatin1String("curvePoints"))) {
                parameters[QLatin1String("curvePoints")] = curveData.value(QLatin1String("curvePoints"));
            }
            binding[QLatin1String("parameters")] = parameters;
            bindings[i] = binding;
        }
    }

    QJsonObject profile;
    profile[QLatin1String("profileName")] =
        QStringLiteral("Imported: %1").arg(QFileInfo(xmlFilePath).completeBaseName());
    profile[QLatin1String("bindings")] = bindings;
    return profile;
}

void LegacyProfileImporter::parseDevice(QXmlStreamReader &xml, QJsonArray &outBindings)
{
    // Fase SC-7.3: the physical device's own name, threaded down into every
    // binding this <device> produces (see parseAxis()/parseButton()) so
    // imports from several distinct devices don't all collapse onto one
    // indistinguishable wildcard source.
    const QString deviceName = xml.attributes().value(QLatin1String("name")).toString();

    QHash<QString, QJsonArray> bindingsByMode;
    QString firstModeName;

    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("mode")) {
            const QString modeName = xml.attributes().value(QLatin1String("name")).toString();
            const QJsonArray modeBindings = parseMode(xml, deviceName);
            if (firstModeName.isEmpty()) {
                firstModeName = modeName;
            }
            bindingsByMode.insert(modeName, modeBindings);
        } else {
            // <key> (the keyboard pseudo-device's own scan-code-keyed
            // mode-switch bindings, see class docs) or anything else: still
            // has no equivalent in the current JSON binding schema.
            xml.skipCurrentElement();
        }
    }

    const QString chosenMode =
        bindingsByMode.contains(QLatin1String("Global")) ? QStringLiteral("Global") : firstModeName;
    for (const QJsonValue &binding : bindingsByMode.value(chosenMode)) {
        outBindings.append(binding);
    }
}

QJsonArray LegacyProfileImporter::parseMode(QXmlStreamReader &xml, const QString &deviceName)
{
    QJsonArray bindings;

    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("axis")) {
            const QJsonObject binding = parseAxis(xml, deviceName);
            if (!binding.isEmpty()) {
                bindings.append(binding);
            }
        } else if (xml.name() == QLatin1String("button")) {
            const QJsonObject binding = parseButton(xml, deviceName);
            if (!binding.isEmpty()) {
                bindings.append(binding);
            }
        } else {
            // hat/... : the current JSON binding schema has no equivalent
            // slot for these yet - best-effort ignore rather than guess at
            // an unsupported mapping.
            xml.skipCurrentElement();
        }
    }

    return bindings;
}

QJsonObject LegacyProfileImporter::parseAxis(QXmlStreamReader &xml, const QString &deviceName)
{
    bool axisIdOk = false;
    const int legacyAxisId = xml.attributes().value(QLatin1String("id")).toString().toInt(&axisIdOk);

    // Fase SC-7.1: a real Joystick Gremlin export never puts <remap>
    // directly under <axis> - it's always nested a couple of levels down,
    // inside a <container type="basic"><action-set> wrapper (presumably
    // support for stacking multiple actions on one physical input, unused
    // by a plain remap). readNextStartElement()/skipCurrentElement() only
    // ever look at *direct* children, so that wrapper made every <remap>
    // invisible to this method and the importer silently produced zero
    // bindings. Scanning token-by-token with readNext() instead - matching
    // on isStartElement()/isEndElement() rather than element depth - finds
    // <remap> at whatever depth it's actually nested at.
    QJsonObject binding;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == QLatin1String("axis")) {
            break; // End of this physical axis, whatever container depth got us here.
        }
        if (!xml.isStartElement() ||
            (xml.name() != QLatin1String("remap") && xml.name() != QLatin1String("vjoyremap"))) {
            continue;
        }

        const QXmlStreamAttributes attrs = xml.attributes();
        bool vjoyOk = false;
        bool targetAxisOk = false;
        const int vjoyId = attrs.value(QLatin1String("vjoy")).toString().toInt(&vjoyOk);
        const int legacyTargetAxis = attrs.value(QLatin1String("axis")).toString().toInt(&targetAxisOk);

        if (axisIdOk && vjoyOk && targetAxisOk && legacyAxisId >= 1 && legacyTargetAxis >= 1 && vjoyId >= 1 &&
            vjoyId <= 16) {
            // Legacy axis/target ids are 1-based; AxisEvent/VJoyDevice
            // indices in the current engine are 0-based. The legacy
            // format carries no curve/deadzone data of its own here, so
            // this uses CurveHandler's own safe defaults (see
            // ProfileManager's schema docs for what these match) - Fase
            // SC-7's own <vjoy-devices> curve data, if any, overwrites
            // these afterward.
            QJsonObject parameters;
            parameters[QLatin1String("deadzone")] = 0.05;
            parameters[QLatin1String("sensitivity")] = 1.0;
            parameters[QLatin1String("inputMin")] = 0;
            parameters[QLatin1String("inputMax")] = 65535;
            parameters[QLatin1String("outputMin")] = 0;
            parameters[QLatin1String("outputMax")] = 32767;

            // Fase SC-7.3: tags the source with the device this <axis> came
            // from (instead of the wildcard "" every prior phase used), so
            // bindings imported from several distinct physical devices stay
            // distinguishable and Swap Devices can target them individually.
            binding[QLatin1String("sourceDevice")] = QStringLiteral("[Legacy] ") + deviceName;
            binding[QLatin1String("sourceAxis")] = legacyAxisId - 1;
            binding[QLatin1String("actionType")] = QStringLiteral("CurveHandler");
            binding[QLatin1String("targetOutputId")] = vjoyId;
            binding[QLatin1String("targetAxis")] = legacyTargetAxis - 1;
            binding[QLatin1String("parameters")] = parameters;
        }
    }

    return binding;
}

QJsonObject LegacyProfileImporter::parseButton(QXmlStreamReader &xml, const QString &deviceName)
{
    bool buttonIdOk = false;
    const int legacyButtonId = xml.attributes().value(QLatin1String("id")).toString().toInt(&buttonIdOk);

    // Fase SC-7.1: see parseAxis()'s own comment - <remap>/<keyboard> are
    // nested behind a <container>/<action-set> wrapper here too, so this
    // scans token-by-token instead of only looking at direct children.
    QJsonObject binding;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == QLatin1String("button")) {
            break; // End of this physical button, whatever container depth got us here.
        }
        if (!binding.isEmpty() || !xml.isStartElement()) {
            // A <remap>/<keyboard> after one was already parsed (a button
            // only drives one handler), or not a start tag at all.
            continue;
        }

        if (xml.name() == QLatin1String("remap") || xml.name() == QLatin1String("vjoyremap")) {
            const QXmlStreamAttributes attrs = xml.attributes();
            bool vjoyOk = false;
            bool targetButtonOk = false;
            const int vjoyId = attrs.value(QLatin1String("vjoy")).toString().toInt(&vjoyOk);
            const int legacyTargetButton = attrs.value(QLatin1String("button")).toString().toInt(&targetButtonOk);

            if (buttonIdOk && vjoyOk && targetButtonOk && legacyButtonId >= 1 && legacyTargetButton >= 1 &&
                vjoyId >= 1 && vjoyId <= 16) {
                // Legacy button/target ids are 1-based; ButtonEvent/
                // VJoyDevice indices in the current engine are 0-based.
                binding[QLatin1String("sourceDevice")] = QStringLiteral("[Legacy] ") + deviceName;
                binding[QLatin1String("sourceButton")] = legacyButtonId - 1;
                binding[QLatin1String("actionType")] = QStringLiteral("ButtonRemapHandler");
                binding[QLatin1String("targetOutputId")] = vjoyId;
                binding[QLatin1String("targetButton")] = legacyTargetButton - 1;
            }
        } else if (xml.name() == QLatin1String("keyboard")) {
            // "key" is already a hardware scan code (this legacy schema
            // carries no symbolic key names anywhere - see the real
            // <key id=".."> elements under the "keyboard" pseudo-device),
            // sent via SendInputKeyboardBackend, matching this importer's
            // documented fallback (see class docs).
            bool scanCodeOk = false;
            const int scanCode = xml.attributes().value(QLatin1String("key")).toString().toInt(&scanCodeOk);

            if (buttonIdOk && scanCodeOk && legacyButtonId >= 1 && scanCode >= 0 && scanCode <= 0xFFFF) {
                QJsonObject parameters;
                parameters[QLatin1String("scanCode")] = scanCode;

                binding[QLatin1String("sourceDevice")] = QStringLiteral("[Legacy] ") + deviceName;
                binding[QLatin1String("sourceButton")] = legacyButtonId - 1;
                binding[QLatin1String("actionType")] = QStringLiteral("KeyboardHandler");
                binding[QLatin1String("parameters")] = parameters;
            }
        }
    }

    return binding;
}

void LegacyProfileImporter::parseVjoyDevices(QXmlStreamReader &xml, QMap<int, QMap<int, QJsonObject>> &outCurveDataByVjoy)
{
    // Joystick Gremlin's own <vjoy-device> elements carry no reliable "this
    // is vJoy 1" id of their own (see class docs) - positional order is the
    // only signal this format actually gives.
    int vJoyId = 0;
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("vjoy-device")) {
            xml.skipCurrentElement();
            continue;
        }
        ++vJoyId;

        // Fase SC-7.2: a real export nests <axis> behind a <mode
        // name="Default"> wrapper here too (the same "not actually a direct
        // child" problem Fase SC-7.1 already fixed for parseAxis()/
        // parseButton()) - readNextStartElement()/skipCurrentElement() only
        // look at direct children, so <mode> made every vJoy <axis> (and
        // therefore every curve/deadzone) invisible to this method. Scanning
        // token-by-token instead finds <axis> at whatever depth it's
        // actually nested at, breaking only once this <vjoy-device>'s own
        // closing tag is reached.
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isEndElement() && xml.name() == QLatin1String("vjoy-device")) {
                break; // End of this vJoy device, whatever container depth got us here.
            }
            if (!xml.isStartElement() || xml.name() != QLatin1String("axis")) {
                continue;
            }

            bool axisIdOk = false;
            const int legacyAxisId = xml.attributes().value(QLatin1String("id")).toString().toInt(&axisIdOk);
            const QJsonObject curveData = parseVjoyAxisCurve(xml);
            if (axisIdOk && legacyAxisId >= 1 && !curveData.isEmpty()) {
                outCurveDataByVjoy[vJoyId][legacyAxisId - 1] = curveData;
            }
        }
    }
}

QJsonObject LegacyProfileImporter::parseVjoyAxisCurve(QXmlStreamReader &xml)
{
    // Fase SC-7.1: see parseAxis()'s own comment - <response-curve> (and its
    // <deadzone>/<mapping>/<control-point> children) can likewise sit behind
    // a <container>/<action-set> wrapper rather than directly under <axis>,
    // so this scans token-by-token instead of only looking at direct
    // children. inCubicSplineMapping tracks whether the reader is currently
    // somewhere inside a <mapping type="cubic-spline">...</mapping> span,
    // since a <control-point> only means something in that context.
    QJsonObject result;
    QJsonArray curvePoints;
    bool inCubicSplineMapping = false;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == QLatin1String("axis")) {
            break; // End of this vJoy axis, whatever container depth got us here.
        }

        if (xml.isEndElement() && xml.name() == QLatin1String("mapping")) {
            inCubicSplineMapping = false;
        } else if (xml.isStartElement() && xml.name() == QLatin1String("deadzone")) {
            const QXmlStreamAttributes attrs = xml.attributes();
            const double centerHigh = attrs.value(QLatin1String("center-high")).toString().toDouble();
            const double centerLow = attrs.value(QLatin1String("center-low")).toString().toDouble();
            result[QLatin1String("deadzone")] = qMax(qAbs(centerHigh), qAbs(centerLow));
        } else if (xml.isStartElement() && xml.name() == QLatin1String("mapping")) {
            inCubicSplineMapping = xml.attributes().value(QLatin1String("type")) == QLatin1String("cubic-spline");
        } else if (inCubicSplineMapping && xml.isStartElement() && xml.name() == QLatin1String("control-point")) {
            const QXmlStreamAttributes attrs = xml.attributes();
            QJsonObject point;
            point[QLatin1String("x")] = attrs.value(QLatin1String("x")).toString().toDouble();
            point[QLatin1String("y")] = attrs.value(QLatin1String("y")).toString().toDouble();
            curvePoints.append(point);
        }
    }

    if (!curvePoints.isEmpty()) {
        result[QLatin1String("curvePoints")] = curvePoints;
    }

    return result;
}

QJsonObject LegacyProfileImporter::makeError(const QString &message)
{
    qWarning() << "LegacyProfileImporter:" << message;
    QJsonObject error;
    error[QLatin1String("error")] = message;
    return error;
}
