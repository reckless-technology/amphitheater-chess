# UCI engines: a child process with its lines read on threads of its own, the protocol's text, and the engine session.
add_library(uci_host_uci STATIC
    Engine.cpp
    Process.cpp
    ProcessPosix.cpp
    ProcessWindows.cpp
    UciProtocol.cpp)
target_include_directories(uci_host_uci PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
find_package(Threads REQUIRED)
target_link_libraries(uci_host_uci PUBLIC uci_host_chess Threads::Threads PRIVATE uci_host_warnings)
