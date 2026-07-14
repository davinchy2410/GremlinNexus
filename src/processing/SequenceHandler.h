#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <QElapsedTimer>

#include "IActionHandler.h"

/**
 * @brief Rotary/cyclic button action (Fase 13): each physical press fires
 *        the next action in a fixed list instead of always firing the same
 *        one, wrapping back to the first action after the last.
 *
 * A press (evt.pressed == true) forwards immediately to
 * m_actions[m_currentIndex] and advances the index; the matching release is
 * forwarded to that same action once the physical button is actually
 * released (Fase 20.21 - previously synthesized as an instant same-tick
 * press-then-release pulse, which no game/vJoy target can register as a
 * real press). m_activeAction remembers which action is still "held" across
 * that gap, so the release lands correctly even though m_currentIndex may
 * have moved on by the time it arrives. An empty m_actions list is a no-op
 * rather than a crash - see ProfileManager::instantiateSequenceHandler(),
 * which refuses to build one.
 */
class SequenceHandler : public IActionHandler
{
public:
    explicit SequenceHandler(std::vector<std::shared_ptr<IActionHandler>> actions);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    /// Rebuilds this handler's "SequenceHandler" binding JSON, matching
    /// ProfileManager::instantiateSequenceHandler()'s "parameters.actions"
    /// schema - m_currentIndex (live rotation state, not configuration) is
    /// deliberately not serialized and always restarts at 0 on reload.
    QJsonObject toJson() const override;

private:
    std::vector<std::shared_ptr<IActionHandler>> m_actions;
    std::size_t m_currentIndex = 0;

    /// Fase 20.21: whichever action the physical press currently held.
    /// processButton()'s synthetic press-then-release pulse (same tick)
    /// used to mean no game/vJoy target could ever detect a 0ms-wide press
    /// - now the press forwards immediately and the matching release is
    /// deferred until the physical button is actually released, the same
    /// "hold for as long as it's held" behavior a plain remap gets.
    /// m_currentIndex still only advances on press, so a step never fires
    /// twice for one physical press even though the release comes later.
    std::shared_ptr<IActionHandler> m_activeAction;

    /// Fase 20.22: started on every legitimate index advance, checked on
    /// the next press - a mechanical switch's contact bounce can register
    /// as press-release-press within a couple milliseconds, and since a
    /// press used to always advance m_currentIndex, that bounce silently
    /// consumed two steps per physical press instead of one (the reported
    /// "it skips by twos and alternates between two buttons" symptom). A
    /// press arriving under kBounceThresholdMs after the last real advance
    /// is treated as that same bounce - forwarded to m_activeAction so the
    /// game/vJoy target still sees a clean press, but m_currentIndex does
    /// not move.
    QElapsedTimer m_advanceTimer;

    /// Fase 20.24: started on every real release, checked on the next
    /// press - a mechanical switch's spring can also bounce on the way
    /// *up*, producing a phantom release-then-press well after the press
    /// that m_advanceTimer above already guards. Since that release
    /// already reset m_activeAction to null (a real release), the
    /// re-press's bounce filter can't just forward to m_activeAction - it
    /// has to logically step back to whichever action the release just
    /// closed out, re-press it, and still not advance m_currentIndex.
    QElapsedTimer m_releaseTimer;

    /// Bounce window for both m_advanceTimer and m_releaseTimer above -
    /// tuned against real hardware (a plain 50ms guess proved too tight for
    /// the physical rotary switch this was built against; contact bounce on
    /// that switch can run up to ~150ms).
    static constexpr qint64 kBounceThresholdMs = 150;
};
