#pragma once
/// @file Openings.h Opening books for matches: positions from an EPD file, or move sequences from a PGN file.
///
/// An EPD line is a FEN's four position fields followed by operations ("hmvc 0; fmvn 1; id \"Ruy Lopez\";"); a
/// line of six FEN fields reads too. Blank lines and lines starting with '#' are skipped. A PGN game gives its
/// starting position and its main line, cut to a number of plies when asked; its name is its Opening tag, else its
/// ECO tag, else its Event tag.

#include "GameRunner.h"
#include "chess/PolyglotBook.h"
#include "chess/Result.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace uci_host::match
{

/** The opening file formats. */
enum class OpeningFormat
{
    kEpd,      ///< One position a line.
    kPgn,      ///< Games, their main lines played.
    kPolyglot, ///< A Polyglot book (.bin), walked from the starting position.
};

/** The order openings are used in. */
enum class OpeningOrder
{
    kSequential, ///< As the file has them, from the start offset, wrapping round.
    kRandom,     ///< Shuffled once, by the seed.
};

/**
 * Read an EPD line as an opening.
 * @param line The line.
 * @return The opening (a position and no moves), or why the line is not one.
 */
Result<Opening> ParseEpdLine(std::string_view line);

/**
 * Read openings from text.
 * @param text The file's text.
 * @param format Its format.
 * @param plies For PGN, the most moves of each game to keep; 0 for all.
 * @param errors Lines or games skipped, and why, appended (may be null).
 * @return The openings.
 */
std::vector<Opening> ParseOpenings(std::string_view text, OpeningFormat format, int plies,
                                   std::vector<std::string> *errors = nullptr);

/**
 * Read openings from a file.
 * @param path The file, UTF-8.
 * @param format Its format.
 * @param plies For PGN, the most moves of each game to keep; 0 for all.
 * @return The openings (at least one), or why there are none.
 */
Result<std::vector<Opening>> LoadOpenings(const std::string &path, OpeningFormat format, int plies);

/**
 * An opening walked out of a Polyglot book: from the starting position, a weighted-random book move a ply until the
 * book has none or the line is long enough.
 * @param book The book.
 * @param plies The longest line, 1 or more.
 * @param seed The random walk's seed: one seed, one line, on every platform.
 * @return The opening (the starting position and the line's moves).
 */
Opening PolyglotOpening(const chess::PolyglotBook &book, int plies, std::uint64_t seed);

/**
 * Put openings in the order to use them.
 * @param openings The openings, reordered.
 * @param order The order.
 * @param seed The random order's seed.
 * @param start For the sequential order, the opening to start from (counting from 0, wrapping).
 */
void OrderOpenings(std::vector<Opening> &openings, OpeningOrder order, std::uint64_t seed, int start = 0);

/**
 * Read a file whole.
 * @param path The file, UTF-8.
 * @return Its bytes, or why it could not be read.
 */
Result<std::string> ReadTextFile(const std::string &path);

} // namespace uci_host::match
