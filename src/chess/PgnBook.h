#pragma once
/// @file PgnBook.h An opening book read from PGN: the moves games play in their openings, each weighted by how many
/// games play it in that position, as a Polyglot book - so a PGN of games or of opening lines serves wherever a .bin
/// does, and a library of games makes a book (match/BookMaker.h).

#include "PolyglotBook.h"
#include "Result.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace amphitheater::chess
{

/** How deep a PGN opened as a book reaches by default: the first twenty moves of each side. */
inline constexpr int kPgnBookPlies = 40;

/**
 * A book gathered from PGN texts, one after another: every move each game plays in its first plies - its variations'
 * moves too - counted under the position it is played in, a game counting once for a move however often its lines
 * repeat it.
 */
class PgnBookBuilder
{
  public:
    /** @param plies How many plies of each game the book takes. */
    explicit PgnBookBuilder(int plies = kPgnBookPlies) : plies_(plies)
    {
    }

    /**
     * Take in a PGN text's games.
     * @param text The text.
     * @param errors Each game the reader could not read, added to (may be null).
     * @return How many games it read.
     */
    int Add(std::string_view text, std::vector<std::string> *errors = nullptr);

    /** @return How many games have been taken in. */
    long long games() const
    {
        return games_;
    }

    /** @return How many different moves, each in its position, have been seen. */
    std::size_t move_count() const
    {
        return counts_.size();
    }

    /**
     * The book's entries: each move played by at least a number of games, its weight the number of games that play it
     * there - scaled down alike where the most would pass the format's 65,535, none falling below 1.
     * @param least_games The fewest games a move must be played in to be kept.
     * @return The entries, in no order.
     */
    std::vector<PolyglotEntry> Entries(int least_games = 1) const;

  private:
    int                                                          plies_;     ///< The plies of each game taken.
    long long                                                    games_ = 0; ///< The games taken in.
    std::map<std::pair<std::uint64_t, std::uint16_t>, long long> counts_;    ///< Games per position's key and move.
};

/**
 * A book of a PGN text's openings, as PgnBookBuilder gathers it.
 * @param text The PGN text.
 * @param plies How many plies of each game the book takes.
 * @param errors Each game the reader could not read, added to (may be null).
 * @return The book, or why there is none: no game, or no move in the plies taken.
 */
Result<PolyglotBook> BookFromPgn(std::string_view text, int plies = kPgnBookPlies,
                                 std::vector<std::string> *errors = nullptr);

} // namespace amphitheater::chess
