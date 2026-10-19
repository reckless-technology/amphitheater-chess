# The command line: the runner as a library the tests call in-process, and the executable round it.
add_library(uci_host_cli_core STATIC Cli.cpp)
target_include_directories(uci_host_cli_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(uci_host_cli_core PUBLIC uci_host_match uci_host_version PRIVATE uci_host_warnings)

add_executable(uci-host-cli main.cpp)
target_link_libraries(uci-host-cli PRIVATE uci_host_cli_core uci_host_warnings)
