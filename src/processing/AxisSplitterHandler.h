#pragma once

#include <memory>
#include <vector>

#include "IActionHandler.h"
#include "IVirtualOutputDevice.h"

/**
 * @brief Splits one physical axis' travel into independent threshold zones,
 *        each firing its own vJoy button on entry/exit (Fase 10.9).
 *
 * Deliberately its own class rather than a generalization of
 * AxisToButtonHandler (Fase 9.5): that handler is a single-threshold
 * axis->button *bridge* feeding one wrapped IActionHandler (any handler
 * kind, not necessarily a button press - see its own class docs), which is
 * a different shape entirely from "N independently-configured [min, max]
 * zones on one axis, each directly driving its own vJoy button" - forcing
 * one class to cover both would mean AxisToButtonHandler's existing single-
 * threshold JSON schema and callers gain an unrelated multi-zone mode (or
 * vice versa), for no real reuse benefit: the two share only "an axis event
 * turns into a button state somewhere," not any actual code.
 *
 * A classic use: assign a throttle axis 90-100% of its travel to trigger
 * the afterburner/WEP detent button, independent of whatever the throttle's
 * own primary vJoy Remap already does with the raw axis value - one
 * physical axis input feeds a normal axis binding (via a separate,
 * concurrently-registered CurveHandler route) AND this handler, since
 * EventRouter dispatches the same AxisEvent to every route bound to that
 * (systemPath, axisIndex) - see ProfileManager's binding schema docs.
 */
class AxisSplitterHandler : public IActionHandler
{
public:
    /// One configured zone: [minFraction, maxFraction] of the axis' full
    /// input range (after normalizing via inputMin/inputMax - see
    /// processAxis()), firing targetButton on m_target while the axis sits
    /// inside the zone.
    struct Zone
    {
        double minFraction = 0.0; ///< In [0, 1].
        double maxFraction = 1.0; ///< In [0, 1], >= minFraction.
        int targetButton = 0;     ///< vJoy button index, [0, 128).
    };

    /**
     * @param target    Virtual output device every zone's targetButton lives on.
     * @param zones     Configured zones - order doesn't matter; zones may overlap
     *                  (each is tracked independently, so an overlapping region
     *                  simply fires every zone it falls within).
     * @param inputMin  Raw HID logical minimum of the source axis.
     * @param inputMax  Raw HID logical maximum of the source axis.
     */
    AxisSplitterHandler(std::shared_ptr<IVirtualOutputDevice> target, std::vector<Zone> zones, int inputMin = 0,
                         int inputMax = 65535);

    void processAxis(const AxisEvent &evt) override;
    void processButton(const ButtonEvent &evt) override;

    /// Rebuilds this handler's "AxisSplitterHandler" binding JSON (Fase 10.9).
    QJsonObject toJson() const override;

private:
    std::shared_ptr<IVirtualOutputDevice> m_target;
    std::vector<Zone> m_zones;
    int m_inputMin;
    int m_inputMax;

    /// Per-zone last-known inside/outside state, parallel to m_zones - only
    /// a state *transition* re-sends setButton(), mirroring how a real
    /// button only reports one press per press (same reasoning as
    /// AxisToButtonHandler's m_wasPressed).
    std::vector<bool> m_zoneActive;
};
