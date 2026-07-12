#pragma once

#include <QObject>
#include <QString>

#include "IActionHandler.h"

class EventRouter;
class QTimer;

/**
 * @brief Shift-state mode switch: while its bound button is held, the
 *        router's mode is m_targetMode; on release, whichever mode should be
 *        active now that this shift is no longer held is restored
 *        automatically.
 *
 * Unlike ModeSwitchHandler (a *permanent* switch — still active after the
 * button is released), this is the "hold this button to access a temporary
 * layer" pattern (shift-states, in HOTAS terminology). Press/release push
 * and pop this handler's target mode on EventRouter's shared shift stack
 * (see EventRouter::pushTemporaryMode()/popTemporaryMode()) rather than each
 * handler instance privately snapshotting/restoring "the" previous mode -
 * that used to break the moment a second shift button (e.g. one per HOTAS
 * stick) was pressed while a first one was still held, since releasing the
 * first would stomp the mode back past the second, still-held one. A
 * redundant press while already shifted (e.g. a spurious repeat event) is
 * ignored via m_active, so it can never push its target mode twice.
 *
 * QObject (unlike every other IActionHandler, which are plain interfaces):
 * needed for m_releaseGraceTimer - see processButton()'s own docs for why a
 * release needs to be deferrable, and DeviceManager.cpp's
 * parseHidReport()/registerHidDevice() docs for the class of HID hardware
 * glitch (a real, held button transiently missing from a single otherwise-
 * "successful" report, then reappearing on the very next one - distinct
 * from the "API call outright failed" case that fix already covers) this
 * exists to survive. QObject must be the first base per Qt's own multiple-
 * inheritance rule; IActionHandler doesn't inherit QObject itself, so no
 * diamond.
 *
 * Same ownership shape as ModeSwitchHandler otherwise: a plain EventRouter&
 * that always outlives this handler, since the routing table holding it is
 * one of the router's own members (see ModeSwitchHandler's docs for the
 * full argument).
 */
class TemporaryModeSwitchHandler : public QObject, public IActionHandler
{
    Q_OBJECT

public:
    TemporaryModeSwitchHandler(EventRouter &router, QString targetMode);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    /// Rebuilds this handler's "TemporaryModeSwitch" binding JSON (Fase 13),
    /// matching ProfileManager::instantiateTemporaryModeSwitchHandler()'s
    /// "parameters.targetMode" schema. m_active is deliberately not
    /// serialized - it is live gesture state (whether this shift is
    /// currently held), not configuration, and always starts fresh when a
    /// profile is reloaded.
    QJsonObject toJson() const override;

private:
    /// How long a release is held pending before it's treated as real - see
    /// processButton()'s docs.
    static constexpr int kReleaseGraceMs = 60;

    EventRouter &m_router;
    QString m_targetMode;
    bool m_active = false;

    /// Lazily created on the first release that needs deferring. Parented
    /// to `this`, so it's destroyed automatically with the handler - no
    /// dangling-callback risk even if a profile reload destroys this
    /// handler mid-grace-period.
    QTimer *m_releaseGraceTimer = nullptr;
};
