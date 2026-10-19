#pragma once
/// @file CompetitionSession.h A match or a tournament run from the window: the core's Tournament on its worker
/// threads, its events - games started, moves made, games finished, warnings - and its engines' lines drained once a
/// frame into what the window shows: the games in progress, any of which the board follows, the finished games, and
/// the score, the Elo and the standings.

#include "match/Statistics.h"
#include "match/Tournament.h"
#include "uci/Engine.h"

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace uci_host::app
{

/** A game of the competition in progress, as last reported. */
struct LiveGame
{
    int                                   number = 0;         ///< The game's number, from 1.
    std::string                           white_name;         ///< White's name.
    std::string                           black_name;         ///< Black's name.
    std::shared_ptr<const chess::Game>    game;               ///< The game so far; null before its first move.
    std::vector<match::MoveNote>          notes;              ///< One per move.
    chess::Milliseconds                   white_remaining{0}; ///< White's clock after the last move.
    chess::Milliseconds                   black_remaining{0}; ///< Black's clock after the last move.
    std::chrono::steady_clock::time_point turn_started;       ///< When the side to move's turn began.
};

/** A line an engine of the competition exchanged. */
struct CompetitionLogLine
{
    int                                   game_number = 0; ///< The game it was in.
    std::string                           engine;          ///< The engine's name.
    uci::LineDirection                    direction;       ///< Which way it went.
    std::string                           text;            ///< The line.
    std::chrono::steady_clock::time_point at;              ///< When.
};

/** A match or a tournament, played on the core's worker threads, followed from the window's thread. */
class CompetitionSession
{
  public:
    /**
     * Start it.
     * @param settings How to play it; moves are reported and the engines' lines logged whatever they say.
     * @param is_match Whether it is a match: two engines, scored as one against the other.
     */
    CompetitionSession(match::TournamentSettings settings, bool is_match);

    /** Stops play and waits for the workers. */
    ~CompetitionSession();

    CompetitionSession(const CompetitionSession &)            = delete;
    CompetitionSession &operator=(const CompetitionSession &) = delete;

    /** @return Why it could not start, or empty when it started. */
    const std::string &start_error() const
    {
        return start_error_;
    }

    /**
     * Take in what has happened, never waiting.
     * @return Whether anything changed.
     */
    bool Poll();

    /** Stop: no new game starts and the games in progress are abandoned before their next move. */
    void Stop();

    /** @return Whether every game has been played, or play stopped and the workers are done. */
    bool IsFinished() const
    {
        return is_finished_;
    }

    /** @return Whether it is a match. */
    bool is_match() const
    {
        return is_match_;
    }

    /** @return The games in progress, by number. */
    const std::map<int, LiveGame> &live() const
    {
        return live_;
    }

    /** @return The finished games, in the order they finished. */
    const std::vector<std::shared_ptr<const match::GameRecord>> &finished() const
    {
        return finished_;
    }

    /** @return The games in the schedule. */
    int total_games() const;

    /** @return Each engine's name, as the tournament knows it now. */
    std::vector<std::string> EngineNames() const;

    /** @return Every finished game's result. */
    std::vector<match::GameResultEntry> Results() const;

    /** @return For a match, the first engine's wins, draws and losses. */
    match::ScoreLine FirstEngineScore() const;

    /** @return The warnings and the lines since the last call, in order. */
    std::vector<CompetitionLogLine> DrainLog();

    /** @return The warnings so far. */
    const std::vector<std::string> &warnings() const
    {
        return warnings_;
    }

    /** @return How it is played. */
    const match::TournamentSettings &settings() const
    {
        return settings_;
    }

    /**
     * A one-line summary of the score: "Pawnstar 3½ - 2½ Zenith (Elo +35 ± 140, LOS 70 %)" for a match, the leader's
     * points for a tournament.
     * @return The text; empty before any game finished.
     */
    std::string ScoreText() const;

  private:
    match::TournamentSettings                             settings_;            ///< How it is played.
    bool                                                  is_match_;            ///< A match.
    std::unique_ptr<match::Tournament>                    tournament_;          ///< The tournament.
    std::string                                           start_error_;         ///< Why it did not start.
    std::map<int, LiveGame>                               live_;                ///< The games in progress.
    std::vector<std::shared_ptr<const match::GameRecord>> finished_;            ///< The finished games.
    std::vector<std::string>                              warnings_;            ///< The warnings.
    bool                                                  is_finished_ = false; ///< Done.
    std::shared_ptr<std::mutex>                           log_lock_;            ///< Guards the log.
    std::shared_ptr<std::vector<CompetitionLogLine>>      log_;                 ///< Lines not yet drained.
};

/**
 * Points as a person writes them: "3", "2½", "0".
 * @param points The points.
 * @return The text.
 */
std::string PointsText(double points);

} // namespace uci_host::app
