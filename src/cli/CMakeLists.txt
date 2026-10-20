# The command line: the runner as a library the tests call in-process, and the executable round it.
add_library(amphitheater_cli_core STATIC Cli.cpp)
target_include_directories(amphitheater_cli_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(amphitheater_cli_core PUBLIC amphitheater_match amphitheater_version PRIVATE amphitheater_warnings)

add_executable(amphitheater-cli main.cpp)
target_link_libraries(amphitheater-cli PRIVATE amphitheater_cli_core amphitheater_warnings)
