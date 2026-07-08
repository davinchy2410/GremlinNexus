#include "SequenceHandler.h"

#include <utility>

#include <QDebug>
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
        const qint64 adv = m_advanceTimer.isValid() ? m_advanceTimer.elapsed() : -1;
        const qint64 rel = m_releaseTimer.isValid() ? m_releaseTimer.elapsed() : -1;

        if ((m_advanceTimer.isValid() && adv < 150) || (m_releaseTimer.isValid() && rel < 150)) {
            qInfo() << "SequenceHandler: Rebote ignorado (press). adv=" << adv << "rel=" << rel;
            if (!m_activeAction) {
                const std::size_t prevIndex = (m_currentIndex == 0) ? (m_actions.size() - 1) : (m_currentIndex - 1);
                m_activeAction = m_actions[prevIndex];
                qInfo() << "SequenceHandler: Restoring prev action for bounce index=" << prevIndex;
            }
            if (m_activeAction) {
                m_activeAction->processButton(evt);
            }
            return;
        }

        qInfo() << "SequenceHandler: Pulsacion LEGITIMA. adv=" << adv << "rel=" << rel << "currentIndex=" << m_currentIndex;
        m_advanceTimer.start();
        m_activeAction = m_actions[m_currentIndex];
        if (m_activeAction) {
            m_activeAction->processButton(evt);
        } else {
            qInfo() << "SequenceHandler: WARNING: m_actions[currentIndex] is null!";
        }
        m_currentIndex = (m_currentIndex + 1) % m_actions.size();
    } else {
        const qint64 adv = m_advanceTimer.isValid() ? m_advanceTimer.elapsed() : -1;
        m_releaseTimer.start();
        qInfo() << "SequenceHandler: Release received. adv=" << adv;
        if (m_activeAction) {
            qInfo() << "SequenceHandler: Passing release to m_activeAction and resetting it.";
            m_activeAction->processButton(evt);
            m_activeAction.reset();
        } else {
            qInfo() << "SequenceHandler: WARNING: Release received but m_activeAction is ALREADY NULL!";
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
