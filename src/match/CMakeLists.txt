# Games, matches and tournaments between engines: players, the referee, adjudication, openings, the schedule, the
# statistics and the tournament's worker threads.
add_library(uci_host_match STATIC
    Adjudicator.cpp
    EnginePlayer.cpp
    GameRunner.cpp
    Openings.cpp
    Schedule.cpp
    Statistics.cpp
    Tournament.cpp)
target_include_directories(uci_host_match PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(uci_host_match PUBLIC uci_host_uci PRIVATE uci_host_warnings)
