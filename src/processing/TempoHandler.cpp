#include "TempoHandler.h"

#include <cstddef>
#include <utility>

#include <QJsonArray>
#include <QJsonObject>
#include <QLatin1String>
#include <QString>
#include <QTimer>

#include "DelayAction.h"

/// Fase 21: drives one shortActions/longActions/doubleActions cascade
/// asynchronously so a DelayAction entry in the list genuinely pauses before
/// the next entry fires, instead of every entry firing in the same
/// EventRouter tick (the bug this class exists to fix - the old fire() sent
/// a synthetic press to every handler in the list back to back with no way
/// for DelayAction, a fire-and-forget log-only action, to hold up its
/// neighbors). Self-contained - owns its own QTimer and a private copy of
/// the handler list/systemPath/buttonIndex, no shared state with
/// TempoHandler itself - so overlapping cascades (e.g. two rapid SHORT taps
/// on the same button, or a SHORT and a LONG on two different buttons) never
/// corrupt each other. Parented to the TempoHandler that spawned it so it's
/// torn down if that handler is destroyed mid-cascade; otherwise it deletes
/// itself via deleteLater() once the last entry has fired (or, with
/// sustainLastAction, once onSourceButtonReleased() closes out the hold).
///
/// Deliberately declared at file scope, not inside an unnamed namespace:
/// TempoHandler.h forward-declares this same class so TempoHandler can hold
/// a QPointer<TempoCascadeRunner> member - an unnamed-namespace type would
/// give this a different identity per translation unit and break that.
class TempoCascadeRunner : public QObject
{
public:
    TempoCascadeRunner(std::vector<std::shared_ptr<IActionHandler>> handlers, QString systemPath, int buttonIndex,
                        int pulseDurationMs, bool sustainLastAction, QObject *parent)
        : QObject(parent)
        , m_handlers(std::move(handlers))
        , m_systemPath(std::move(systemPath))
        , m_buttonIndex(buttonIndex)
        , m_pulseDurationMs(pulseDurationMs)
        , m_sustainLastAction(sustainLastAction)
    {
        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout, this, &TempoCascadeRunner::advance);
    }

    void start() { advance(); }

    /// Fase 22: called by TempoHandler the instant the physical button that
    /// triggered this cascade (LONG/DOUBLE only - see TempoHandler's class
    /// docs) is released. Two cases:
    ///  - The cascade already reached its sustained-hold last entry
    ///    (m_heldHandler set): send that entry's release now and tear the
    ///    runner down - this is the "sustained hold" finally letting go.
    ///  - The cascade is still mid-flight, most likely parked in m_timer
    ///    waiting on a DelayAction: stop that timer and tear the runner down
    ///    without ever reaching the remaining entries - an "abort", so
    ///    nothing fires as a phantom press after the user already let go of
    ///    the button that was driving this whole cascade.
    /// Idempotent-safe to call at most once per cascade (TempoHandler only
    /// ever calls it from a release edge, and nulls its own tracking pointer
    /// right after), but harmless if called with nothing pending either way.
    void onSourceButtonReleased()
    {
        m_timer.stop();
        if (m_heldHandler) {
            const ButtonEvent release{m_systemPath, m_buttonIndex, false};
            m_heldHandler->processButton(release);
            m_heldHandler.reset();
        }
        deleteLater();
    }

private:
    void advance()
    {
        while (m_index < m_handlers.size()) {
            const std::shared_ptr<IActionHandler> handler = m_handlers[m_index];
            const bool isLastEntry = (m_index == m_handlers.size() - 1);
            ++m_index;
            if (!handler) {
                continue;
            }

            if (auto *delay = dynamic_cast<DelayAction *>(handler.get())) {
                m_timer.start(delay->delayMs());
                return; // Yield to the event loop; advance() resumes at the next entry once this fires.
            }

            const ButtonEvent press{m_systemPath, m_buttonIndex, true};
            handler->processButton(press);

            if (m_sustainLastAction && isLastEntry) {
                // Fase 22: sustained hold - no auto-release timer. Stays
                // pressed (and this runner stays alive, no deleteLater())
                // until onSourceButtonReleased() closes it out on the real
                // physical release.
                m_heldHandler = handler;
                return;
            }

            // Fase 22 (bugfix - "stuck ON" vJoy buttons): deliberately NOT
            // using the (msec, context, functor) overload here. That overload
            // ties the pending release to *this* runner's lifetime - but the
            // runner calls deleteLater() on itself the moment the while loop
            // below finishes, which happens immediately after scheduling the
            // release for whichever entry turns out to be the cascade's last
            // one. deleteLater()'s DeferredDelete event gets processed on the
            // very next return to the event loop - long before this
            // pulseDurationMs timer would ever fire - so Qt's context-object
            // guard silently cancels the release and the button (or key)
            // never sees its "up" event. The context-free overload below has
            // no such guard: the lambda itself keeps handler alive
            // (shared_ptr capture) and fires regardless of what happens to
            // the runner that scheduled it.
            const QString systemPath = m_systemPath;
            const int buttonIndex = m_buttonIndex;
            QTimer::singleShot(m_pulseDurationMs, [handler, systemPath, buttonIndex]() {
                const ButtonEvent release{systemPath, buttonIndex, false};
                handler->processButton(release);
            });
        }
        deleteLater(); // Cascade finished with no sustained hold pending; nothing left to fire.
    }

    std::vector<std::shared_ptr<IActionHandler>> m_handlers;
    QString m_systemPath;
    int m_buttonIndex;
    int m_pulseDurationMs;
    bool m_sustainLastAction;
    std::size_t m_index = 0;
    QTimer m_timer;

    /// Set only once, when the cascade's last entry fires under
    /// m_sustainLastAction - the handler currently being held pressed,
    /// awaiting onSourceButtonReleased(). Empty at every other time
    /// (including for a plain pulse-only cascade, or before the last entry
    /// has fired yet).
    std::shared_ptr<IActionHandler> m_heldHandler;
};

