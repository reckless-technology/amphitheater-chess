# The window, Reckless Amphitheater, on the toolkit: the app (Amphitheater) - a macOS .app, a Windows executable with its
# version resource and icon, a Linux binary - and the files it carries inside itself.

# The files the binary carries (the toolkit's reckless_module_assets): the series' serif, the knob and the company mark,
# the monospaced family the engines' output and the FEN are set in, the app's icon for a Linux window (its 256-pixel
# render, icon-256.png, which Main.cpp hands the platform as _NET_WM_ICON and Help > About draws as its mark), and the twelve pieces
# (assets/pieces/staunton), each by the name pieces/<piece>.svg.
reckless_monospace_font_files(amphitheater_monospace)
set(amphitheater_pieces "")
foreach(colour w b)
    foreach(kind K Q R B N P)
        foreach(set cburnett staunton)
            list(APPEND amphitheater_pieces
                 "pieces/${set}/${colour}${kind}.svg=${PROJECT_SOURCE_DIR}/assets/pieces/${set}/${colour}${kind}.svg")
        endforeach()
    endforeach()
endforeach()
reckless_module_assets(amphitheater_assets
    ${amphitheater_monospace}
    ${amphitheater_pieces}
    "icon-256.png=${PROJECT_SOURCE_DIR}/assets/icon/png/icon-256.png")

# The window's views and controllers, which the app, the render tool and the window's suite link.
add_library(amphitheater_gui STATIC
    AboutDialog.cpp
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
    Evaluations.cpp
    GameAnalysisDialog.cpp
    GameFiles.cpp
    GamePickerDialog.cpp
    LayoutSettings.cpp
    MainController.cpp
    MainView.cpp
    Menus.cpp
    MovesList.cpp
    NewGameDialog.cpp
    Panels.cpp
    PieceSet.cpp
    PlaySession.cpp
    PositionEditor.cpp
    SearchTracker.cpp
    SettingsFolder.cpp
    Showcase.cpp
    StandingsDialog.cpp
    WindowSizes.cpp)
target_include_directories(amphitheater_gui PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(amphitheater_gui
    PUBLIC  amphitheater_match amphitheater_cli_core amphitheater_version amphitheater_assets reckless_app_shell reckless_common_panel reckless_gfx_skia
    PRIVATE amphitheater_warnings)

add_executable(Amphitheater Main.cpp)
target_link_libraries(Amphitheater PRIVATE amphitheater_gui reckless_app_platform amphitheater_warnings)
# On Linux the program is named as Linux programs are, in lower case: /opt/reckless-technology/bin/amphitheater.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(Amphitheater PROPERTIES OUTPUT_NAME amphitheater)
endif()

# The render tool: the main window drawn to a PNG with sample state, for the review images in docs/design/.
add_executable(amphitheater_render_main RenderMain.cpp)
target_link_libraries(amphitheater_render_main PRIVATE amphitheater_gui amphitheater_warnings)

# The identity the bundle carries, as the toolkit's bundle helpers read it. Its icon is the host's own, Jonny's sketch
# of the Colosseum on a rounded square, rendered for every size by tools/make_icon.py into assets/icon/ and committed -
# so the toolkit's reckless_bundle_icon, which scales one picture down at build time, is not used.
set(AMPHITHEATER_ICONS "${PROJECT_SOURCE_DIR}/assets/icon")
set(AMPHITHEATER_COMPANY "Reckless Technology")
set(AMPHITHEATER_DISPLAY "Reckless Amphitheater")
set(AMPHITHEATER_COPYRIGHT "Copyright (C) 2026 Jonny Reckless")
set(RECKLESS_VERSION "${AMPHITHEATER_VERSION}")
set(RECKLESS_VERSION_MAJOR "${AMPHITHEATER_VERSION_MAJOR}")
set(RECKLESS_VERSION_MINOR "${AMPHITHEATER_VERSION_MINOR}")
set(RECKLESS_VERSION_BUILD "${AMPHITHEATER_VERSION_BUILD}")

if(APPLE)
    # The bundle's plist: the host's own (Info.plist.in beside this file), which asks for nothing the host does not
    # use - no microphone - with the host's Icon.icns, which the plist names, and the PkgInfo of an app (APPL, signature
    # ????) beside it, and an ad-hoc signature over the whole.
    set(BUNDLE_EXECUTABLE Amphitheater)
    set(BUNDLE_IDENTIFIER com.recklesstechnology.amphitheater)
    set(DISPLAY "${AMPHITHEATER_DISPLAY}")
    set(VERSION "${AMPHITHEATER_VERSION}")
    set(COPYRIGHT "${AMPHITHEATER_COPYRIGHT}")
    set(plist "${CMAKE_CURRENT_BINARY_DIR}/Amphitheater_Info.plist")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in" "${plist}" @ONLY)
    set_target_properties(Amphitheater PROPERTIES MACOSX_BUNDLE TRUE MACOSX_BUNDLE_INFO_PLIST "${plist}")
    set(pkginfo "${CMAKE_CURRENT_BINARY_DIR}/Amphitheater/PkgInfo")
    file(CONFIGURE OUTPUT "${pkginfo}" CONTENT "APPL????")
    target_sources(Amphitheater PRIVATE "${AMPHITHEATER_ICONS}/Icon.icns" "${pkginfo}")
    set_source_files_properties("${AMPHITHEATER_ICONS}/Icon.icns" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_source_files_properties("${pkginfo}" PROPERTIES MACOSX_PACKAGE_LOCATION ".")
    reckless_sign_bundle(Amphitheater "$<TARGET_BUNDLE_DIR:Amphitheater>")
elseif(WIN32)
    # A window of its own, no console; main() as its entry, as the toolkit's platform expects. The version resource
    # and the icon Explorer and the task bar show.
    set_target_properties(Amphitheater PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_link_options(Amphitheater PRIVATE "/ENTRY:mainCRTStartup")
    endif()
    reckless_windows_version_resource(Amphitheater AMPHITHEATER APP Amphitheater.exe "${AMPHITHEATER_ICONS}/Icon.ico")
endif()

if(AMPHITHEATER_BUILD_TESTS)
    # The app's own launch: its window opened, a few frames drawn, closed. On a display (label window): Linux runs it
    # under xvfb-run, macOS on its window server, Windows on whatever window station the runner has.
    add_test(NAME app_smoke COMMAND Amphitheater --smoke)
    set_tests_properties(app_smoke PROPERTIES LABELS window RESOURCE_LOCK display TIMEOUT 120)
endif()
