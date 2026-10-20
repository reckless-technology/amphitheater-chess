# The window, Reckless UCI Host, on the toolkit: the app (RecklessUciHost) - a macOS .app, a Windows executable with its
# version resource and icon, a Linux binary - and the files it carries inside itself.

# The files the binary carries (the toolkit's reckless_module_assets): the series' serif, the knob and the company mark,
# the monospaced family the engines' output and the FEN are set in, the app's icon for a Linux window (its 256-pixel
# render, icon-256.png, which Main.cpp hands the platform as _NET_WM_ICON), and the twelve pieces
# (assets/pieces/staunton), each by the name pieces/<piece>.svg.
reckless_monospace_font_files(uci_host_monospace)
set(uci_host_pieces "")
foreach(colour w b)
    foreach(kind K Q R B N P)
        list(APPEND uci_host_pieces "pieces/${colour}${kind}.svg=${PROJECT_SOURCE_DIR}/assets/pieces/staunton/${colour}${kind}.svg")
    endforeach()
endforeach()
reckless_module_assets(uci_host_assets
    ${uci_host_monospace}
    ${uci_host_pieces}
    "icon-256.png=${PROJECT_SOURCE_DIR}/assets/icon/png/icon-256.png")

# The window's views and controllers, which the app, the render tool and the window's suite link.
add_library(uci_host_gui STATIC
    AnalysisPanel.cpp
    AnalysisSession.cpp
    BundledEngines.cpp
    BoardView.cpp
    CompetitionDialog.cpp
    CompetitionSession.cpp
    Controls.cpp
    EngineCatalogue.cpp
    EngineLog.cpp
    EngineManagerDialog.cpp
    EvalGraph.cpp
    GameFiles.cpp
    GamePickerDialog.cpp
    MainController.cpp
    MainView.cpp
    Menus.cpp
    MovesList.cpp
    NewGameDialog.cpp
    Panels.cpp
    PieceSet.cpp
    PlaySession.cpp
    SearchTracker.cpp
    Showcase.cpp
    StandingsDialog.cpp
    WindowSizes.cpp)
target_include_directories(uci_host_gui PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(uci_host_gui
    PUBLIC  uci_host_match uci_host_cli_core uci_host_version uci_host_assets reckless_app_shell reckless_common_panel reckless_gfx_skia
    PRIVATE uci_host_warnings)

add_executable(RecklessUciHost Main.cpp)
target_link_libraries(RecklessUciHost PRIVATE uci_host_gui reckless_app_platform uci_host_warnings)

# The render tool: the main window drawn to a PNG with sample state, for the review images in docs/design/.
add_executable(uci_host_render_main RenderMain.cpp)
target_link_libraries(uci_host_render_main PRIVATE uci_host_gui uci_host_warnings)

# The identity the bundle carries, as the toolkit's bundle helpers read it. Its icon is the host's own, the black queen
# on a white square, rendered for every size by tools/make_icon.py into assets/icon/ and committed - so the toolkit's
# reckless_bundle_icon, which scales one picture down at build time, is not used.
set(UCI_HOST_ICONS "${PROJECT_SOURCE_DIR}/assets/icon")
set(UCI_HOST_COMPANY "Reckless Technology")
set(UCI_HOST_DISPLAY "Reckless UCI Host")
set(UCI_HOST_COPYRIGHT "Copyright (C) 2026 Jonny Reckless")
set(RECKLESS_VERSION "${UCI_HOST_VERSION}")
set(RECKLESS_VERSION_MAJOR "${UCI_HOST_VERSION_MAJOR}")
set(RECKLESS_VERSION_MINOR "${UCI_HOST_VERSION_MINOR}")
set(RECKLESS_VERSION_BUILD "${UCI_HOST_VERSION_BUILD}")

if(APPLE)
    # The bundle's plist: the host's own (Info.plist.in beside this file), which asks for nothing the host does not
    # use - no microphone - with the host's Icon.icns, which the plist names, and the PkgInfo of an app (APPL, signature
    # ????) beside it, and an ad-hoc signature over the whole.
    set(BUNDLE_EXECUTABLE RecklessUciHost)
    set(BUNDLE_IDENTIFIER com.recklesstechnology.ucihost)
    set(DISPLAY "${UCI_HOST_DISPLAY}")
    set(VERSION "${UCI_HOST_VERSION}")
    set(COPYRIGHT "${UCI_HOST_COPYRIGHT}")
    set(plist "${CMAKE_CURRENT_BINARY_DIR}/RecklessUciHost_Info.plist")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in" "${plist}" @ONLY)
    set_target_properties(RecklessUciHost PROPERTIES MACOSX_BUNDLE TRUE MACOSX_BUNDLE_INFO_PLIST "${plist}")
    set(pkginfo "${CMAKE_CURRENT_BINARY_DIR}/RecklessUciHost/PkgInfo")
    file(CONFIGURE OUTPUT "${pkginfo}" CONTENT "APPL????")
    target_sources(RecklessUciHost PRIVATE "${UCI_HOST_ICONS}/Icon.icns" "${pkginfo}")
    set_source_files_properties("${UCI_HOST_ICONS}/Icon.icns" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_source_files_properties("${pkginfo}" PROPERTIES MACOSX_PACKAGE_LOCATION ".")
    reckless_sign_bundle(RecklessUciHost "$<TARGET_BUNDLE_DIR:RecklessUciHost>")
elseif(WIN32)
    # A window of its own, no console; main() as its entry, as the toolkit's platform expects. The version resource
    # and the icon Explorer and the task bar show.
    set_target_properties(RecklessUciHost PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_link_options(RecklessUciHost PRIVATE "/ENTRY:mainCRTStartup")
    endif()
    reckless_windows_version_resource(RecklessUciHost UCI_HOST APP RecklessUciHost.exe "${UCI_HOST_ICONS}/Icon.ico")
endif()

if(UCI_HOST_BUILD_TESTS)
    # The app's own launch: its window opened, a few frames drawn, closed. On a display (label window): Linux runs it
    # under xvfb-run, macOS on its window server, Windows on whatever window station the runner has.
    add_test(NAME app_smoke COMMAND RecklessUciHost --smoke)
    set_tests_properties(app_smoke PROPERTIES LABELS window RESOURCE_LOCK display TIMEOUT 120)
endif()
