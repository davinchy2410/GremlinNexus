#include "EngineViewModel.h"

#include "EventRouter.h"

EngineViewModel::EngineViewModel(EventRouter &router, QObject *parent)
    : QObject(parent)
    , m_router(router)
    , m_isEngineRunning(router.isRunning())
{
}

bool EngineViewModel::isEngineRunning() const
{
    return m_isEngineRunning;
}

void EngineViewModel::setIsEngineRunning(bool running)
{
    if (m_isEngineRunning == running) {
        return;
    }

    if (running) {
        m_router.start();
    } else {
        m_router.stop();
    }

    m_isEngineRunning = running;
    emit isEngineRunningChanged();
}
