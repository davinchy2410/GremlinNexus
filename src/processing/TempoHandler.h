#pragma once

#include <memory>
#include <vector>

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>

#include "IActionHandler.h"

class QTimer;
class TempoCascadeRunner;

/**
 * @brief Distinguishes a short tap, a long hold, and a double tap on one
 *        physical button, cascading through every handler configured for
 *        whichever gesture fired, each with a synthetic press-then-release
 *        pair (same tick).
 *
 * Threading: same non-blocking design as MacroHandler (read its class docs
 * first) - a QObject owning two single-shot QTimers instead of
 * std::this_thread::sleep_for, so gesture detection never stalls the
 * EventRouter tick. Construct this, and let it receive processButton(),
 * only from the thread EventRouter itself lives on.
 *
 * State machine:
 *  - A press starts the long-press timer.
 *  - If that timer fires while still held: LONG. Fires every handler in
 *    m_longHandlers, in order, via a TempoCascadeRunner.
 *  - If released before the long-press timer fires:
 *     - m_doubleHandlers empty -> fires every handler in m_shortHandlers
 *       immediately (no reason to make every short tap wait out the
 *       double-tap window when there's nothing to disambiguate against -
 *       this is a low-latency project).
 *     - m_doubleHandlers non-empty -> starts the double-tap timer instead.
 *        - a second press arrives before it fires -> DOUBLE. Fires every
 *          handler in m_doubleHandlers, in order.
 *        - the timer fires first (no second press) -> SHORT. Fires every
 *          handler in m_shortHandlers.
 *
 * Any of the three lists left empty (e.g. no double-tap action configured)
 * is simply never fired for that gesture - not an error.
 *
 * Fase 22 "sustained hold" on the last cascade entry: LONG and DOUBLE both
 * fire while the physical button that triggered them is still actually held
 * down (LONG at the long-press threshold; DOUBLE on the second tap's own
 * press) - so for those two gestures, the cascade's last entry does not
 * auto-release after m_pulseDurationMs like every entry before it. Instead
 * it stays pressed until that same physical button is released, at which
 * point processButton() forwards the release to whichever TempoCascadeRunner
 * is still tracked in m_activeLongRunner/m_activeDoubleRunner - see
 * TempoCascadeRunner::onSourceButtonReleased() in the .cpp for what happens
 * if the cascade is still mid-flight on a DelayAction at that moment
 * (aborted, not fired late). SHORT never gets this treatment: it always
 * fires *after* its triggering button has already been released, so there is
 * no future physical-release event left to link a hold to - every SHORT
 * entry, including the last, keeps the plain 50ms pulse.
 */
class TempoHandler : public QObject, public IActionHandler
{
    Q_OBJECT

public:
    TempoHandler(std::vector<std::shared_ptr<IActionHandler>> shortHandlers,
                 std::vector<std::shared_ptr<IActionHandler>> longHandlers,
                 std::vector<std::shared_ptr<IActionHandler>> doubleHandlers, int longPressMs = 500,
                 int doubleTapMs = 250, int pulseDurationMs = 50, QObject *parent = nullptr);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    /// Rebuilds this handler's "TempoHandler" binding JSON (Fase 13,
    /// cascading actions), matching ProfileManager::instantiateTempoHandler()'s
    /// "shortActions"/"longActions"/"doubleActions"/"parameters" schema - an
    /// empty list is simply omitted, same convention as
    /// instantiateTempoHandler() treating an absent key as "no handlers for
    /// this gesture". longPressMs/doubleTapMs are read back from the
    /// QTimers themselves rather than kept in separate members, since
    /// QTimer::interval() already holds exactly what the constructor set.
    QJsonObject toJson() const override;

private slots:
    void onLongPressTimeout();
    void onDoubleTapTimeout();

private:
    enum class State
    {
        Idle,
        WaitingForLongOrRelease,     ///< Button held; m_longPressTimer running.
        WaitingForSecondTap,         ///< Released after a short tap; m_doubleTapTimer running.
        WaitingForLongFiredRelease,  ///< LONG already fired; waiting for the physical release to reset.
        WaitingForDoubleFiredRelease, ///< DOUBLE already fired; waiting for the 2nd press's release to reset.
    };

    /// Sends m_pendingEvt's systemPath/buttonIndex as a synthetic press to
    /// each handler in handlers, in order. A DelayAction entry pauses the
    /// cascade (non-blocking - see TempoCascadeRunner in the .cpp) before the
    /// next entry fires, instead of the whole list firing in the same tick.
    /// A null entry (shouldn't happen - ProfileManager already filters those
    /// out - but cheap to guard) is skipped rather than crashing. Every entry
    /// releases after m_pulseDurationMs *except* the last one when
    /// sustainLastAction is true (LONG/DOUBLE - see class docs); that entry
    /// instead stays held until the caller forwards the physical release to
    /// the returned runner via TempoCascadeRunner::onSourceButtonReleased().
    /// Returns nullptr if handlers is empty (nothing to run).
    TempoCascadeRunner *fire(const std::vector<std::shared_ptr<IActionHandler>> &handlers, bool sustainLastAction);

    std::vector<std::shared_ptr<IActionHandler>> m_shortHandlers;
    std::vector<std::shared_ptr<IActionHandler>> m_longHandlers;
    std::vector<std::shared_ptr<IActionHandler>> m_doubleHandlers;

    /// Fase 22: the in-flight cascade for whichever LONG/DOUBLE gesture last
    /// fired, tracked so the matching physical release (see processButton()'s
    /// WaitingForLongFiredRelease/WaitingForDoubleFiredRelease cases) can be
    /// forwarded to it. QPointer so a cascade that finishes and self-deletes
    /// entirely on its own (e.g. sustainLastAction was requested but the list
    /// turned out to hold nothing actionable) is safely observed as null
    /// rather than left dangling.
    QPointer<TempoCascadeRunner> m_activeLongRunner;
    QPointer<TempoCascadeRunner> m_activeDoubleRunner;

    QTimer *m_longPressTimer = nullptr;
    QTimer *m_doubleTapTimer = nullptr;

    /// Fase 20.18: how long fire()'s synthetic release is deferred for a
    /// SHORT/DOUBLE gesture's artificial press-then-release pulse -
    /// user-editable since some games/sims need longer than the previous
    /// hardcoded 50ms to register an artificial click. Not used for LONG
    /// anymore - see onLongPressTimeout()/processButton()'s
    /// WaitingForLongFiredRelease case for why that gesture now holds for
    /// as long as the physical button stays down instead of pulsing.
    int m_pulseDurationMs = 50;

    /// Fase 20.20: started on every press, checked on the matching release -
    /// a mechanical switch's contact bounce can register as a release
    /// within 1-2ms of the press that just happened, which this C++ engine
    /// (unlike the old Python Joystick Gremlin, slow enough to never catch
    /// it) reads as a real physical release, firing an accidental SHORT
    /// gesture before LONG's own threshold ever gets a chance to elapse.
    /// See processButton()'s release branch for the actual filter.
    QElapsedTimer m_pressTimer;

    State m_state = State::Idle;
    ButtonEvent m_pendingEvt; ///< systemPath/buttonIndex of the gesture in progress, for synthesized events.
};
