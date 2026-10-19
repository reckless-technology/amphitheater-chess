#pragma once
/// @file PlaySession.h A game played on the board: the core's GameRunner on a thread of its own, each side an engine
/// (EnginePlayer) or the person at the window (a CallbackPlayer whose chooser waits for the board's move), its progress
/// and the engines' lines handed to the window, which polls them once a frame.

#include "EngineCatalogue.h"
#include "chess/Game.h"
#include "chess/TimeControl.h"
#include "match/GameRunner.h"
#include "uci/Engine.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace uci_host::app
{

/** One side of a game on the board. */
struct SideSpec
{
    bool        is_human   = true;  ///< The person at the window; else the engine.
    std::string human_name = "You"; ///< The person's name, as the strip and the PGN show it.
    EngineEntry engine;             ///< The engine, when not the person.

    /** @return The name the strip shows. */
    std::string Name() const
    {
        return is_human ? human_name : engine.DisplayName();
    }
};

/** Both sides' clocks at one position of a game: as they stood once the move that led to it was charged. */
struct ClockReading
{
    chess::Milliseconds white_remaining{};     ///< White's time left.
    chess::Milliseconds black_remaining{};     ///< Black's.
    int                 white_moves_to_go = 0; ///< White's moves to the next session; 0 for none.
    int                 black_moves_to_go = 0; ///< Black's, likewise.
    chess::Milliseconds white_increment{};     ///< White's increment.
    chess::Milliseconds black_increment{};     ///< Black's.

    /**
     * Both clocks read.
     * @param white White's clock.
     * @param black Black's clock.
     * @return The reading.
     */
    static ClockReading Of(const chess::Clock &white, const chess::Clock &black);
};

/** How a game on the board is to be played. */
struct PlaySettings
{
    SideSpec                          white;        ///< White.
    SideSpec                          black;        ///< Black.
    chess::TimeControl                time_control; ///< Both sides' time control; no sessions for no clock.
    std::string                       start_fen;    ///< The starting position; empty for the standard one.
    std::vector<std::string>          start_moves;  ///< Moves from it, in UCI notation, before play begins (unclocked).
    std::string                       event = "Game"; ///< PGN's Event.
    std::optional<chess::TimeControl> white_resumed;  ///< White's control when a game resumes (a take-back), else none.
    std::optional<chess::TimeControl> black_resumed;  ///< Black's, likewise.
    /// A resumed game's clocks at every position up to the one it resumes from, so a later take-back finds them.
    std::vector<ClockReading> resumed_clocks;
    /// A resumed game's opening: how many of `start_moves` were given before play began, the rest played and so open
    /// to a take-back; none for all of them.
    std::optional<std::size_t> opening_length;
};

/** A line an engine exchanged during the game. */
struct PlayLogLine
{
    std::string                           engine;    ///< The engine's name.
    uci::LineDirection                    direction; ///< Which way it went.
    std::string                           text;      ///< The line.
    std::chrono::steady_clock::time_point at;        ///< When.
};

/** The game as it stands, for the window. */
struct PlaySnapshot
{
    chess::Game                           game;   ///< The game so far.
    std::vector<match::MoveNote>          notes;  ///< One per move: score, depth, time, book.
    std::vector<ClockReading>             clocks; ///< One per position, from the start: [n] with n moves played.
    chess::Milliseconds                   white_remaining{};        ///< White's clock after the last move.
    chess::Milliseconds                   black_remaining{};        ///< Black's clock after the last move.
    int                                   white_moves_to_go = 0;    ///< White's moves to the next session; 0 for none.
    int                                   black_moves_to_go = 0;    ///< Black's, likewise.
    chess::Milliseconds                   white_increment{};        ///< White's increment now.
    chess::Milliseconds                   black_increment{};        ///< Black's increment now.
    std::chrono::steady_clock::time_point turn_started;             ///< When the side to move's turn began.
    bool                                  is_human_to_move = false; ///< The board waits for the person.
    bool                                  is_finished      = false; ///< The game is over (or abandoned).
    std::string                           white_name;               ///< White's name.
    std::string                           black_name;               ///< Black's name.
    int                                   version = 0;              ///< Counts every change.
};

/** A game on the board, played on its own thread. */
class PlaySession
{
  public:
    /**
     * A game, started at once on its own thread.
     * @param settings How to play it.
     */
    explicit PlaySession(PlaySettings settings);

    /** Stops the game and waits for its thread (an engine thinking finishes its move first). */
    ~PlaySession();

    PlaySession(const PlaySession &)            = delete;
    PlaySession &operator=(const PlaySession &) = delete;

    /** @return The game as it stands. */
    PlaySnapshot Snapshot() const;

    /** @return Counts every change to the snapshot, for a window that redraws only on one. */
    int Version() const;

    /**
     * The person's move, for the side the board waits on.
     * @param move The move.
     * @return Whether it was taken: the board waited for the person, and the move is legal there.
     */
    bool SubmitHumanMove(chess::Move move);

    /** Stop the game: a person's wait ends at once and an engine's at its answer; the game is abandoned. */
    void Stop();

    /** @return The lines the engines exchanged since the last call, in order. */
    std::vector<PlayLogLine> DrainLog();

    /** @return Whether the game is over and its thread done. */
    bool IsFinished() const;

    /** @return The finished game's record, once it is finished. */
    std::optional<match::GameRecord> Record() const;

    /** @return How it is played. */
    const PlaySettings &settings() const
    {
        return settings_;
    }

  private:
    /** The game's thread: the players made, the runner run, the record kept. */
    void Run();

    /**
     * The person's turn: wait for the board's move, the deadline or the stop.
     * @param request What the runner asks.
     * @return The answer.
     */
    match::MoveReply AwaitHuman(const match::MoveRequest &request);

    PlaySettings                     settings_;       ///< How it is played.
    mutable std::mutex               mutex_;          ///< Guards everything below.
    std::condition_variable          wake_;           ///< Wakes the person's wait.
    PlaySnapshot                     snapshot_;       ///< The game as it stands.
    std::optional<chess::Move>       human_move_;     ///< The person's move, handed over.
    std::vector<PlayLogLine>         log_;            ///< Lines not yet drained.
    std::optional<match::GameRecord> record_;         ///< The finished game.
    std::atomic<bool>                cancel_ = false; ///< Stop.
    std::thread                      thread_;         ///< The game's thread.
};

/**
 * The time control a game resumed from a clock continues with: what is left on the clock as its first session's time,
 * with the session's moves to go and increment, then the control's last session, which repeats.
 * @param control The original time control.
 * @param remaining The time left.
 * @param moves_to_go Moves to go in the session; 0 for the rest of the game.
 * @param increment The session's increment.
 * @return The time control to resume with.
 */
chess::TimeControl ResumedTimeControl(const chess::TimeControl &control, chess::Milliseconds remaining, int moves_to_go,
                                      chess::Milliseconds increment);

} // namespace uci_host::app
