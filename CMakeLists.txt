cmake_minimum_required(VERSION 3.21)

project(GremblingNexus LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Xml Gui PrintSupport Qml Quick QuickControls2 QuickEffects QuickDialogs2 Network WebSockets Svg LinguistTools TextToSpeech Multimedia)

# Opt in to the current Qt6 QML-module CMake behavior explicitly (default
# ":/qt/qml/" resource prefix; auto-generated qmldir files for every
# subdirectory passed to QML_FILES below) instead of leaving both unset and
# getting a "policy not set" warning on every configure. QTP0004 matters
# once QML_FILES spans more than one directory again (all Phase 10 Part 1
# .qml files are deliberately kept flat in src/qml/ for now - see that
# directory's own note: splitting pragma-Singleton files like Theme.qml
# into a separate subdirectory made them fail to resolve from sibling
# directories, since each extra directory gets its own qmldir).
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# -----------------------------------------------------------------------------
# Phase 1: Core device catalogue module (DeviceManager).
# -----------------------------------------------------------------------------
add_library(GremblingCore STATIC
    src/core/DeviceInfo.h
    src/core/DeviceManager.h
    src/core/DeviceManager.cpp
    src/core/ShutdownTrace.h
    src/core/AsyncLogSink.h
    src/core/AsyncLogSink.cpp
    src/core/AutoSwitchManager.h
    src/core/AutoSwitchManager.cpp
    src/core/I18nManager.h
    src/core/I18nManager.cpp
    src/core/VoiceFeedbackManager.h
    src/core/VoiceFeedbackManager.cpp
    src/core/HidHideController.h
    src/core/HidHideController.cpp
    src/network/PwaServer.h
    src/network/PwaServer.cpp
)

target_include_directories(GremblingCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/network
)

target_link_libraries(GremblingCore
    PUBLIC
        Qt6::Core
        Qt6::Network
        Qt6::WebSockets
        Qt6::Qml
        Qt6::TextToSpeech
)

# Embeds the PWA's single static HTML file into the binary (Fase 16 Part 3)
# so PwaServer::loadPwaHtml() can serve it from ":/pwa/index.html" with no
# loose file to ship or lose track of at runtime.
qt_add_resources(GremblingCore "pwa_resources"
    PREFIX "/pwa"
    BASE "src/pwa"
    FILES src/pwa/index.html
)

if(WIN32)
    # RawInput/PnP-notification (user32), HID report parsing (hid),
    # device-interface enumeration (setupapi) and device-tree/CM_Get_Parent
    # traversal (cfgmgr32, for reading the real bus-reported product name of
    # composite USB devices off their parent node) used by DeviceManager's
    # Windows-native monitoring backend.
    target_link_libraries(GremblingCore
        PUBLIC
            user32
            hid
            setupapi
            cfgmgr32
    )
endif()

# -----------------------------------------------------------------------------
# Phase 3: Virtual output module (VirtualOutputManager / vJoy backend).
#
# Kept as its own target, separate from GremblingCore, mirroring the
# MasterPlan's split between the input-reading and output-generating
# modules: this module has no dependency on DeviceManager/DeviceInfo, and
# vJoyInterface.dll is resolved dynamically at runtime (QLibrary), so it
# needs no extra Windows import libraries at link time.
# -----------------------------------------------------------------------------
add_library(GremblingOutput STATIC
    src/output/IVirtualOutputDevice.h
    src/output/VJoyDevice.h
    src/output/VJoyDevice.cpp
    src/output/ViGEmDevice.h
    src/output/ViGEmDevice.cpp
    src/output/VirtualOutputManager.h
    src/output/VirtualOutputManager.cpp
)