TempoHandler::TempoHandler(std::vector<std::shared_ptr<IActionHandler>> shortHandlers,
                            std::vector<std::shared_ptr<IActionHandler>> longHandlers,
                            std::vector<std::shared_ptr<IActionHandler>> doubleHandlers, int longPressMs,
                            int doubleTapMs, int pulseDurationMs, QObject *parent)
    : QObject(parent)
    , m_shortHandlers(std::move(shortHandlers))
    , m_longHandlers(std::move(longHandlers))
    , m_doubleHandlers(std::move(doubleHandlers))
    , m_pulseDurationMs(pulseDurationMs)
{
    m_longPressTimer = new QTimer(this);
    m_longPressTimer->setSingleShot(true);
    m_longPressTimer->setInterval(longPressMs);
    connect(m_longPressTimer, &QTimer::timeout, this, &TempoHandler::onLongPressTimeout);

    m_doubleTapTimer = new QTimer(this);
    m_doubleTapTimer->setSingleShot(true);
    m_doubleTapTimer->setInterval(doubleTapMs);
    connect(m_doubleTapTimer, &QTimer::timeout, this, &TempoHandler::onDoubleTapTimeout);
}

void TempoHandler::processAxis(const AxisEvent & /*evt*/)
{
    // Tempo gestures are triggered by a button, not an axis.
}

void TempoHandler::processButton(const ButtonEvent &evt)
{
    if (evt.pressed) {
        switch (m_state) {
        case State::Idle:
            m_pendingEvt = evt;
            m_pressTimer.start(); // Fase 20.20: bounce-filter clock for the release below.
            m_longPressTimer->start();
            m_state = State::WaitingForLongOrRelease;
            break;
        case State::WaitingForSecondTap:
            m_doubleTapTimer->stop();
            m_pendingEvt = evt;
            // Fase 22: sustainLastAction=true - this second tap is still
            // physically held right now, so the cascade's last entry can
            // legitimately hold until it's released (see
            // WaitingForDoubleFiredRelease below).
            m_activeDoubleRunner = fire(m_doubleHandlers, /*sustainLastAction=*/true);
            m_state = State::WaitingForDoubleFiredRelease;
            break;
        case State::WaitingForLongOrRelease:
        case State::WaitingForLongFiredRelease:
        case State::WaitingForDoubleFiredRelease:
            break; // Redundant press mid-gesture; ignore.
        }
        return;
    }

    // Release.
    // Fase 20.20: mechanical contact bounce filter - a switch can lose and
    // regain contact within 1-2ms of the press that just registered, which
    // this engine's 1000Hz HID polling actually catches (unlike the old
    // Python Joystick Gremlin, too slow to ever see it). Reading that as a
    // real release here would fire an accidental SHORT gesture before
    // LONG's own threshold ever got a chance to elapse - no human releases
    // a button 20ms after pressing it, so this is unambiguously bounce, not
    // input, and is simply dropped.
    if (m_state == State::WaitingForLongOrRelease && m_pressTimer.isValid() && m_pressTimer.elapsed() < 20) {
        return;
    }

    switch (m_state) {
    case State::WaitingForLongOrRelease:
        m_longPressTimer->stop();
        if (!m_doubleHandlers.empty()) {
            m_doubleTapTimer->start();
            m_state = State::WaitingForSecondTap;
        } else {
            // Fase 22: sustainLastAction=false - SHORT fires *after* this
            // very release, so there is no future physical-release event
            // left to link a hold to; every entry, including the last, gets
            // the plain pulse.
            fire(m_shortHandlers, /*sustainLastAction=*/false);
            m_state = State::Idle;
        }
        break;
    case State::WaitingForLongFiredRelease:
        // Fase 22: LONG's cascade (started in onLongPressTimeout()) already
        // pressed every entry, holding its last one open - this is that same
        // physical button's real release, so close the hold out now (or, if
        // the cascade is still mid-DelayAction, abort it) instead of firing
        // an artificial release here directly.
        if (m_activeLongRunner) {
            m_activeLongRunner->onSourceButtonReleased();
        }
        m_activeLongRunner = nullptr;
        m_state = State::Idle;
        break;
    case State::WaitingForDoubleFiredRelease:
        // Fase 22: mirrors WaitingForLongFiredRelease above - the second
        // tap's own cascade (started in the press branch above) is holding
        // its last entry open until this exact release.
        if (m_activeDoubleRunner) {
            m_activeDoubleRunner->onSourceButtonReleased();
        }
        m_activeDoubleRunner = nullptr;
        m_state = State::Idle;
        break;
    case State::Idle:
    case State::WaitingForSecondTap:
        break; // Release with no matching in-progress press; ignore.
    }
}

