#pragma once

#include <QString>

#include "IActionHandler.h"

/**
 * @brief Maps one physical button to a mouse click or scroll-wheel tick,
 *        via Win32MouseInjector - the click/scroll counterpart to
 *        MouseRelativeAxisHandler's axis-driven relative cursor movement
 *        (both live under the same Win32MouseInjector family - see that
 *        class' own docs).
 *
 * One instance drives exactly one targetAction - every new
 * Win32MouseInjector-based feature (this class, plus
 * MouseRelativeAxisHandler's relative axis movement) lives under one
 * consistent family/code path.
 *
 * Unlike MouseRelativeAxisHandler, this needs no MouseWorkerThread or
 * pacing of any kind: button press/release events are inherently
 * low-frequency (DeviceManager never throttles them - see
 * DeviceMonitorWorker's own docs on why axes but not buttons get
 * throttled), so calling Win32MouseInjector directly and synchronously
 * from processButton() adds no measurable latency and needs no separate
 * thread.
 */
class MouseButtonHandler : public IActionHandler
{
public:
    /// @param targetAction One of "Left", "Right", "Middle" (a click -
    ///                     fires on both press and release) or "ScrollUp"/
    ///                     "ScrollDown" (one wheel tick - fires on press
    ///                     only; release is a no-op, matching how a
    ///                     physical mouse wheel has no "release" of its own).
    explicit MouseButtonHandler(const QString &targetAction);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    QJsonObject toJson() const override;

private:
    QString m_targetAction;
};
