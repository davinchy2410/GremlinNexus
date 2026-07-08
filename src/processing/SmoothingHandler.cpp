#include "SmoothingHandler.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QJsonObject>
#include <QLatin1String>

SmoothingHandler::SmoothingHandler(double smoothingFactor, std::shared_ptr<IActionHandler> wrapped)
    : m_smoothingFactor(std::clamp(smoothingFactor, 0.0, 0.99))
    , m_wrapped(std::move(wrapped))
{
}

void SmoothingHandler::processAxis(const AxisEvent &evt)
{
    if (!m_wrapped) {
        return;
    }

    if (!m_initialized) {
        m_currentFiltered = static_cast<double>(evt.value);
        m_initialized = true;
    } else {
        // Exponential Moving Average (EMA)
        // alpha = 1.0 - smoothingFactor. If smoothing is 0, alpha is 1.0 (raw). If smoothing is 0.9, alpha is 0.1 (smooth).
        const double alpha = 1.0 - m_smoothingFactor;
        m_currentFiltered = (alpha * static_cast<double>(evt.value)) + (m_smoothingFactor * m_currentFiltered);
    }

    AxisEvent smoothed = evt;
    smoothed.value = static_cast<int>(std::lround(m_currentFiltered));
    m_wrapped->processAxis(smoothed);
}

void SmoothingHandler::processButton(const ButtonEvent & /*evt*/)
{
    // Anti-jitter smoothing has no meaning for a discrete button event.
}

QJsonObject SmoothingHandler::toJson() const
{
    QJsonObject binding;
    binding[QLatin1String("actionType")] = QStringLiteral("SmoothingHandler");

    QJsonObject parameters;
    parameters[QLatin1String("smoothingFactor")] = m_smoothingFactor;
    binding[QLatin1String("parameters")] = parameters;

    if (m_wrapped) {
        binding[QLatin1String("wrappedAction")] = m_wrapped->toJson();
    }
    return binding;
}
