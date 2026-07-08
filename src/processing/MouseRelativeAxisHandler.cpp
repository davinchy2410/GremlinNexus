#include "MouseRelativeAxisHandler.h"

#include <algorithm>
#include <cmath>

#include <QLatin1String>

#include "MouseWorkerThread.h"

MouseRelativeAxisHandler::MouseRelativeAxisHandler(MouseWorkerThread &worker, MouseAxis axis, int inputMin,
                                                     int inputMax, double sensitivity, double deadzone)
    : m_worker(worker)
    , m_axis(axis)
    , m_inputMin(inputMin)
    , m_inputMax(inputMax)
    , m_sensitivity(sensitivity)
    , m_deadzone(std::clamp(deadzone, 0.0, 0.99))
{
}

void MouseRelativeAxisHandler::processAxis(const AxisEvent &evt)
{
    const double inputRange = static_cast<double>(m_inputMax - m_inputMin);
    if (inputRange <= 0.0) {
        return;
    }

    // Normalize the raw HID value to bipolar [-1, 1] against this axis' own
    // caller-supplied logical range (see this class' own docs on why that's
    // not a fixed [0, 65535]) - same formula as CurveHandler::processAxis().
    const double normalized =
        std::clamp(((static_cast<double>(evt.value) - m_inputMin) / inputRange) * 2.0 - 1.0, -1.0, 1.0);

    const double sign = normalized < 0.0 ? -1.0 : 1.0;
    const double magnitude = std::abs(normalized);

    float velocity = 0.0f;
    if (magnitude > m_deadzone) {
        // Deadzone: rescale the region outside it back out to fill [0, 1]
        // magnitude - same shape as CurveHandler's own deadzone math - so a
        // push just past the deadzone boundary starts at (near-)zero speed
        // instead of jumping straight to whatever magnitude cleared it.
        const double adjusted = (magnitude - m_deadzone) / (1.0 - m_deadzone);
        velocity = static_cast<float>(sign * adjusted * m_sensitivity);
    }
    // magnitude <= m_deadzone: velocity stays 0.0f - explicit target-zero,
    // not just "don't update", so the worker thread's own accumulator
    // actually decays to a stop instead of coasting on the last nonzero
    // velocity this handler ever wrote.

    if (m_axis == MouseAxis::X) {
        m_worker.setVelocityX(velocity);
    } else {
        m_worker.setVelocityY(velocity);
    }
}

void MouseRelativeAxisHandler::processButton(const ButtonEvent & /*evt*/)
{
    // Axis-only handler: button events carry no meaning for a mouse-move transform.
}

QJsonObject MouseRelativeAxisHandler::toJson() const
{
    // No targetOutputId/targetDeviceType/targetAxis at the top level, unlike
    // CurveHandler's own toJson() - this handler drives the OS cursor
    // directly through the shared MouseWorkerThread, never a vJoy/ViGEm
    // IVirtualOutputDevice, so there is no vJoy target to describe -
    // parameters-only, see ProfileManager::instantiateMouseRelativeAxisHandler()'s
    // schema.
    QJsonObject binding;
    binding[QLatin1String("actionType")] = QStringLiteral("MouseRelativeAxis");

    QJsonObject parameters;
    parameters[QLatin1String("targetMouseAxis")] = m_axis == MouseAxis::X ? QStringLiteral("X") : QStringLiteral("Y");
    parameters[QLatin1String("sensitivity")] = m_sensitivity;
    parameters[QLatin1String("deadzone")] = m_deadzone;
    parameters[QLatin1String("inputMin")] = m_inputMin;
    parameters[QLatin1String("inputMax")] = m_inputMax;
    binding[QLatin1String("parameters")] = parameters;

    return binding;
}
