#pragma once

#include "IActionHandler.h"

class MouseWorkerThread;

/// Which screen axis a given MouseRelativeAxisHandler instance drives - one
/// instance, one role, the same shape MouseButtonHandler's targetAction
/// follows for clicks/scroll.
enum class MouseAxis
{
    X,
    Y,
};

/**
 * @brief Turns one bipolar joystick axis into a target cursor *velocity*,
 *        written into a shared MouseWorkerThread rather than calling
 *        SendInput directly.
 *
 * One instance drives exactly one screen axis (X or Y) - two instances
 * sharing the same MouseWorkerThread (one per axis) is the expected setup.
 *
 * processAxis() does no OS calls itself and never blocks: it only computes
 * a target velocity and writes it into the shared MouseWorkerThread's
 * lock-free atomic (see that class' own docs for why the actual SendInput
 * pacing lives there instead of here). This keeps EventRouter's hot path
 * (processAxis can run at up to 250Hz - see DeviceMonitorWorker's own
 * flush-cadence docs) as cheap as CurveHandler's own O(1) LUT lookup.
 *
 * Raw AxisEvent::value is normalized against the caller-supplied
 * [inputMin, inputMax] range - the same inputMin/inputMax convention
 * CurveHandler takes, since a HID device's own logical range varies (a
 * 10-bit stick vs. a 16-bit one - see AxisEvent's own docs).
 */
class MouseRelativeAxisHandler : public IActionHandler
{
public:
    /**
     * @param worker      Shared MouseWorkerThread whose velocity this
     *                    handler drives - NOT owned by this handler; its
     *                    start()/stop() lifecycle and lifetime belong to
     *                    whoever wires an X+Y handler pair together.
     * @param axis        Which screen axis this instance drives.
     * @param inputMin    Raw HID logical minimum of the source axis.
     * @param inputMax    Raw HID logical maximum of the source axis.
     * @param sensitivity Pixels per 10ms tick (see MouseWorkerThread) at
     *                    full deflection (|bipolar value| == 1.0) - scales
     *                    linearly from 0 at the deadzone boundary up to
     *                    this value at full stick deflection.
     * @param deadzone    Fraction of the bipolar [-1, 1] range around
     *                    center treated as zero velocity; clamped to
     *                    [0, 0.99].
     */
    MouseRelativeAxisHandler(MouseWorkerThread &worker, MouseAxis axis, int inputMin, int inputMax,
                              double sensitivity, double deadzone);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    QJsonObject toJson() const override;

private:
    MouseWorkerThread &m_worker;
    MouseAxis m_axis;
    int m_inputMin;
    int m_inputMax;
    double m_sensitivity;
    double m_deadzone;
};
