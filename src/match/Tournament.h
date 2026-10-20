#pragma once
/// @file Tournament.h A match or a round robin, its games played in parallel.
///
/// The schedule (Schedule.h) is shared out among worker threads, as many as the concurrency: each takes the next game
/// not yet started, plays it to its end with a GameRunner, and takes the next. Each worker keeps its own engine
/// processes - one per engine it has played with, restarted after a failure - so games running at once never share
/// an engine, and every engine is driven by the one thread that owns it.
///
/// Progress goes into a thread-safe queue of events that the owner drains: the command line's main loop, the GUI's
/// frame callback. The queue closes after the last event, kFinished. Finished games are appended to the PGN file as
/// they end, in the order they end.

#include "Adjudicator.h"
#include "GameRunner.h"
#include "Schedule.h"
#include "Statistics.h"
#include "chess/Result.h"
#include "uci/Engine.h"
#include "uci/ThreadSafeQueue.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace uci_host::match
{

/** An engine in a tournament. */
struct TournamentEngine
{
    uci::EngineConfig                          config;         ///< How to start it (its does_ponder among it).
    std::optional<chess::TimeControl>          time_control;   ///< Its own time control; the tournament's when not set.
    std::shared_ptr<const chess::PolyglotBook> book;           ///< Its own opening book, if any.
    int                                        book_depth = 0; ///< Book moves only before this game ply; 0 no limit.
};

/**
 * Receives an engine's lines during a tournament, on the worker thread playing the game: the game, the side the engine
 * plays in it (which tells two copies of one engine apart), its name, the direction and the line as it went.
 */
using TournamentEngineLogger = std::function<void(int game_number, chess::Color side, const std::string &engine,
                                                  uci::LineDirection direction, std::string_view line)>;

/** How a tournament is to be played. */
struct TournamentSettings
{
    std::vector<TournamentEngine> engines;         ///< The engines, two for a match.
    chess::TimeControl            time_control;    ///< The time control, unless an engine has its own.
    ScheduleSettings              schedule;        ///< Rounds, games per encounter, double, repeat (engines is set).
    int                           concurrency = 1; ///< Games played at once.
    std::vector<Opening>          openings;        ///< The openings, in the order to use them; none for the start.
    std::shared_ptr<const chess::PolyglotBook> opening_book;           ///< Openings walked from a book instead, if set.
    int                                        opening_book_plies = 8; ///< How long the book's openings are, in plies.
    std::uint64_t          seed = 1;             ///< The seed of every random choice: book walks and book moves.
    AdjudicationSettings   adjudication;         ///< When to end games early.
    chess::Milliseconds    time_margin{0};       ///< How far past its time a side may go.
    chess::Milliseconds    stall_timeout{60000}; ///< How long to wait for a move with no clock.
    std::string            event = "?";          ///< PGN's Event.
    std::string            site  = "?";          ///< PGN's Site.
    std::string            pgn_path;             ///< A file to append every finished game to; empty for none.
    TournamentEngineLogger engine_logger;        ///< Receives every engine line, may be empty.
    bool does_report_moves = false; ///< Whether every move is an event (kMoveMade), for a window following a game.
};

/** Something that happened in a tournament. */
struct TournamentEvent
{
    /** What happened. */
    enum class Kind
    {
        kGameStarted,  ///< A game began.
        kGameFinished, ///< A game ended.
        kMoveMade,     ///< A move was made in a game in progress (only when the settings ask for moves).
        kWarning,      ///< Something went wrong without stopping play (an engine's option skipped, a file).
        kFinished,     ///< Every game has been played, or play was stopped; the last event.
    };

    Kind                               kind        = Kind::kWarning; ///< What happened.
    int                                game_number = 0;              ///< The game, from 1.
    int                                total_games = 0;              ///< Games in the schedule.
    int                                white       = 0;              ///< White's index.
    int                                black       = 0;              ///< Black's index.
    std::string                        white_name;                   ///< White's name.
    std::string                        black_name;                   ///< Black's name.
    chess::Outcome                     outcome;                      ///< For kGameFinished: how it ended.
    std::string                        message;                      ///< For kWarning: what went wrong.
    std::shared_ptr<const GameRecord>  record;                       ///< For kGameFinished: the whole game.
    std::shared_ptr<const chess::Game> game;                         ///< For kMoveMade: the game after the move.
    std::shared_ptr<const MoveNote>    note;                         ///< For kMoveMade: what was said of the move.
    chess::Milliseconds                white_remaining{0};           ///< For kMoveMade: White's clock after the move.
    chess::Milliseconds                black_remaining{0};           ///< For kMoveMade: Black's clock after the move.
};

/** A match or a round robin. */
class Tournament
{
  public:
    /**
     * A tournament, not yet started.
     * @param settings How to play it.
     */
    explicit Tournament(TournamentSettings settings);

    /** Stops play and waits for the workers. */
    ~Tournament();

    Tournament(const Tournament &)            = delete;
    Tournament &operator=(const Tournament &) = delete;

    /**
     * Start play on the worker threads.
     * @return Ok, or why it cannot start (fewer than two engines, nothing to play).
     */
    Status Start();

    /** @return The events, for the owner to drain; closed after kFinished. */
    ThreadSafeQueue<TournamentEvent> &Events()
    {
        return events_;
    }

    /** Stop: no new game starts, and games in progress are abandoned before their next move. */
    void RequestStop();

    /** Wait for the workers to finish. */
    void Wait();

    /** @return How many games the schedule holds. */
    int TotalGames() const
    {
        return static_cast<int>(schedule_.size());
    }

    /** @return The schedule. */
    const std::vector<ScheduledGame> &Schedule() const
    {
        return schedule_;
    }

    /** @return Each engine's name: the configured one, or the engine's own once it has played. */
    std::vector<std::string> EngineNames() const;

    /** @return Every finished game's result, in the order they finished. */
    std::vector<GameResultEntry> Results() const;

  private:
    /** One worker: take the next game, play it, repeat. */
    void Work();

    TournamentSettings               settings_;            ///< How to play.
    std::vector<ScheduledGame>       schedule_;            ///< Every game.
    ThreadSafeQueue<TournamentEvent> events_;              ///< Progress, for the owner.
    std::vector<std::thread>         workers_;             ///< The workers.
    std::atomic<int>                 next_game_   = 0;     ///< The next game to start, by index.
    std::atomic<int>                 working_     = 0;     ///< Workers still running; the last sends kFinished.
    std::atomic<bool>                is_stopping_ = false; ///< Whether play is to stop.
    mutable std::mutex               mutex_;               ///< Guards names_, results_ and the PGN file.
    std::vector<std::string>         names_;               ///< Each engine's name.
    std::vector<GameResultEntry>     results_;             ///< Finished games' results.
};

} // namespace uci_host::match
