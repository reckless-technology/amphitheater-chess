#pragma once
/// @file TextBook.h Opening books in the Reckless engines' text format (.book).
///
/// A book is a file of lines, one opening to a line, each a sequence of coordinate moves replayed from the standard
/// start position: `e2e4 e7e5 g1f3`. A `#` begins a comment that runs to the end of the line, so a file carries its own
/// provenance above its moves; a token beginning with a digit is a move number (`1.`) and names no move; and a blank
/// line is nothing. The format is `pawnstar`'s, read there by `OpeningBook::ParseLineOfPlay`, and the same bytes are
/// shipped by `pawnstar-rs` and `pawnstar-go`.
///
/// **A move may carry a trailing `?`**, which is the one part of the format worth explaining. Such a move is
/// *questionable*: it is replayed, so the rest of its line still parses and the engine keeps its reply to it, but it is
/// never chosen. That is how a book holds an answer to a move the engine would decline to play itself - the opponent
/// may well play it - and it is why a weak move is marked rather than deleted. Deleting it would take the whole
/// continuation with it.
///
/// A move's weight is **the number of lines that pass through it**, not a game count: the generator emits one line per
/// pruned leaf, so a popular move is reached by many lines and the uniform choice among a position's stored moves is
/// frequency-weighted by construction. Nothing in the file says a weight.
///
/// This reader keeps a line's structure - its move numbers, its comment, and whether it had any moves at all - so that
/// a book read and written back is the same file, byte for byte. That is not a nicety: a tool that screens a book
/// rewrites it in place, and a diff that shows every line changed hides the lines it meant to change.

#include "Move.h"
#include "Position.h"
#include "Result.h"

#include <cstddef>
#include <iosfwd>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace amphitheater::chess
{

/** One move token of a book line. */
struct TextBookMove
{
    std::string coordinate;              ///< The move in coordinate notation, without any `?`.
    bool        is_questionable = false; ///< Whether it carried a trailing `?`: replayed, never chosen.
};

/** One line of a book: an opening, a comment, or both. */
struct TextBookLine
{
    std::vector<std::string>  tokens;  ///< Every token in order, moves and move numbers alike, as written.
    std::vector<TextBookMove> moves;   ///< Just the moves, in order, with their `?` separated from the coordinate.
    std::string               comment; ///< The `#` and everything after it, verbatim, or empty.

    /** Whether this line names any move at all; false for a blank line or a comment on its own. */
    bool HasMoves() const
    {
        return !moves.empty();
    }
};

/** A book as read: every line in file order, including the comments. */
struct TextBook
{
    std::vector<TextBookLine> lines; ///< In file order. A comment-only line is a line with no moves.

    /** How many lines name at least one move. */
    std::size_t OpeningCount() const;

    /** How many moves carry a `?`, counted as tokens rather than as distinct moves. */
    std::size_t QuestionableTokenCount() const;
};

/**
 * Read a book from text.
 *
 * Nothing here is validated against the rules of chess: a token's legality depends on the position it is played from,
 * which is `ReplayTextBook`'s business, and a reader that refused an illegal move could not round-trip a file in order
 * to report what is wrong with it.
 *
 * @param text The whole file.
 * @return The book.
 */
TextBook ReadTextBook(std::string_view text);

/**
 * Write a book back out.
 *
 * Lines are joined with single spaces and terminated with a single `\n`, whatever the platform: the engines ship these
 * bytes in three repositories and check that all three agree, so a CRLF written on Windows would make the same book
 * differ by the machine that last touched it. A line keeps its own tokens and its own comment.
 *
 * @param book The book.
 * @return The file's text.
 */
std::string WriteTextBook(const TextBook &book);

/** What replaying a book found: the moves available at each position it reaches. */
struct TextBookIndex
{
    /// Every position the book reaches, by Polyglot key, and the moves stored there. A position appears once however
    /// many lines or move orders arrive at it.
    struct Entry
    {
        std::vector<Move> selectable;   ///< Moves the engine may choose here.
        std::vector<Move> questionable; ///< Moves it recognises and replays but never chooses.
    };

    std::map<std::uint64_t, Entry> positions;    ///< Keyed as PolyglotKey keys a position.
    std::vector<std::string>       errors;       ///< One per line that did not replay, naming the line and the move.
    std::size_t                    replayed = 0; ///< How many lines replayed in full.
};

/**
 * Replay every line of a book from the start position, as the engines' loaders do.
 *
 * @param book The book.
 * @return What each position holds, and a message for every line that did not replay.
 */
TextBookIndex ReplayTextBook(const TextBook &book);

} // namespace amphitheater::chess
