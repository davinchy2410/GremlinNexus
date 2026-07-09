#include <QCoreApplication>
#include <QDebug>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStringList>
#include <QUrl>

#include <QQmlEngine>
#include <QtQml/qqml.h>

#include <windows.h>
#include <objbase.h>

#include "AutoSwitchManager.h"
#include "DeviceManager.h"
#include "EventRouter.h"
#include "ShutdownTrace.h"
#include "I18nManager.h"
#include "PwaServer.h"
#include "SCIntegrationManager.h"
#include "VJoyDevice.h"
#include "VoiceFeedbackManager.h"
#include "gui/CalibrationViewModel.h"
#include "gui/CurveEditorViewModel.h"
#include "gui/DeviceTesterViewModel.h"
#include "gui/EngineViewModel.h"
#include "gui/LogModel.h"
#include "gui/MacroRecorderViewModel.h"
#include "gui/MainViewModel.h"
#include "gui/ProfileEditorViewModel.h"
#include "gui/QrCodeItem.h"
#include "gui/SettingsViewModel.h"
#include "LegacyMigrator.h"

namespace {

/// Picks the first of a short, curated list of monospace typefaces that's
/// actually installed on this machine, falling back to Qt's own platform
/// default rather than failing or silently rendering with a system serif
/// font. This is the single global switch behind the FUI/Data Terminal
/// redesign's "toda la interfaz grite Terminal" typography requirement:
/// QGuiApplication::setFont() becomes the default font every QML Text/
/// Control that doesn't set its own font.family inherits, so this one call
/// re-typefaces the whole app without needing to touch font.family in
/// dozens of individual .qml files. This project does not bundle font
/// files (nothing to license/ship yet), so this only ever *uses* a font if
/// it's already present - Consolas/Courier New ship with Windows itself,
/// so the fallback chain realistically always finds one.
void applyPreferredFont()
{
    static const QStringList kPreferredFamilies{
        QStringLiteral("Cascadia Mono"),
        QStringLiteral("Consolas"),
        QStringLiteral("Courier New"),
    };

    const QStringList available = QFontDatabase::families();
    for (const QString &family : kPreferredFamilies) {
        if (available.contains(family)) {
            QFont font(family);
            font.setStyleHint(QFont::Monospace);
            QGuiApplication::setFont(font);
            return;
        }
    }
    // None of the preferred families are installed - keep the platform default.
}

} // namespace

