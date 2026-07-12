#include "MacroRecorderViewModel.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QKeySequence>

#include "../core/DeviceManager.h"
#include "../output/IVirtualOutputDevice.h"
#include "../processing/ButtonRemapHandler.h"
#include "../processing/EventRouter.h"

namespace {
// Below this, two recorded events (keyboard or joystick) are treated as
// "simultaneous enough" - skips emitting a near-zero "Delay: Nms" step for
// ordinary event-loop jitter rather than something the user actually
// paused for.
constexpr qint64 kMinRecordableDelayMs = 15;
} // namespace

MacroRecorderViewModel::MacroRecorderViewModel(EventRouter &router, QObject *parent)
    : QObject(parent)
    , m_router(router)
{
}

MacroRecorderViewModel::~MacroRecorderViewModel()
{
    if (m_recording && qApp) {
        qApp->removeEventFilter(this);
    }
}

bool MacroRecorderViewModel::recording() const
{
    return m_recording;
}

void MacroRecorderViewModel::startRecording()
{
    if (m_recording) {
        return;
    }
    m_recording = true;
    m_hasLastEvent = false;
    qApp->installEventFilter(this);
    connect(&DeviceManager::instance(), &DeviceManager::buttonPressed, this,
            &MacroRecorderViewModel::onButtonPressed);
    emit recordingChanged();
}

void MacroRecorderViewModel::stopRecording()
{
    if (!m_recording) {
        return;
    }
    m_recording = false;
    qApp->removeEventFilter(this);
    disconnect(&DeviceManager::instance(), &DeviceManager::buttonPressed, this,
               &MacroRecorderViewModel::onButtonPressed);
    emit recordingChanged();
}

void MacroRecorderViewModel::recordElapsedWaitIfAny()
{
    if (m_hasLastEvent) {
        const qint64 elapsedMs = m_lastEventTimer.elapsed();
        if (elapsedMs >= kMinRecordableDelayMs) {
            emit stepRecorded(QStringLiteral("Wait"), 0, static_cast<int>(elapsedMs), QString());
        }
    }
    m_lastEventTimer.restart();
    m_hasLastEvent = true;
}

bool MacroRecorderViewModel::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_recording || (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)) {
        return QObject::eventFilter(watched, event);
    }

    auto *keyEvent = static_cast<QKeyEvent *>(event);
    if (keyEvent->isAutoRepeat()) {
        // A held key's OS auto-repeat is not a new step - only the original
        // press and the eventual release matter to playback.
        return true;
    }

    recordElapsedWaitIfAny();

    const int scanCode = static_cast<int>(keyEvent->nativeScanCode());
    QString label = QKeySequence(keyEvent->key()).toString(QKeySequence::NativeText);
    if (label.isEmpty()) {
        label = QStringLiteral("Key 0x%1").arg(scanCode, 0, 16);
    }
    const QString kind = (event->type() == QEvent::KeyPress) ? QStringLiteral("PressKey") : QStringLiteral("ReleaseKey");
    emit stepRecorded(kind, scanCode, 0, label);

    // Swallow the event while recording, so it does whatever it's meant to
    // in the macro instead of also acting on the popup's own UI underneath
    // (button mnemonics, focus navigation, etc).
    return true;
}

void MacroRecorderViewModel::onButtonPressed(const QString &systemPath, int buttonIndex, bool pressed)
{
    // Deliberately narrow scope (see class docs): only a *direct*
    // ButtonRemapHandler - resolved fresh, in whatever mode is active right
    // now - has one unambiguous "vJoy button" to record. Anything else
    // (unbound, Tempo/Toggle/keyboard/nested macro/...) gets a skip signal
    // instead of a guessed-at step.
    const std::shared_ptr<IActionHandler> handler = m_router.resolveButtonHandlerForCurrentMode(systemPath, buttonIndex);
    auto *remap = dynamic_cast<ButtonRemapHandler *>(handler.get());
    if (!remap || !remap->target()) {
        emit buttonRecordSkipped(handler
                                      ? tr("That button isn't a direct vJoy button assignment - not recorded.")
                                      : tr("That button has no assignment yet - not recorded."));
        return;
    }

    recordElapsedWaitIfAny();

    const int targetOutputId = remap->target()->deviceId();
    const bool isVigemTarget = remap->target()->isViGEmDevice();
    const int targetButton = remap->targetButton();
    const QString kind = pressed ? QStringLiteral("PressButton") : QStringLiteral("ReleaseButton");
    const QString label = tr("Button %1").arg(targetButton + 1);
    emit buttonStepRecorded(kind, targetOutputId, isVigemTarget, targetButton, label);
}