void TempoHandler::onLongPressTimeout()
{
    if (m_state != State::WaitingForLongOrRelease) {
        return; // Stray timeout after the state already moved on; ignore.
    }
    // Fase 22: the physical button is still actually held at this instant
    // (that's what triggered LONG in the first place), so - like DOUBLE's
    // second tap - this cascade's last entry can legitimately hold until
    // that same button is released (see WaitingForLongFiredRelease above).
    // Previously this looped m_longHandlers directly with no DelayAction/
    // pulse support at all; routing LONG through fire() like SHORT/DOUBLE
    // gives it both for free.
    m_activeLongRunner = fire(m_longHandlers, /*sustainLastAction=*/true);
    m_state = State::WaitingForLongFiredRelease;
}

void TempoHandler::onDoubleTapTimeout()
{
    if (m_state != State::WaitingForSecondTap) {
        return;
    }
    // Fase 22: sustainLastAction=false - by the time this fires, the first
    // tap was released long ago (that's what started this timer), so - same
    // reasoning as the plain-SHORT branch in processButton() - there is no
    // physical-release event left to wait for.
    fire(m_shortHandlers, /*sustainLastAction=*/false);
    m_state = State::Idle;
}

TempoCascadeRunner *TempoHandler::fire(const std::vector<std::shared_ptr<IActionHandler>> &handlers,
                                         bool sustainLastAction)
{
    if (handlers.empty()) {
        return nullptr;
    }
    // Fase 21: handed off to a TempoCascadeRunner instead of firing every
    // entry synchronously in this same tick - a DelayAction entry needs to
    // actually pause the cascade before the next entry fires (previously it
    // only logged a message and returned immediately, so e.g. "Output vJoy
    // Btn 1, Delay 1000ms, Output vJoy Btn 2" fired both buttons at once).
    // Parented to `this` so it's torn down if this TempoHandler is destroyed
    // mid-cascade.
    auto *runner = new TempoCascadeRunner(handlers, m_pendingEvt.systemPath, m_pendingEvt.buttonIndex,
                                           m_pulseDurationMs, sustainLastAction, this);
    runner->start();
    return runner;
}

QJsonObject TempoHandler::toJson() const
{
    QJsonObject binding;
    binding[QLatin1String("actionType")] = QStringLiteral("TempoHandler");

    QJsonObject parameters;
    parameters[QLatin1String("longPressMs")] = m_longPressTimer->interval();
    parameters[QLatin1String("doubleTapMs")] = m_doubleTapTimer->interval();
    parameters[QLatin1String("pulseDurationMs")] = m_pulseDurationMs;
    binding[QLatin1String("parameters")] = parameters;

    auto toJsonArray = [](const std::vector<std::shared_ptr<IActionHandler>> &handlers) {
        QJsonArray array;
        for (const auto &handler : handlers) {
            array.append(handler ? handler->toJson() : QJsonObject());
        }
        return array;
    };

    if (!m_shortHandlers.empty()) {
        binding[QLatin1String("shortActions")] = toJsonArray(m_shortHandlers);
    }
    if (!m_longHandlers.empty()) {
        binding[QLatin1String("longActions")] = toJsonArray(m_longHandlers);
    }
    if (!m_doubleHandlers.empty()) {
        binding[QLatin1String("doubleActions")] = toJsonArray(m_doubleHandlers);
    }
    return binding;
}
