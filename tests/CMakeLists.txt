# The suites, one GoogleTest binary per library, each a CTest of its own.
include(GoogleTest)

# A suite binary: its sources, the libraries it tests, and the test data directory.
function(uci_host_suite name)
    cmake_parse_arguments(SUITE "" "" "SOURCES;LIBRARIES" ${ARGN})
    add_executable(${name} ${SUITE_SOURCES})
    target_link_libraries(${name} PRIVATE ${SUITE_LIBRARIES} GTest::gtest_main uci_host_warnings)
    target_compile_definitions(${name} PRIVATE UCI_HOST_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
endfunction()

uci_host_suite(uci_host_chess_test
    SOURCES chess/PerftTest.cpp chess/FenTest.cpp chess/NotationTest.cpp chess/GameTest.cpp chess/PgnTest.cpp
            chess/TimeControlTest.cpp chess/PolyglotTest.cpp
    LIBRARIES uci_host_chess)
add_test(NAME chess COMMAND uci_host_chess_test)

# The fake engine the UCI, match and command-line suites play against (tests/fake_engine/FakeEngine.cpp).
add_executable(uci_host_fake_engine fake_engine/FakeEngine.cpp)
target_link_libraries(uci_host_fake_engine PRIVATE uci_host_uci uci_host_warnings)

uci_host_suite(uci_host_uci_test
    SOURCES uci/UciProtocolTest.cpp uci/EngineTest.cpp
    LIBRARIES uci_host_uci)
target_compile_definitions(uci_host_uci_test PRIVATE UCI_HOST_FAKE_ENGINE="$<TARGET_FILE:uci_host_fake_engine>")
add_dependencies(uci_host_uci_test uci_host_fake_engine)
add_test(NAME uci COMMAND uci_host_uci_test)

uci_host_suite(uci_host_match_test
    SOURCES match/GameRunnerTest.cpp match/TournamentTest.cpp match/PonderAndBookTest.cpp
    LIBRARIES uci_host_match)
target_compile_definitions(uci_host_match_test PRIVATE UCI_HOST_FAKE_ENGINE="$<TARGET_FILE:uci_host_fake_engine>")
add_dependencies(uci_host_match_test uci_host_fake_engine)
add_test(NAME match COMMAND uci_host_match_test)

uci_host_suite(uci_host_cli_test
    SOURCES cli/CliTest.cpp
    LIBRARIES uci_host_cli_core)
target_compile_definitions(uci_host_cli_test PRIVATE UCI_HOST_FAKE_ENGINE="$<TARGET_FILE:uci_host_fake_engine>")
add_dependencies(uci_host_cli_test uci_host_fake_engine)
add_test(NAME cli COMMAND uci_host_cli_test)

# The executable itself, end to end: a perft and a short match between fake engines.
add_test(NAME cli_perft COMMAND uci-host-cli perft startpos 4)
set_tests_properties(cli_perft PROPERTIES PASS_REGULAR_EXPRESSION "Nodes: 197281")
add_test(NAME cli_match
         COMMAND uci-host-cli match -engine "cmd=$<TARGET_FILE:uci_host_fake_engine>" name=A
                 -engine "cmd=$<TARGET_FILE:uci_host_fake_engine>" name=B -each tc=5+0.05 -games 2 -maxmoves 20)
set_tests_properties(cli_match PROPERTIES PASS_REGULAR_EXPRESSION "Finished match")

# Against a real engine, when UCI_HOST_TEST_ENGINE names one; the suite skips itself otherwise.
uci_host_suite(uci_host_integration_test
    SOURCES integration/RealEngineTest.cpp
    LIBRARIES uci_host_match)
add_test(NAME integration COMMAND uci_host_integration_test)

# The window's views and controller, with no window: the board, the lists, the graph, the controller and the window
# drawn through Skia (its review image written where RECKLESS_SCREENSHOT_DIR names).
if(UCI_HOST_GUI)
    uci_host_suite(uci_host_window_test
        SOURCES app/WindowTest.cpp app/PlayTest.cpp app/BundledEnginesTest.cpp app/CatalogueTest.cpp app/ControlsTest.cpp app/AnalysisTest.cpp app/EngineManagerTest.cpp app/FilesTest.cpp app/MenusTest.cpp app/CompetitionTest.cpp app/WindowSizesTest.cpp app/AboutTest.cpp app/EvaluationsTest.cpp
        LIBRARIES uci_host_gui reckless_toolkit_test_support)
    target_compile_definitions(uci_host_window_test PRIVATE UCI_HOST_FAKE_ENGINE="$<TARGET_FILE:uci_host_fake_engine>")
    add_dependencies(uci_host_window_test uci_host_fake_engine)
    add_test(NAME gui COMMAND uci_host_window_test)
endif()

# The render tool end to end, with no display: the window with the mockup's state, and with each dialog open.
if(UCI_HOST_GUI)
    add_test(NAME render_main COMMAND uci_host_render_main "${CMAKE_CURRENT_BINARY_DIR}/render-main.png" 1 1240 1000)
    foreach(view flipped newgame engines tournament standings about)
        add_test(NAME render_${view}
                 COMMAND uci_host_render_main "${CMAKE_CURRENT_BINARY_DIR}/render-${view}.png" 1 1240 1000 ${view})
    endforeach()
    set_tests_properties(render_main render_flipped render_newgame render_engines render_tournament
                         render_standings render_about
                         PROPERTIES PASS_REGULAR_EXPRESSION "1240 x 1000")
endif()

# The bundled engines' pins and packaging (tools/bundled_engines.py), in Python's own unittest, against the fake engine.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME packaging COMMAND Python3::Interpreter -I "${CMAKE_CURRENT_SOURCE_DIR}/packaging/test_bundled_engines.py")
    set_tests_properties(packaging PROPERTIES ENVIRONMENT "UCI_HOST_FAKE_ENGINE=$<TARGET_FILE:uci_host_fake_engine>")
endif()

# The app's icon as tools/make_icon.py renders it from Jonny's artwork (assets/art/): the committed files held to the
# script. It skips (77) where the Python lacks the pinned resvg-py and Pillow (tools/requirements.txt), which the setup
# scripts' .venv and CI's venvs carry.
if(Python3_Interpreter_FOUND)
    add_test(NAME icon COMMAND Python3::Interpreter -I "${PROJECT_SOURCE_DIR}/tools/make_icon.py" --check)
    set_tests_properties(icon PROPERTIES SKIP_RETURN_CODE 77)
endif()

# THIRD-PARTY-NOTICES.txt as tools/third_party_notices.py writes it from this tree's fetches and the engines' pins.
if(UCI_HOST_GUI AND Python3_Interpreter_FOUND)
    add_test(NAME notices
             COMMAND Python3::Interpreter -I "${PROJECT_SOURCE_DIR}/tools/third_party_notices.py" "${CMAKE_BINARY_DIR}" --check)
endif()
