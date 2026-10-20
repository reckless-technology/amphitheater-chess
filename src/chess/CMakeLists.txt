# The rules of chess: the board and its move generator (adapted from pawnstar), FEN, SAN and UCI notation, games and
# their endings, PGN, time controls and clocks. No threads, no I/O but what a caller hands in as text.
add_library(amphitheater_chess STATIC
    BoardSetup.cpp
    Game.cpp
    GameTree.cpp
    Notation.cpp
    Pgn.cpp
    PgnBook.cpp
    PolyglotBook.cpp
    PolyglotRandom.cpp
    Position.cpp
    TimeControl.cpp)
target_include_directories(amphitheater_chess PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(amphitheater_chess PRIVATE amphitheater_warnings)
