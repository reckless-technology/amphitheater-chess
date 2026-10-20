# Games, matches and tournaments between engines: players, the referee, adjudication, openings, the schedule, the
# statistics and the tournament's worker threads.
add_library(amphitheater_match STATIC
    Adjudicator.cpp
    EnginePlayer.cpp
    GameAnalysis.cpp
    GameRunner.cpp
    Openings.cpp
    Schedule.cpp
    Statistics.cpp
    TestSuite.cpp
    Tournament.cpp)
target_include_directories(amphitheater_match PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(amphitheater_match PUBLIC amphitheater_uci PRIVATE amphitheater_warnings)
