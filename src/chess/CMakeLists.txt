# The rules of chess: the board and its move generator (adapted from pawnstar), FEN, SAN and UCI notation, games and
# their endings, PGN, time controls and clocks. No threads, no I/O but what a caller hands in as text.
add_library(uci_host_chess STATIC
    Game.cpp
    Notation.cpp
    Pgn.cpp
    PolyglotBook.cpp
    PolyglotRandom.cpp
    Position.cpp
    TimeControl.cpp)
target_include_directories(uci_host_chess PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(uci_host_chess PRIVATE uci_host_warnings)
