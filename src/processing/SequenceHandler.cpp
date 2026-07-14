#include "SequenceHandler.h"

#include <utility>

#include <QJsonArray>
#include <QJsonObject>
#include <QLatin1String>

SequenceHandler::SequenceHandler(std::vector<std::shared_ptr<IActionHandler>> actions)
    : m_actions(std::move(actions))
{
}

void SequenceHandler::processAxis(const AxisEvent & /*evt*/)
{
    // Sequence/rotary steps are triggered by a discrete button, not an axis.
}

void SequenceHandler::processButton(const ButtonEvent &evt)
{
    if (m_actions.empty()) {
        return;
    }

    if (evt.pressed) {
        const bool isBounce =
            (m_advanceTimer.isValid() && m_advanceTimer.elapsed() < kBounceThresholdMs) ||
            (m_releaseTimer.isValid() && m_releaseTimer.elapsed() < kBounceThresholdMs);

        if (isBounce) {
            if (!m_activeAction) {
                // The release that preceded this bounced press already
                // closed out and reset m_activeAction (see m_releaseTimer's
                // own docs) - step back to whichever action that release
                // just released and re-press it, so the game/vJoy target
                // still sees a clean, continuous hold instead of a dropout.
                const std::size_t prevIndex = (m_currentIndex == 0) ? (m_actions.size() - 1) : (m_currentIndex - 1);
                m_activeAction = m_actions[prevIndex];
            }
            if (m_activeAction) {
                m_activeAction->processButton(evt);
            }
            return;
        }

        m_advanceTimer.start();
        m_activeAction = m_actions[m_currentIndex];
        if (m_activeAction) {
            m_activeAction->processButton(evt);
        }
        m_currentIndex = (m_currentIndex + 1) % m_actions.size();
    } else {
        m_releaseTimer.start();
        if (m_activeAction) {
            m_activeAction->processButton(evt);
            m_activeAction.reset();
        }
    }
}

QJsonObject SequenceHandler::toJson() const
{
    QJsonObject binding;
    binding[QLatin1String("actionType")] = QStringLiteral("SequenceHandler");

    QJsonArray actionsArray;
    for (const auto &action : m_actions) {
        actionsArray.append(action ? action->toJson() : QJsonObject());
    }

    QJsonObject parameters;
    parameters[QLatin1String("actions")] = actionsArray;
    binding[QLatin1String("parameters")] = parameters;
    return binding;
}
