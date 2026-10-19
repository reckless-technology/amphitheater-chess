# The window, Reckless UCI Host, on the toolkit: the app (RecklessUciHost) - a macOS .app, a Windows executable with its
# version resource and icon, a Linux binary - and the files it carries inside itself.

# The files the binary carries (the toolkit's reckless_module_assets): the series' serif, the knob and the company mark,
# the monospaced family the engines' output and the FEN are set in, the app's icon for a Linux window, and the twelve
# pieces (assets/pieces/staunton), each by the name pieces/<piece>.svg.
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
    "RecklessAppIcon256.png=${RECKLESS_TOOLKIT_DIR}/assets/logos/RecklessAppIcon256.png")

add_executable(RecklessUciHost Main.cpp)
target_link_libraries(RecklessUciHost PRIVATE reckless_app_platform reckless_gfx_skia uci_host_assets uci_host_version
                                              uci_host_warnings)

# The identity the bundle carries, as the toolkit's bundle helpers read it.
set(UCI_HOST_COMPANY "Reckless Technology")
set(UCI_HOST_DISPLAY "Reckless UCI Host")
set(UCI_HOST_COPYRIGHT "Copyright (C) 2026 Jonny Reckless")
set(RECKLESS_VERSION "${UCI_HOST_VERSION}")
set(RECKLESS_VERSION_MAJOR "${UCI_HOST_VERSION_MAJOR}")
set(RECKLESS_VERSION_MINOR "${UCI_HOST_VERSION_MINOR}")
set(RECKLESS_VERSION_BUILD "${UCI_HOST_VERSION_BUILD}")

if(APPLE)
    # The bundle's plist: the host's own (Info.plist.in beside this file), which asks for nothing the host does not
    # use - no microphone - with the toolkit's icon and PkgInfo beside it and an ad-hoc signature over the whole.
    set(BUNDLE_EXECUTABLE RecklessUciHost)
    set(BUNDLE_IDENTIFIER com.recklesstechnology.ucihost)
    set(DISPLAY "${UCI_HOST_DISPLAY}")
    set(VERSION "${UCI_HOST_VERSION}")
    set(COPYRIGHT "${UCI_HOST_COPYRIGHT}")
    set(plist "${CMAKE_CURRENT_BINARY_DIR}/RecklessUciHost_Info.plist")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in" "${plist}" @ONLY)
    set_target_properties(RecklessUciHost PROPERTIES MACOSX_BUNDLE TRUE MACOSX_BUNDLE_INFO_PLIST "${plist}")
    reckless_mac_bundle_files(RecklessUciHost APPL)
    reckless_sign_bundle(RecklessUciHost "$<TARGET_BUNDLE_DIR:RecklessUciHost>")
elseif(WIN32)
    # A window of its own, no console; main() as its entry, as the toolkit's platform expects. The version resource
    # and the icon Explorer and the task bar show.
    set_target_properties(RecklessUciHost PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_link_options(RecklessUciHost PRIVATE "/ENTRY:mainCRTStartup")
    endif()
    reckless_bundle_icon(RecklessUciHost Icon.ico uci_host_ico)
    reckless_windows_version_resource(RecklessUciHost UCI_HOST APP RecklessUciHost.exe "${uci_host_ico}")
endif()

if(UCI_HOST_BUILD_TESTS)
    # The app's own launch: its window opened, a few frames drawn, closed. On a display (label window): Linux runs it
    # under xvfb-run, macOS on its window server, Windows on whatever window station the runner has.
    add_test(NAME app_smoke COMMAND RecklessUciHost --smoke)
    set_tests_properties(app_smoke PROPERTIES LABELS window RESOURCE_LOCK display TIMEOUT 120)
endif()
