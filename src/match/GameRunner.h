#pragma once
/// @file GameRunner.h One game between two players, refereed: the clocks, the rules, the adjudication, the record.
///
/// The runner owns the game. A side with its own book has the book's move played for it, unclocked and marked "book",
/// until the book has none or its depth is reached; after that it asks the side to move for its move with a deadline -
/// the flag's fall plus the margin under a clock, the stall timeout without one - charges the time to the side's clock,
/// and ends the game for any of: the rules (mate, stalemate, insufficient material, threefold repetition, fifty moves),
/// a flag (a draw when the opponent could never mate, as the rules have it), an illegal or unreadable move, a crash or
/// a stall, an adjudication, or a cancellation. Every move's score, depth and time is kept for the PGN's comments.

#include "Adjudicator.h"
#include "Player.h"
#include "chess/Game.h"
#include "chess/Pgn.h"
#include "chess/PolyglotBook.h"
#include "chess/TimeControl.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace uci_host::match
{

/** Where a game starts: a position and moves played from it before the players take over. */
struct Opening
{
    std::string              fen;   ///< The starting position; empty for the standard one.
    std::vector<std::string> moves; ///< Moves from it, in UCI notation, not charged to any clock.
    std::string              name;  ///< What to call it, if anything (an EPD's id, a PGN's Opening tag).
};

/** A side's own opening book: the host plays its moves for the side while the book has one. */
struct BookSettings
{
    std::shared_ptr<const chess::PolyglotBook> book;      ///< The book; none for no book.
    int                                        depth = 0; ///< Book moves only before this game ply; 0 for no limit.
    chess::BookSelection selection                   = chess::BookSelection::kWeightedRandom; ///< How a move is chosen.
    std::uint64_t        seed                        = 1; ///< The random choice's seed, so a game can be played again.
};

/** How a game is to be played. */
struct GameSettings
{
    Opening              opening;              ///< Where it starts.
    chess::TimeControl   white_time_control;   ///< White's time control.
    chess::TimeControl   black_time_control;   ///< Black's time control.
    chess::Milliseconds  time_margin{0};       ///< How far past its time a side may go before its flag falls.
    chess::Milliseconds  stall_timeout{60000}; ///< How long to wait for a move with no clock to say when.
    AdjudicationSettings adjudication;         ///< When to end it early.
    BookSettings         white_book;           ///< White's own book.
    BookSettings         black_book;           ///< Black's own book.
    std::string          event = "?";          ///< PGN's Event.
    std::string          site  = "?";          ///< PGN's Site.
    std::string          round = "?";          ///< PGN's Round.
};

/** What was said about one move, for its PGN comment. */
struct MoveNote
{
    std::optional<uci::Score> score;           ///< The mover's score, from its side.
    std::optional<int>        depth;           ///< The depth it reached.
    chess::Milliseconds       elapsed;         ///< How long it took.
    bool                      is_book = false; ///< Whether it was an opening move, played for the side.

    /**
     * The comment: "+0.35/18 1.2s", a mate as "+M5" or "-M3", "book" for an opening move.
     * @return The text.
     */
    std::string Comment() const;
};

/** A finished game. */
struct GameRecord
{
    chess::Game                           game;       ///< The game, its outcome set.
    std::string                           white_name; ///< White's name.
    std::string                           black_name; ///< Black's name.
    GameSettings                          settings;   ///< How it was played.
    std::vector<MoveNote>                 notes;      ///< One per move, the opening's included.
    std::chrono::system_clock::time_point started;    ///< When it started.

    /**
     * The game as PGN: the Seven Tag Roster, TimeControl (and WhiteTimeControl and BlackTimeControl when they
     * differ), Termination, PlyCount, Opening, FEN and SetUp from another position; each move's note as its comment,
     * and the outcome in words after the last move.
     * @return The PGN game.
     */
    chess::PgnGame ToPgn() const;
};

/** What the runner reports as a game goes on, on the game's thread. */
struct GameProgress
{
    const chess::Game  &game;        ///< The game, after the move.
    const MoveNote     &note;        ///< What was said about the move.
    const chess::Clock &white_clock; ///< White's clock after it.
    const chess::Clock &black_clock; ///< Black's clock after it.
};

/** Plays one game between two players. */
class GameRunner
{
  public:
    /**
     * A runner.
     * @param white White.
     * @param black Black.
     * @param settings How to play.
     */
    GameRunner(Player &white, Player &black, GameSettings settings);

    /**
     * Play the game to its end.
     * @param on_move Called after every move, may be empty.
     * @param cancel When set, the game is abandoned before the next move - or at once, when the side asked answers
     *        with no move after it was set (a person's wait given up) - may be null.
     * @return The finished game.
     */
    GameRecord Play(const std::function<void(const GameProgress &)> &on_move = {},
                    const std::atomic<bool>                         *cancel  = nullptr);

  private:
    Player      &white_;    ///< White.
    Player      &black_;    ///< Black.
    GameSettings settings_; ///< How to play.
};

} // namespace uci_host::match
