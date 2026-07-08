#include "ConditionHandler.h"

#include <utility>

#include "EventRouter.h"

ConditionHandler::ConditionHandler(EventRouter &router, QString modSystemPath, int modButtonIndex,
                                    bool requirePressed, std::shared_ptr<IActionHandler> wrapped)
    : m_router(router)
    , m_modSystemPath(std::move(modSystemPath))
    , m_modButtonIndex(modButtonIndex)
    , m_requirePressed(requirePressed)
    , m_wrapped(std::move(wrapped))
{
}

bool ConditionHandler::conditionMet() const
{
    return m_router.isButtonPressed(m_modSystemPath, m_modButtonIndex) == m_requirePressed;
}

void ConditionHandler::processAxis(const AxisEvent &evt)
{
    if (m_wrapped && conditionMet()) {
        m_wrapped->processAxis(evt);
    }
}

void ConditionHandler::processButton(const ButtonEvent &evt)
{
    if (m_wrapped && conditionMet()) {
        m_wrapped->processButton(evt);
    }
}
