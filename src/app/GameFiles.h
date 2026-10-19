#pragma once
/// @file GameFiles.h Games to and from text for File and Edit: a PGN game's card, a game written as PGN with what is
/// known of it, a FEN or a PGN read from the clipboard.

#include "MainController.h"
#include "chess/Game.h"
#include "chess/Pgn.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace uci_host::app
{

/**
 * What the card says of a game read from PGN: its players, event, opening and time control from its tags.
 * @param game The game.
 * @return The card's facts.
 */
GameInfo InfoFromPgn(const chess::PgnGame &game);

/**
 * The game shown as PGN: the game it was read as - its tags, comments and variations - when its moves are still the
 * game's, else the game's moves with the Seven Tag Roster from what is known of it.
 * @param game The game.
 * @param info What the card says of it.
 * @param source The PGN it came from (a file, or a game played on the board), if any.
 * @param date Today's date, "2026.10.19", for a game that has none.
 * @return The PGN game.
 */
chess::PgnGame PgnOf(const chess::Game &game, const GameInfo &info, const std::optional<chess::PgnGame> &source,
                     const std::string &date);

/** @return Today's date as PGN writes it, "2026.10.19". */
std::string TodayInPgn();

/** What text from the clipboard holds. */
struct PastedGames
{
    std::optional<chess::Position> position;    ///< A FEN's position.
    std::vector<chess::PgnGame>    games;       ///< A PGN's games.
    std::size_t                    skipped = 0; ///< A PGN's games that could not be read.
};

/**
 * Read text as a FEN, else as PGN.
 * @param text The text.
 * @return What it holds; neither a position nor a game when it is neither.
 */
PastedGames ReadPasted(std::string_view text);

} // namespace uci_host::app
