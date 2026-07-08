#include "MacroRecorderViewModel.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QKeySequence>

namespace {
// Below this, two key events are treated as "simultaneous enough" - skips
// emitting a near-zero "Delay: Nms" step for ordinary event-loop jitter
// rather than something the user actually paused for.
constexpr qint64 kMinRecordableDelayMs = 15;
} // namespace

MacroRecorderViewModel::MacroRecorderViewModel(QObject *parent)
    : QObject(parent)
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
    emit recordingChanged();
}

void MacroRecorderViewModel::stopRecording()
{
    if (!m_recording) {
        return;
    }
    m_recording = false;
    qApp->removeEventFilter(this);
    emit recordingChanged();
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

    if (m_hasLastEvent) {
        const qint64 elapsedMs = m_lastEventTimer.elapsed();
        if (elapsedMs >= kMinRecordableDelayMs) {
            emit stepRecorded(QStringLiteral("Wait"), 0, static_cast<int>(elapsedMs), QString());
        }
    }
    m_lastEventTimer.restart();
    m_hasLastEvent = true;

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
