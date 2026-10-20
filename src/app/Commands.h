#pragma once

/// @file Commands.h What the menus, the buttons and the keys ask the window to do, by number.

namespace amphitheater::app
{

/** A command, as a menu item's id. Zero is no command (a dismissed menu). */
enum Command : int
{
    kNoCommand = 0, ///< Nothing: a menu dismissed.

    kNewGame = 100, ///< Game > New game...: the players, the time control, from a position or the start.
    kOpenPgn,       ///< File > Open PGN...: a file's games, one picked.
    kSavePgn,       ///< File > Save PGN...: the game shown, written to a file.
    kOpenBook,      ///< File > Open book...: a Polyglot book or a PGN, its moves for the position shown under the card.
    kUseBuiltInBook, ///< File > Built-in book: Zenith's own, which the window carries.
    kCloseBook,      ///< File > Close book.
    kQuit,           ///< File > Quit.

    kCopyFen = 200, ///< Edit > Copy FEN.
    kCopyPgn,       ///< Edit > Copy PGN.
    kPasteFen,      ///< Edit > Paste FEN or PGN: a position, or a game, from the clipboard.
    kSetUpPosition, ///< Edit > Set up position...: pieces put on a board, the position shown from there.

    kTakeBack = 300,   ///< Game > Take back: the person's last move and the engine's reply.
    kStopGame,         ///< Game > Stop the game: abandoned, kept on the board.
    kFirst,            ///< Game > First position.
    kBack,             ///< Game > Back a move.
    kForward,          ///< Game > Forward a move.
    kLast,             ///< Game > Last position.
    kFlip,             ///< View > Flip the board.
    kAnalyse,          ///< Engines > Analyse: the selected engines think about the position shown.
    kPromoteVariation, ///< Game > Promote variation: the variation shown made its line's main continuation.
    kDeleteVariation,  ///< Game > Delete variation: the variation shown taken off the game.
    kAnalyseGame,      ///< Game > Analyse the game...: an engine over every position of the line shown.
    kStopGameAnalysis, ///< Game > Stop the game analysis.

    kEngineManager = 400, ///< Engines > Manage engines...: add, remove and configure engines.
    kTestSuite,           ///< Engines > Test suite...: an EPD suite's positions against an engine.

    kNewMatch = 500, ///< Match > New match...: two engines, many games.
    kStopMatch,      ///< Match > Stop the match.

    kNewTournament = 600, ///< Tournament > New tournament...: a round robin.
    kStopTournament,      ///< Tournament > Stop the tournament.
    kShowStandings,       ///< Tournament > Standings and crosstable.

    kClearLog     = 700, ///< View > Clear the engine log.
    kShowLog      = 701, ///< View > Engine log: shown, or folded away to its header.
    kPiecesBase   = 730, ///< View > Pieces > a set: this plus the set's index in kPieceSets.
    kWindowSize50 = 710, ///< View > 50%: the window at half the layout's design size.
    kWindowSize75,       ///< View > 75%.
    kWindowSize100,      ///< View > 100%: the design size.
    kWindowSize125,      ///< View > 125%.
    kWindowSize150,      ///< View > 150%.
    kWindowLarger,       ///< View > Larger: the next size up.
    kWindowSmaller,      ///< View > Smaller: the next size down.

    kAbout         = 800,   ///< Help > About Reckless Amphitheater.
    kWatchGameBase = 10000, ///< Match and Tournament > Watch game n: this plus the game's number.
};

} // namespace amphitheater::app
