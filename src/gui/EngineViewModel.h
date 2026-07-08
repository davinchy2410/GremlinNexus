#pragma once

#include <QObject>

class EventRouter;

/**
 * @brief ViewModel for TopHeader's global "Engine: ON/OFF" switch (Fase 10.7;
 *        moved from the Sidebar to TopHeader in Fase 15.5).
 *
 * Thin wrapper around EventRouter::start()/stop(): the engine is the
 * input->output routing/processing pipeline (DeviceManager's own hardware
 * enumeration keeps running regardless, so screens like Device Tester still
 * work with the engine off). Starts OFF - router.start() used to be called
 * unconditionally from main() (Phase 4), but now that there is a real
 * on/off switch in the UI, auto-starting would make the switch a lie the
 * first time the app opens.
 */
class EngineViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isEngineRunning READ isEngineRunning WRITE setIsEngineRunning NOTIFY isEngineRunningChanged)

public:
    explicit EngineViewModel(EventRouter &router, QObject *parent = nullptr);

    bool isEngineRunning() const;
    void setIsEngineRunning(bool running);

signals:
    void isEngineRunningChanged();

private:
    EventRouter &m_router;
    bool m_isEngineRunning;
};
