#pragma once
/// @file PgnBook.h An opening book read from PGN: the moves a file's games play in their openings, each weighted by how
/// many games play it in that position, as a Polyglot book - so a PGN of games or of opening lines serves wherever a
/// .bin does.

#include "PolyglotBook.h"
#include "Result.h"

#include <string>
#include <string_view>
#include <vector>

namespace amphitheater::chess
{

/** How deep a PGN book reaches by default: the first twenty moves of each side. */
inline constexpr int kPgnBookPlies = 40;

/**
 * A book of a PGN text's openings: every move each game plays in its first plies - its variations' moves too - stored
 * under the position it is played in, its weight the number of games that play it there (at most 65,535). A game
 * counts once for a move however often its lines repeat it.
 * @param text The PGN text.
 * @param plies How many plies of each game the book takes.
 * @param errors Each game the reader could not read, added to (may be null).
 * @return The book, or why there is none: no game, or no move in the plies taken.
 */
Result<PolyglotBook> BookFromPgn(std::string_view text, int plies = kPgnBookPlies,
                                 std::vector<std::string> *errors = nullptr);

} // namespace amphitheater::chess
