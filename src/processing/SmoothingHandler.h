#pragma once

#include <deque>
#include <memory>

#include "IActionHandler.h"

/**
 * @brief Axis-only wrapper (Fase 13): smooths a jittery physical axis (e.g.
 *        a cheap potentiometer) with an Exponential Moving Average (EMA) before
 *        forwarding the smoothed value on to m_wrapped.
 *
 * It uses a smoothing factor [0.0, 0.99] where 0 is no smoothing (raw data)
 * and 0.99 is heavy smoothing.
 *
 * Button events carry no meaning for axis smoothing and are ignored.
 */
class SmoothingHandler : public IActionHandler
{
public:
    SmoothingHandler(double smoothingFactor, std::shared_ptr<IActionHandler> wrapped);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    /// Rebuilds this handler's "SmoothingHandler" binding JSON
    QJsonObject toJson() const override;

private:
    double m_smoothingFactor;
    std::shared_ptr<IActionHandler> m_wrapped;
    
    bool m_initialized = false;
    double m_currentFiltered = 0.0;
};