int main(int argc, char *argv[])
{
    // Fase 15.6: refuses to start a second instance - two processes both
    // reading RawInput / writing to vJoy at once corrupts engine state. Held
    // for the lifetime of the process; Windows releases it automatically on
    // exit, so no matching CloseHandle() is needed.
    HANDLE hMutex = CreateMutexA(NULL, FALSE, "GremblingEx_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // A native Win32 MessageBoxA rather than a QML dialog - no need to
        // spin up QGuiApplication/the QML engine just to report this.
        MessageBoxA(NULL, "GremblingEx ya se está ejecutando (revisa la bandeja del sistema).", "Instancia Duplicada", MB_ICONWARNING | MB_OK);
        return 0;
    }

    // Installed before anything else can log (DeviceManager::initialize()
    // below starts a background thread immediately) so the Log Console
    // never misses early startup messages.
    LogModel::instance().install();

    // Fase 0 (stabilization): ViGEmClient's underlying vigem_alloc/
    // vigem_connect call chain (see ViGEmDevice::acquire()) relies on
    // SetupAPI/WinUsb, which expects the calling thread's COM apartment to
    // already be initialized. Every ViGEmDevice call (acquire/update/
    // relinquish) happens on this same main thread - see EventRouter's own
    // threading docs - so one apartment-threaded init here, held for the
    // whole process lifetime, covers all of them; this is deliberately NOT
    // a per-acquire()/relinquish() Co(Un)Initialize pair, which would risk
    // one ViGEmDevice instance's release() tearing down the apartment out
    // from under a second instance still using it on the same thread.
    // RPC_E_CHANGED_MODE means some other component (a native Qt dialog,
    // etc.) already initialized COM here with a different concurrency
    // model - that's fine, some apartment is active either way - so only a
    // genuine failure is logged, and CoUninitialize() is only ever called
    // if this call is the one that actually owns the increment.
    const HRESULT comInitResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitializedHere = (comInitResult == S_OK || comInitResult == S_FALSE);
    if (FAILED(comInitResult) && comInitResult != RPC_E_CHANGED_MODE) {
        qWarning() << "main: CoInitializeEx failed (hr=" << Qt::hex << Qt::showbase << comInitResult
                   << ") - ViGEmBus output devices may fail to acquire";
    }

    QGuiApplication app(argc, argv);

    // Fase 20.5: without an organization/app name registered, QSettings has
    // nowhere to persist "LastProfile" to - the auto-load added in Fase 20.4
    // silently found nothing to load on every launch.
    QCoreApplication::setOrganizationName(QStringLiteral("Antigravity"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("antigravity.com"));
    QCoreApplication::setApplicationName(QStringLiteral("GremblingEx"));

    // Force the Basic Controls style everywhere: Phase 10's design system is
    // fully custom (dark/glassmorphism), so no native-OS-styled control
    // should ever visually bleed through.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Crisp, pixel-aligned small text rather than Qt Quick's default
    // distance-field text rendering, which can look slightly soft at the
    // small sizes a dense desktop UI uses.
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    applyPreferredFont();

    // Fase 16 Part 2: lets RemoteControlPopup.qml instantiate <QrCodeItem>
    // directly to render its pairing URL, with no C++-side context property
    // needed (unlike the ViewModels below, which are single app-wide
    // instances - a QR code is just a value-driven visual, so the ordinary
    // QML-type registration pattern fits better than a singleton).
    qmlRegisterType<QrCodeItem>("GremblingNexus", 1, 0, "QrCodeItem");

    // Fase (HidHide removal): GremblingNexus no longer manages HidHide
    // whitelisting/cloaking itself - the CLI-based integration proved too
    // flaky (async calls that either blocked the UI thread or raced
    // DeviceManager's own startup scan depending on how they were called,
    // and a stdin pipe deadlock under some HidHide/Windows configurations
    // that no timeout tuning fully resolved). Users who want a device hidden
    // from other applications now whitelist this executable and cloak the
    // device manually via HidHide's own GUI - standard practice for the
    // wider Joystick Gremlin community. Device enumeration is a single,
    // immediate, fire-and-forget scan with no HidHide-specific retry/delay
    // logic surrounding it.
    DeviceManager::instance().initialize();

    // The 200 Hz output tick (Phase 4), per EventRouter's documented
    // threading model - runs on this (main) thread's event loop once
    // started. Outlives the QML engine by construction order (declared
    // first, destroyed last). No longer auto-started here (Phase 10.7):
    // TopHeader's "Engine: ON/OFF" switch (see EngineViewModel) now owns
    // start()/stop(), so the engine begins OFF until the user turns it on
    // - auto-starting would make that switch lie on every launch.
    EventRouter router;

    // Voice Feedback: EventRouter is a plain instance here (not a
    // singleton), so this connection - not VoiceFeedbackManager's own
    // constructor - is what wires mode changes to speech. AutoConnection
    // means this dispatches as a queued call onto VoiceFeedbackManager's
    // (the main/GUI) thread whenever setMode() is called from another
    // thread, so TTS never runs synchronously on the input-handling thread.
    QObject::connect(&router, &EventRouter::modeChanged, &VoiceFeedbackManager::instance(),
                      &VoiceFeedbackManager::onModeChanged);

    // PWA remote-control server (Fase 16 Part 1, input routing fixed post-
    // Fase 17): a remote web client's authenticated button presses are
    // injected into DeviceManager exactly like real hardware (see
    // DeviceManager::injectButtonPress()), instead of feeding EventRouter
    // directly - that way DeviceTesterViewModel and ProfileEditorViewModel's
    // Quick Bind, which both only listen to DeviceManager::buttonPressed,
    // see a PWA tap too, not just EventRouter's own routing table. Whether
    // it actually starts (and which persisted PIN it uses) is now decided
    // by init() itself, from the user's last "serverEnabled" toggle in
    // RemoteControlPopup.qml - see PwaServer::init()'s own docs.
    PwaServer pwaServer;
    QObject::connect(&pwaServer, &PwaServer::remoteInputReceived, &app,
                      [](const QString &deviceName, int buttonId, bool pressed) {
        DeviceManager::instance().injectButtonPress(EventRouter::pwaSystemPath(deviceName), buttonId, pressed);
    });

    // Fase 16.5: a paired PWA client is registered as a synthetic DeviceInfo
    // (32 buttons, no axes/hats) the instant it authenticates, so it shows
    // up in the Profiles screen and can be bound exactly like a physical
    // joystick - EventRouter::pwaSystemPath() is the same "pwa:"+deviceName
    // mapping used above to route its input, so bindings against this
    // DeviceInfo's systemPath just work. Keyed by the user-chosen deviceName
    // (Fase 17), not the random per-install deviceId, so bindings survive a
    // browser cache wipe or switching to a different tablet as long as the
    // device name is re-entered the same. Both DeviceManager::
    // addOrUpdateDevice()/removeDevice() lock internally (see
    // DeviceManager.h), so calling them directly here - PwaServer and
    // DeviceManager both live on this same main thread - needs no queued
    // connection.
    QObject::connect(&pwaServer, &PwaServer::clientConnected, &app,
                      [](const QString &deviceId, const QString &deviceName) {
        Q_UNUSED(deviceId);
        DeviceInfo info;
        info.systemPath = EventRouter::pwaSystemPath(deviceName);
        info.deviceName = deviceName;
        info.isConnected = true;
        info.numButtons = 32;
        DeviceManager::instance().addOrUpdateDevice(info);
    });
    QObject::connect(&pwaServer, &PwaServer::clientDisconnected, &app,
                      [](const QString &deviceId, const QString &deviceName) {
        Q_UNUSED(deviceId);
        DeviceManager::instance().removeDevice(EventRouter::pwaSystemPath(deviceName));
    });

    // Fase (PWA UX/security refactor): self-manages whether it should be
    // running (and which persisted PIN to use) via QSettings - see
    // PwaServer::init()'s own docs - instead of always starting.
    pwaServer.init();

    // Fase 16: broadcasts a vJoy button's state to every connected PWA
    // client the instant a physical joystick flips it (see
    // VJoyDevice::setButton()), so an `indicator` control on any paired
    // tablet lights up in real time - independent of which client (if any)
    // actually owns that binding. vjoy_N uses vJoy's own 1-based button
    // numbering (buttonIndex is 0-based internally), matching how
    // handleButtonMessage() already converts the other direction for
    // incoming PWA taps.
    VJoyDevice::setTelemetryCallback([&pwaServer](unsigned int, int buttonIndex, bool pressed) {
        pwaServer.broadcastTelemetry(QStringLiteral("vjoy_%1").arg(buttonIndex + 1), pressed);
    });

    // Fase 12: polls the Windows foreground window every second and asks to
    // switch profiles when it changes - see AutoSwitchManager's own docs for
    // why polling instead of a global hook. Constructed before
    // profileEditorViewModel so the connect() below (wiring its signal to
    // profileEditorViewModel's own loadProfileFromPath()) has both ends
    // already alive; profileEditorViewModel also takes a reference to it
    // directly, for its addAutoSwitchRule()/removeAutoSwitchRule()/
    // setAutoSwitchDefaultProfile() QML-facing wrappers.
    AutoSwitchManager autoSwitch;

    MainViewModel viewModel;
    ProfileEditorViewModel profileEditorViewModel(router, autoSwitch, pwaServer);
    CurveEditorViewModel curveEditorViewModel;
    DeviceTesterViewModel deviceTesterViewModel;
    EngineViewModel engineViewModel(router);
    MacroRecorderViewModel macroRecorderViewModel;

    // Fase 11: shares profileEditorViewModel's own ProfileManager instance
    // (rather than owning a second one) so a calibration captured here is
    // the same one saveProfileToPath()/serializeProfile() write out.
    CalibrationViewModel calibrationViewModel(profileEditorViewModel.profileManager());

    SettingsViewModel settingsViewModel(autoSwitch);

    LegacyMigrator legacyMigrator;

    // Deliberately decoupled (see AutoSwitchManager's own docs): it has no
    // idea what a "profile" is, it just names a path to load.
    QObject::connect(&autoSwitch, &AutoSwitchManager::profileSwitchRequested, &profileEditorViewModel,
                      &ProfileEditorViewModel::loadProfileFromPath);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("mainViewModel"), &viewModel);
    engine.rootContext()->setContextProperty(QStringLiteral("profileEditorViewModel"), &profileEditorViewModel);
    engine.rootContext()->setContextProperty(QStringLiteral("curveEditorViewModel"), &curveEditorViewModel);
    engine.rootContext()->setContextProperty(QStringLiteral("deviceTesterViewModel"), &deviceTesterViewModel);
    engine.rootContext()->setContextProperty(QStringLiteral("engineViewModel"), &engineViewModel);
    engine.rootContext()->setContextProperty(QStringLiteral("macroRecorder"), &macroRecorderViewModel);
    engine.rootContext()->setContextProperty(QStringLiteral("logModel"), &LogModel::instance());
    engine.rootContext()->setContextProperty(QStringLiteral("pwaServer"), &pwaServer);
    engine.rootContext()->setContextProperty(QStringLiteral("calibrationViewModel"), &calibrationViewModel);
    engine.rootContext()->setContextProperty(QStringLiteral("settingsViewModel"), &settingsViewModel);
    engine.rootContext()->setContextProperty(QStringLiteral("legacyMigrator"), &legacyMigrator);
    engine.rootContext()->setContextProperty(QStringLiteral("SCManager"), &SCIntegrationManager::instance());
    I18nManager::instance().setEngine(&engine);
    engine.rootContext()->setContextProperty(QStringLiteral("I18nManager"), &I18nManager::instance());
    engine.rootContext()->setContextProperty(QStringLiteral("VoiceFeedbackManager"), &VoiceFeedbackManager::instance());

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QGuiApplication::exit(-1); }, Qt::QueuedConnection);

    // qt_add_qml_module (see CMakeLists.txt) aliases each QML_FILES entry
    // under its full path relative to the source dir, not just its
    // basename - so this is qrc:/qt/qml/<URI>/<path-to-file-from-CMakeLists>,
    // not qrc:/qt/qml/<URI>/main.qml.
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/GremblingNexus/src/qml/main.qml")));

    const int exitCode = app.exec();

    logShutdownTrace(QStringLiteral("main: app.exec() returned %1").arg(exitCode));

    // Confirmed root cause (Heisenbug investigation: HID enumeration losing
    // devices after Quit -> relaunch): DeviceManager::instance() is a
    // Meyer's singleton, so relying on its destructor to tear down the
    // monitoring thread/RawInput registration meant that teardown only ran
    // via atexit(), strictly *after* everything below - including `app`
    // itself - already unwound; undefined-behavior territory for the Qt
    // cross-thread calls that teardown needs. Calling shutdown() explicitly
    // here, while qApp and every ViewModel/the QML engine are still fully
    // alive, guarantees a deterministic, synchronous teardown instead:
    // RIDEV_REMOVE + UnregisterDeviceNotification + DestroyWindow all run on
    // the monitor thread before it's stopped, then the thread is joined and
    // its worker deleted - see DeviceManager::shutdown()'s own docs.
    DeviceManager::instance().shutdown();

    // Only undo the increment this process actually owns - see the
    // CoInitializeEx call above.
    if (comInitializedHere) {
        CoUninitialize();
    }

    logShutdownTrace(QStringLiteral("main: about to return %1 to the CRT").arg(exitCode));

    return exitCode;
}
