#include "MacroHandler.h"

#include <QJsonArray>
#include <QLatin1String>
#include <QTimer>

#include "IKeyboardBackend.h"
#include "Win32MouseInjector.h"

namespace {
constexpr int kMacroScrollDelta = 120; // Win32's own WHEEL_DELTA - one scroll notch, same as MouseButtonHandler's kWheelDelta.
} // namespace

MacroHandler::MacroHandler(std::shared_ptr<IVirtualOutputDevice> target,
                            std::shared_ptr<IKeyboardBackend> keyboardBackend, std::vector<MacroStep> steps,
                            QObject *parent)
    : QObject(parent)
    , m_target(std::move(target))
    , m_keyboardBackend(std::move(keyboardBackend))
    , m_steps(std::move(steps))
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &MacroHandler::onTimerFired);
}

void MacroHandler::processAxis(const AxisEvent & /*evt*/)
{
    // Macros are triggered by buttons, not axes.
}

void MacroHandler::processButton(const ButtonEvent &evt)
{
    if (!evt.pressed || m_running || m_steps.empty()) {
        return;
    }

    m_running = true;
    m_currentStep = 0;
    executeStep();
}

void MacroHandler::onTimerFired()
{
    executeStep();
}

void MacroHandler::executeStep()
{
    while (m_currentStep < m_steps.size()) {
        const MacroStep &step = m_steps[m_currentStep];
        ++m_currentStep;

        switch (step.type) {
        case MacroStep::Type::PressButton:
            if (m_target) {
                m_target->setButton(step.buttonIndex, true);
            }
            break;
        case MacroStep::Type::ReleaseButton:
            if (m_target) {
                m_target->setButton(step.buttonIndex, false);
            }
            break;
        case MacroStep::Type::PressKey:
            if (m_keyboardBackend) {
                m_keyboardBackend->sendKey(step.scanCode, true);
            }
            break;
        case MacroStep::Type::ReleaseKey:
            if (m_keyboardBackend) {
                m_keyboardBackend->sendKey(step.scanCode, false);
            }
            break;
        case MacroStep::Type::PressMouseButton: {
            Win32MouseInjector injector;
            injector.sendMouseButton(step.mouseAction, true);
            break;
        }
        case MacroStep::Type::ReleaseMouseButton: {
            Win32MouseInjector injector;
            injector.sendMouseButton(step.mouseAction, false);
            break;
        }
        case MacroStep::Type::MouseScroll: {
            // Fires once, like MouseButtonHandler's own ScrollUp/ScrollDown
            // handling - a scroll wheel has no "release" of its own, so
            // there is no matching Press/Release pair for this step.
            Win32MouseInjector injector;
            injector.sendMouseScroll(step.mouseAction == QLatin1String("ScrollUp") ? kMacroScrollDelta
                                                                                    : -kMacroScrollDelta);
            break;
        }
        case MacroStep::Type::Wait:
            m_timer->start(step.waitMs);
            return; // Yield to the event loop; onTimerFired() resumes us.
        }
    }

    m_running = false; // Sequence finished.
}

QJsonObject MacroHandler::toJson() const
{
    QJsonObject binding;
    binding[QLatin1String("actionType")] = QStringLiteral("MacroHandler");
    // Omitted (not written as 0) for a pure-keyboard macro - "targetOutputId": 0
    // would round-trip as a real-looking-but-nonsensical vJoy device id instead
    // of "no vJoy device", the same reasoning instantiateMacroHandler() below
    // uses to treat an absent targetOutputId as "steps never touch vJoy".
    if (m_target) {
        binding[QLatin1String("targetOutputId")] = m_target->deviceId();
        if (m_target->isViGEmDevice()) {
            binding[QLatin1String("targetDeviceType")] = QStringLiteral("vigem");
        }
    }

    QJsonArray stepsArray;
    for (const MacroStep &step : m_steps) {
        QJsonObject stepObject;
        switch (step.type) {
        case MacroStep::Type::PressButton:
            stepObject[QLatin1String("type")] = QStringLiteral("PressButton");
            stepObject[QLatin1String("buttonIndex")] = step.buttonIndex;
            break;
        case MacroStep::Type::ReleaseButton:
            stepObject[QLatin1String("type")] = QStringLiteral("ReleaseButton");
            stepObject[QLatin1String("buttonIndex")] = step.buttonIndex;
            break;
        case MacroStep::Type::PressKey:
            stepObject[QLatin1String("type")] = QStringLiteral("PressKey");
            stepObject[QLatin1String("scanCode")] = step.scanCode;
            break;
        case MacroStep::Type::ReleaseKey:
            stepObject[QLatin1String("type")] = QStringLiteral("ReleaseKey");
            stepObject[QLatin1String("scanCode")] = step.scanCode;
            break;
        case MacroStep::Type::PressMouseButton:
            stepObject[QLatin1String("type")] = QStringLiteral("PressMouseButton");
            stepObject[QLatin1String("targetAction")] = step.mouseAction;
            break;
        case MacroStep::Type::ReleaseMouseButton:
            stepObject[QLatin1String("type")] = QStringLiteral("ReleaseMouseButton");
            stepObject[QLatin1String("targetAction")] = step.mouseAction;
            break;
        case MacroStep::Type::MouseScroll:
            stepObject[QLatin1String("type")] = QStringLiteral("MouseScroll");
            stepObject[QLatin1String("targetAction")] = step.mouseAction;
            break;
        case MacroStep::Type::Wait:
            stepObject[QLatin1String("type")] = QStringLiteral("Wait");
            stepObject[QLatin1String("waitMs")] = step.waitMs;
            break;
        }
        stepsArray.append(stepObject);
    }

    QJsonObject parameters;
    parameters[QLatin1String("steps")] = stepsArray;
    binding[QLatin1String("parameters")] = parameters;
    return binding;
}