target_include_directories(GremblingOutput
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/output
    PRIVATE
        # Header-only logShutdownTrace() (ShutdownTrace.h) only, for
        # VJoyDevice's silent-disconnect diagnostics - not worth linking the
        # whole GremblingCore library just for one inline function.
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

target_link_libraries(GremblingOutput
    PUBLIC
        Qt6::Core
)

# -----------------------------------------------------------------------------
# Phase 4/5/5.5/7: Core routing / processing module (EventRouter,
# IActionHandler, CurveHandler, ButtonRemapHandler, KeyboardHandler +
# IKeyboardBackend backends, MacroHandler, ProfileManager,
# LegacyProfileImporter). Bridges GremblingCore's raw input events to
# GremblingOutput's virtual devices, so it links both PUBLICly (its own
# public headers, e.g. IActionHandler.h, expose AxisEvent/ButtonEvent from
# GremblingCore and IVirtualOutputDevice from GremblingOutput). JSON
# profile (de)serialization uses QJsonDocument/QJsonObject/QJsonArray from
# Qt6::Core; legacy XML import uses QXmlStreamReader from Qt6::Xml
# (streaming, not DOM, to keep memory flat regardless of how large the
# legacy profile is); SendInputKeyboardBackend uses the Win32 SendInput API
# (user32).
# -----------------------------------------------------------------------------
add_library(GremblingProcessing STATIC
    src/processing/IActionHandler.h
    src/processing/CurveHandler.h
    src/processing/CurveHandler.cpp
    src/processing/ButtonRemapHandler.h
    src/processing/ButtonRemapHandler.cpp
    src/processing/HatRemapHandler.h
    src/processing/HatRemapHandler.cpp
    src/processing/AxisSplitterHandler.h
    src/processing/AxisSplitterHandler.cpp
    src/processing/IKeyboardBackend.h
    src/processing/SendInputKeyboardBackend.h
    src/processing/SendInputKeyboardBackend.cpp
    src/processing/KernelKeyboardBackend.h
    src/processing/KernelKeyboardBackend.cpp
    src/processing/KeyboardHandler.h
    src/processing/KeyboardHandler.cpp
    src/processing/MacroHandler.h
    src/processing/MacroHandler.cpp
    src/processing/ModeSwitchHandler.h
    src/processing/ModeSwitchHandler.cpp
    src/processing/TemporaryModeSwitchHandler.h
    src/processing/TemporaryModeSwitchHandler.cpp
    src/processing/TrimHandler.h
    src/processing/TrimHandler.cpp
    src/processing/Win32MouseInjector.h
    src/processing/Win32MouseInjector.cpp
    src/processing/MouseWorkerThread.h
    src/processing/MouseWorkerThread.cpp
    src/processing/MouseRelativeAxisHandler.h
    src/processing/MouseRelativeAxisHandler.cpp
    src/processing/MouseButtonHandler.h
    src/processing/MouseButtonHandler.cpp
    src/processing/ConditionHandler.h
    src/processing/ConditionHandler.cpp
    src/processing/ToggleHandler.h
    src/processing/ToggleHandler.cpp
    src/processing/TempoHandler.h
    src/processing/TempoHandler.cpp
    src/processing/AxisToButtonHandler.h
    src/processing/AxisToButtonHandler.cpp
    src/processing/MergeAxisHandler.h
    src/processing/MergeAxisHandler.cpp
    src/processing/SplitAxisHandler.h
    src/processing/SplitAxisHandler.cpp
    src/processing/SequenceHandler.h
    src/processing/SequenceHandler.cpp
    src/processing/SmoothingHandler.h
    src/processing/SmoothingHandler.cpp
    src/processing/DelayAction.h
    src/processing/DelayAction.cpp
    src/processing/AudioAction.h
    src/processing/AudioAction.cpp
    src/processing/TTSAction.h
    src/processing/TTSAction.cpp
    src/processing/EventRouter.h
    src/processing/EventRouter.cpp
    src/processing/ProfileManager.h
    src/processing/ProfileManager.cpp
    src/processing/LegacyProfileImporter.h
    src/processing/LegacyProfileImporter.cpp
    src/processing/R14ProfileImporter.h
    src/processing/R14ProfileImporter.cpp
    src/processing/LegacyMigrator.h
    src/processing/LegacyMigrator.cpp
    src/processing/SCIntegrationManager.h
    src/processing/SCIntegrationManager.cpp
)

target_include_directories(GremblingProcessing
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/processing
)

target_link_libraries(GremblingProcessing
    PUBLIC
        GremblingCore
        GremblingOutput
        Qt6::Xml
        Qt6::Gui
        Qt6::PrintSupport
        Qt6::Multimedia
)

if(WIN32)
    # SendInputKeyboardBackend's SendInput() call.
    target_link_libraries(GremblingProcessing
        PUBLIC
            user32
    )
endif()

# -----------------------------------------------------------------------------
# Phase 10: QML/Qt Quick desktop frontend (design-system shell) and
# application entry point. Replaces the Phase 6 QWidgets MainWindow per the
# architect's explicit direction to drop QWidgets entirely - GremblingNexus
# no longer links Qt6::Widgets. (MainWindow.h/.cpp are left on disk,
# unreferenced by the build, as reference material for porting its
# profile-load/migrate/device-table logic into the future QML "Profiles"
# screen; delete them once that porting is done.)
# -----------------------------------------------------------------------------

# GremblingNexus rename: this project's own directory path contains a space
# ("grembling 2.0"), and this MinGW windres build cannot parse a quoted -I/-D
# argument containing a space - it forwards them to its internal cc1
# preprocessing pass where the quoting is lost, splitting "...grembling
# 2.0/build/..." into two bogus arguments and failing outright. CMake's
# default RC compile rule always injects the target's full include-dir/
# compile-definition list (needed for real C++ sources, irrelevant for a
# resource script), so app.rc - which has no #include/#define of its own -
# fails to compile the instant this target links anything with a spaced
# include path (i.e. always, since the project root itself qualifies).
# Dropping <DEFINES>/<INCLUDES> here is safe: windres still resolves app.rc's
# relative "app.ico" reference against the .rc file's own directory
# regardless of CWD or -I, confirmed by compiling it standalone with zero -I
# flags.
if(WIN32)
    set(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> <FLAGS> -O coff <SOURCE> -o <OBJECT>")
endif()

qt_add_executable(GremblingNexus WIN32
    app.rc
    src/main.cpp
    src/gui/MainViewModel.h
    src/gui/MainViewModel.cpp
    src/gui/ProfileEditorViewModel.h
    src/gui/ProfileEditorViewModel.cpp
    src/gui/CalibrationViewModel.h
    src/gui/CalibrationViewModel.cpp
    src/gui/SettingsViewModel.h
    src/gui/SettingsViewModel.cpp
    src/gui/CurveEditorViewModel.h
    src/gui/CurveEditorViewModel.cpp
    src/gui/DeviceTesterViewModel.h
    src/gui/DeviceTesterViewModel.cpp
    src/gui/EngineViewModel.h
    src/gui/EngineViewModel.cpp
    src/gui/MacroRecorderViewModel.h
    src/gui/MacroRecorderViewModel.cpp
    src/gui/LogModel.h
    src/gui/LogModel.cpp
    src/gui/QrCodeItem.h
    src/gui/QrCodeItem.cpp
    src/vendor/qrcodegen/qrcodegen.hpp
    src/vendor/qrcodegen/qrcodegen.cpp
)

target_include_directories(GremblingNexus
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gui
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor/qrcodegen
)

# QML_FILES compiles every listed .qml into the binary as Qt Resource data
# (qrc:/qt/qml/GremblingNexus/...), so there is no loose .qml file to ship or
# lose track of at runtime - satisfies the "compile the .qml into the
# binary" requirement without a hand-maintained .qrc.
#
# Theme.qml's own `pragma Singleton` is NOT enough by itself for this Qt/CMake
# combination to register it as a singleton in the generated qmldir - without
# this, the emitted entry was a plain (non-singleton) type, so every
# `Theme.someProperty` reference elsewhere silently evaluated to undefined
# (no compile error - only a runtime "Unable to assign [undefined]" warning).
set_source_files_properties(src/qml/Theme.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(GremblingNexus
    URI GremblingNexus
    VERSION 1.0
    QML_FILES
        src/qml/main.qml
        src/qml/Theme.qml
        src/qml/TopHeader.qml
        src/qml/TopHeaderButton.qml
        src/qml/WindowControls.qml
        src/qml/ScreenPlaceholder.qml
        src/qml/ProfileEditorView.qml
        src/qml/DeviceCard.qml
        src/qml/InputRow.qml
        src/qml/Badge.qml
        src/qml/CurveEditorView.qml
        src/qml/CurveHandle.qml
        src/qml/DeviceTesterView.qml
        src/qml/LogConsoleView.qml
        src/qml/StarCitizenView.qml
        src/qml/SvgIcon.qml
        src/qml/ActionPickerPopup.qml
        src/qml/MacroEditorPopup.qml
        src/qml/AxisSplitterPopup.qml
        src/qml/SequencePopup.qml
        src/qml/OneToOnePopup.qml
        src/qml/ConfirmationPopup.qml
        src/qml/ModeManagerPopup.qml
        src/qml/VJoyAuditorPopup.qml
        src/qml/SettingsView.qml
        src/qml/CalibrationWizardPopup.qml
        src/qml/GlassPanel.qml
        src/qml/RemoteControlPopup.qml
        src/qml/AutoSwitchPopup.qml
        src/qml/ValueSlider.qml
        src/qml/ToolButton.qml
        src/qml/AppComboBox.qml
        src/qml/OutputDeviceCombo.qml
        src/qml/ToggleSwitch.qml
        src/qml/PointCoordinateField.qml
        src/qml/GremblingButton.qml
        src/qml/AppMenuItem.qml
    RESOURCES
        src/qml/icons/profiles.svg
        src/qml/icons/curves.svg
        src/qml/icons/tester.svg
        src/qml/icons/web.svg
        src/qml/icons/settings.svg
        src/qml/icons/log.svg
        src/qml/icons/engine.svg
        src/qml/grembling_icon.svg
)

target_link_libraries(GremblingNexus
    PRIVATE
        GremblingCore
        GremblingOutput
        GremblingProcessing
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickEffects
        Qt6::QuickDialogs2
        Qt6::Svg
        Qt6::TextToSpeech
        ole32
)

# WIN32/MACOSX_BUNDLE deliberately omitted from qt_add_executable() above:
# keeping the console subsystem attached means qDebug()/qWarning()/qInfo()
# output (used throughout DeviceManager, VJoyDevice, ProfileManager,
# LegacyProfileImporter, ...) stays visible while iterating. Add WIN32 once
# the app is ready to ship without a background console window.

# -----------------------------------------------------------------------------
# Internationalization (i18n): lrelease-compiles i18n/grembling_es.ts into a
# .qm embedded straight into GremblingNexus' own resources (default
# RESOURCE_PREFIX "/i18n", matching I18nManager::setLanguage()'s
# ":/i18n/grembling_<langCode>.qm" load path) - no loose .qm to ship or lose
# track of at runtime, same reasoning as every other qt_add_resources/
# QML_FILES usage in this file. lupdate re-scans this target's own sources
# for tr()/qsTr() calls; run the generated "update_translations" build
# target by hand whenever new translatable strings are added, then commit
# the refreshed i18n/grembling_es.ts.
#
# IMMEDIATE_CALL: without it, qt_add_translations() defers itself via
# cmake_language(DEFER ... CALL qt6_add_translations ${forwarded_args}) so it
# can see targets defined later in the same directory - but it forwards
# TS_FILES as an *absolutized, unquoted* path first, and this project's own
# root directory contains a space ("grembling 2.0"); the deferred call's
# argument re-parsing splits that path apart at the space, crashing CMake
# configure with `get_target_property() called with non-existent target
# "2.0/build"`. GremblingNexus is already fully defined above this point, so
# nothing is gained from deferring here - IMMEDIATE_CALL runs the macro right
# now instead, skipping that whole broken code path entirely.
qt_add_translations(GremblingNexus
    TS_FILES i18n/grembling_es.ts
    IMMEDIATE_CALL
)

# Display-name correction (Grembling -> Gremlin, the actual name inherited
# from "Joystick Gremlin"): renaming the CMake target/QML module URI/C++
# namespace itself would break every "import GremblingNexus" across dozens of
# .qml files for no user-visible benefit - OUTPUT_NAME only changes the
# emitted .exe's filename, leaving the target name (and everything that
# references it) untouched.
set_target_properties(GremblingNexus PROPERTIES OUTPUT_NAME "GremlinNexus")
