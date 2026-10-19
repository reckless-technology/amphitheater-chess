#pragma once

/// @file Commands.h What the menus, the buttons and the keys ask the window to do, by number.

namespace uci_host::app
{

/** A command, as a menu item's id. Zero is no command (a dismissed menu). */
enum Command : int
{
    kNoCommand = 0, ///< Nothing: a menu dismissed.

    kNewGame = 100, ///< Game > New game...: the players, the time control, from a position or the start.
    kOpenPgn,       ///< File > Open PGN...: a file's games, one picked.
    kSavePgn,       ///< File > Save PGN...: the game shown, written to a file.
    kQuit,          ///< File > Quit.

    kCopyFen = 200, ///< Edit > Copy FEN.
    kCopyPgn,       ///< Edit > Copy PGN.
    kPasteFen,      ///< Edit > Paste FEN or PGN: a position, or a game, from the clipboard.

    kTakeBack = 300, ///< Game > Take back: the person's last move and the engine's reply.
    kStopGame,       ///< Game > Stop the game: abandoned, kept on the board.
    kFirst,          ///< Game > First position.
    kBack,           ///< Game > Back a move.
    kForward,        ///< Game > Forward a move.
    kLast,           ///< Game > Last position.
    kFlip,           ///< View > Flip the board.
    kAnalyse,        ///< Engines > Analyse: the selected engines think about the position shown.

    kEngineManager = 400, ///< Engines > Manage engines...: add, remove and configure engines.

    kNewMatch = 500, ///< Match > New match...: two engines, many games.
    kStopMatch,      ///< Match > Stop the match.

    kNewTournament = 600, ///< Tournament > New tournament...: a round robin.
    kStopTournament,      ///< Tournament > Stop the tournament.
    kShowStandings,       ///< Tournament > Standings and crosstable.

    kClearLog = 700, ///< View > Clear the engine log.

    kAbout         = 800,   ///< Help > About Reckless UCI Host.
    kWatchGameBase = 10000, ///< Match and Tournament > Watch game n: this plus the game's number.
};

} // namespace uci_host::app
