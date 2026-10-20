#pragma once
/// @file Pgn.h Portable Game Notation: games as tags and movetext, read from text and written back.
///
/// The reader takes a file of any number of games and is tolerant of what real files hold: comments in braces and
/// after semicolons, NAGs ($n) and the suffix annotations (!, ?, !!, ??, !?, ?!) as NAGs, recursive variations, move
/// numbers with or without a space after them, escape lines starting with '%', a missing result, and SAN written
/// loosely (ParseSan). Every move, the variations' included, is replayed and checked; a game with an illegal or
/// unreadable move is skipped with an error naming it, and reading goes on with the next game.
///
/// The writer puts the Seven Tag Roster first, in its order, then the other tags as they were added; escapes quotes
/// and backslashes in tag values; and wraps movetext so no line passes 80 characters.

#include "Game.h"
#include "Move.h"
#include "Position.h"
#include "Result.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace amphitheater::chess
{

struct PgnMove;

/** A line of moves: the main line, or a variation. */
using PgnLine = std::vector<PgnMove>;

/** A move in a PGN game, with what was written around it. */
struct PgnMove
{
    Move                 move = Move::None(); ///< The move.
    std::string          san;                 ///< The move as the writer prints it (re-made from the move).
    std::vector<int>     nags;                ///< Numeric annotation glyphs after it.
    std::string          comment_before;      ///< A comment before the move (at the start of a line only).
    std::string          comment;             ///< A comment after the move.
    std::vector<PgnLine> variations;          ///< Alternatives to this move, each from the position before it.
};

/** One game: tags, movetext and result. */
struct PgnGame
{
    std::vector<std::pair<std::string, std::string>> tags;  ///< The tags, in order.
    Position                                         start; ///< The position before the first move.
    PgnLine                                          moves; ///< The main line.
    std::string comment;                    ///< A comment with no move after it (in a game with no moves).
    GameResult  result = GameResult::kNone; ///< The result the movetext ends with (or the Result tag's).

    PgnGame();

    /**
     * A tag's value.
     * @param name The tag's name, matched exactly.
     * @return The value, or an empty string when there is no such tag.
     */
    std::string Tag(std::string_view name) const;

    /**
     * Set a tag, replacing its value where it exists and adding it at the end where it does not.
     * @param name The tag's name.
     * @param value Its value.
     */
    void SetTag(std::string_view name, std::string_view value);

    /** @return The main line as a Game, its outcome the result with no reason beyond it. */
    Game ToGame() const;

    /**
     * A PGN game from a game: its moves, its result, and FEN and SetUp tags when it starts elsewhere than the
     * starting position. The Seven Tag Roster is left for the caller to fill (the writer prints "?" for any missing).
     * @param game The game.
     * @return The PGN game.
     */
    static PgnGame FromGame(const Game &game);
};

/** Reads the games of a PGN text one at a time. */
class PgnReader
{
  public:
    /**
     * A reader over a text.
     * @param text The whole PGN text, which must outlive the reader.
     */
    explicit PgnReader(std::string_view text);

    /**
     * Read the next game.
     * @param game The game read, when there is one.
     * @param error Why a game was skipped, when one was ("game 3, line 41: 'Nf9' is not a legal move here").
     * @return True when a game was read or skipped (check error), false at the end of the text.
     */
    bool Next(PgnGame &game, std::string &error);

  private:
    /** @return Whether the text is used up. */
    bool AtEnd() const;

    /** Skip whitespace, and escape lines that start with '%'. */
    void SkipSpace();

    /**
     * Skip to the start of the next game: a '[' at the start of a line.
     */
    void SkipToNextGame();

    std::string_view text_;           ///< The text.
    std::size_t      offset_     = 0; ///< Where reading has reached.
    int              line_       = 1; ///< The line reading has reached, for errors.
    int              game_count_ = 0; ///< Games read or skipped so far.
};

/**
 * Read every game of a PGN text.
 * @param text The text.
 * @param errors Why each skipped game was skipped, appended (may be null).
 * @return The games read.
 */
std::vector<PgnGame> ReadPgn(std::string_view text, std::vector<std::string> *errors = nullptr);

/**
 * Write a game as PGN.
 * @param game The game.
 * @return The text: tags, a blank line, the movetext ending with the result, and a blank line after.
 */
std::string WritePgn(const PgnGame &game);

/**
 * Escape a tag value: a backslash before every quote and backslash.
 * @param value The value.
 * @return The escaped value.
 */
std::string EscapeTagValue(std::string_view value);

} // namespace amphitheater::chess
