#pragma once

#include <QJsonObject>
#include <QString>

#include "DeviceInfo.h"

/**
 * @brief A single node in the processing graph: transforms a raw input
 *        event (as delivered by DeviceManager) into staged output-device
 *        state.
 *
 * Concrete handlers (curves, macros, gated conditionals, ...) implement
 * whichever of processAxis/processButton is meaningful to them; the other
 * is typically a documented no-op rather than an error.
 */
class IActionHandler
{
public:
    virtual ~IActionHandler() = default;

    virtual void processAxis(const AxisEvent &evt) = 0;
    virtual void processButton(const ButtonEvent &evt) = 0;

    /**
     * @brief Whether this handler is a mode-switch (ModeSwitchHandler /
     *        TemporaryModeSwitchHandler), which EventRouter::onButtonsChanged()
     *        needs to identify so it can dispatch mode switches before any
     *        other button in the same HID report - a plain virtual call
     *        instead of the two std::dynamic_pointer_cast RTTI lookups that
     *        used to do this same check.
     */
    virtual bool isModeSwitch() const { return false; }

    /**
     * @brief Describes this handler as a JSON binding fragment (Fase 10.8).
     *
     * Returns whatever ProfileManager::instantiateHandler() would need to
     * rebuild an equivalent handler from JSON - "actionType" plus that
     * type's own "targetOutputId"/"targetAxis"/"targetButton"/"parameters"
     * fields (see ProfileManager's class docs for the exact per-type
     * shape). Deliberately does NOT include "sourceDevice"/"sourceAxis"/
     * "sourceButton"/"mode" - those describe the *route*, not the handler,
     * and are already known to whoever is walking EventRouter's routing
     * table (see EventRouter::allRoutes(), ProfileManager::serializeProfile()).
     *
     * The default implementation returns an empty object (no "actionType"
     * key) - a documented "I don't know how to describe myself yet"
     * sentinel for handler kinds nothing has taught to serialize
     * themselves. serializeProfile() skips (with a qWarning) any route
     * whose handler returns this, rather than writing out a binding that
     * would just fail to reload - an honest limitation is better than a
     * profile file that silently can't be read back.
     */
    virtual QJsonObject toJson() const { return QJsonObject(); }

    /**
     * @brief Sprint QoL Part 2: the binding's optional free-text
     *        "parameters.note" (ActionPickerPopup.qml's/CurveEditorView.qml's
     *        own "Nota / Descripción" field), or "" if none.
     *
     * Deliberately a plain concrete member on the base class, not something
     * each concrete handler's own toJson() has to remember to read/write
     * itself: every subclass builds its OWN QJsonObject independently (see
     * toJson()'s own docs), with no shared fields at all beyond
     * "actionType" - if this lived in each subclass instead, every one of
     * them (there are over a dozen) would need its own note-carrying member
     * and its own toJson()/constructor plumbing for it. Instead,
     * ProfileManager::instantiateHandler() (the single dispatcher every
     * binding - top-level or nested inside a wrapper/cascade - passes
     * through) calls setNote() once, right after building whichever
     * concrete handler, straight from the incoming JSON's "parameters.note" -
     * and ProfileManager::serializeProfile()/ProfileEditorViewModel's own
     * findBindingLabel() read it back via note() and merge it into the
     * handler's own toJson() output externally, rather than it ever being
     * part of any concrete handler's own serialization logic.
     */
    QString note() const { return m_note; }
    void setNote(const QString &note) { m_note = note; }

private:
    QString m_note;
};
