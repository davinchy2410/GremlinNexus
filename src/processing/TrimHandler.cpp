#include "TrimHandler.h"

#include <algorithm>
#include <utility>

TrimHandler::TrimHandler(std::shared_ptr<IVirtualOutputDevice> target, int targetAxis, int stepValue,
                          int initialValue)
    : m_target(std::move(target))
    , m_targetAxis(targetAxis)
    , m_stepValue(stepValue)
    , m_currentValue(std::clamp(initialValue, 0, 32767))
{
}

void TrimHandler::processAxis(const AxisEvent & /*evt*/)
{
    // Trim is driven by discrete button presses, not axis motion.
}

void TrimHandler::processButton(const ButtonEvent &evt)
{
    if (!evt.pressed) {
        return;
    }

    m_currentValue = std::clamp(m_currentValue + m_stepValue, 0, 32767);
    if (m_target) {
        m_target->setAxis(m_targetAxis, m_currentValue);
    }
}
