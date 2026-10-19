#pragma once
/// @file EnginePlayer.h A UCI engine as a player.
///
/// The engine is started for the first game and kept between games ("ucinewgame" and "isready" before each); one
/// that crashed, hung or was killed is started afresh before the next game. Each move sends the whole game as
/// "position" and the clocks as "go": wtime, btime, winc, binc and movestogo under sessions, movetime under a fixed
/// time per move, depth and nodes when the time control has them, "go infinite" never.
///
/// Pondering, when the engine's configuration asks for it: after the engine answers "bestmove X ponder Y" and X is
/// played, the player sends the game with Y after it and "go ponder" with the clocks as they then stand, and lets the
/// engine think while the opponent does. When the opponent's move comes it is Y ("ponderhit": the engine's clock
/// starts then, so the pondering is never charged) or not ("stop", the answer to the ponder search thrown away, then
/// the usual "position" and "go", the wait for the stop charged to the engine, which loses on time if it never
/// answers). An engine that names no ponder move simply does not ponder; a game that ends while it ponders stops the
/// search, and an engine that will not stop is started afresh for the next game.

#include "Player.h"
#include "uci/Engine.h"

#include <memory>
#include <string>
#include <vector>

namespace uci_host::match
{

/** A UCI engine playing one side. Driven by one thread, as its Engine is. */
class EnginePlayer : public Player
{
  public:
    /**
     * A player for an engine.
     * @param config How to start the engine (its does_ponder decides whether it ponders).
     * @param time_control The time control it plays under (its depth and node limits go into "go").
     */
    EnginePlayer(uci::EngineConfig config, chess::TimeControl time_control);

    /** @return The engine's name: the configured one, or the engine's own once it has started. */
    std::string Name() const override;

    Status    StartGame(const chess::Game &game) override;
    MoveReply RequestMove(const MoveRequest &request) override;
    void      MoveMade(const MoveRequest &state) override;
    void      EndGame(const chess::Game &game) override;

    /**
     * Receive every line exchanged with the engine (for a log, or the GUI's engine console).
     * @param logger The receiver, called on the game's thread.
     */
    void SetLogger(uci::EngineLogger logger);

    /** @return Warnings from the engine's last start (options skipped). */
    std::vector<std::string> Warnings() const;

    /** @return Whether the engine is pondering now. */
    bool IsPondering() const
    {
        return !pondering_on_.empty();
    }

    /**
     * The "go" a move request becomes.
     * @param request The request.
     * @param time_control The engine's time control.
     * @return The parameters.
     */
    static uci::GoParameters GoFor(const MoveRequest &request, const chess::TimeControl &time_control);

  private:
    /**
     * Stop a ponder search and throw its answer away.
     * @param deadline How long to wait for the answer.
     * @return Whether the engine answered.
     */
    bool StopPondering(std::chrono::steady_clock::time_point deadline);

    /**
     * The reply a finished search becomes.
     * @param result The search's end.
     * @return The reply.
     */
    MoveReply ReplyFrom(const uci::SearchResult &result);

    uci::EngineConfig            config_;             ///< How to start the engine.
    chess::TimeControl           time_control_;       ///< Its time control.
    std::unique_ptr<uci::Engine> engine_;             ///< The engine, once started.
    uci::EngineLogger            logger_;             ///< Receives every line.
    bool                         is_healthy_ = false; ///< Whether the engine answered its last request in time.
    std::string                  expected_reply_;     ///< The ponder move of the engine's last answer, until played.
    std::string                  pondering_on_;       ///< The move the engine is pondering on, while it ponders.
};

} // namespace uci_host::match
