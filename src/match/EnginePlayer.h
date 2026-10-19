#pragma once
/// @file EnginePlayer.h A UCI engine as a player.
///
/// The engine is started for the first game and kept between games ("ucinewgame" and "isready" before each); one
/// that crashed, hung or was killed is started afresh before the next game. Each move sends the whole game as
/// "position" and the clocks as "go": wtime, btime, winc, binc and movestogo under sessions, movetime under a fixed
/// time per move, depth and nodes when the time control has them, "go infinite" never.

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
     * @param config How to start the engine.
     * @param time_control The time control it plays under (its depth and node limits go into "go").
     */
    EnginePlayer(uci::EngineConfig config, chess::TimeControl time_control);

    /** @return The engine's name: the configured one, or the engine's own once it has started. */
    std::string Name() const override;

    Status    StartGame(const chess::Game &game) override;
    MoveReply RequestMove(const MoveRequest &request) override;

    /**
     * Receive every line exchanged with the engine (for a log, or the GUI's engine console).
     * @param logger The receiver, called on the game's thread.
     */
    void SetLogger(uci::EngineLogger logger);

    /** @return Warnings from the engine's last start (options skipped). */
    std::vector<std::string> Warnings() const;

    /**
     * The "go" a move request becomes.
     * @param request The request.
     * @param time_control The engine's time control.
     * @return The parameters.
     */
    static uci::GoParameters GoFor(const MoveRequest &request, const chess::TimeControl &time_control);

  private:
    uci::EngineConfig            config_;             ///< How to start the engine.
    chess::TimeControl           time_control_;       ///< Its time control.
    std::unique_ptr<uci::Engine> engine_;             ///< The engine, once started.
    uci::EngineLogger            logger_;             ///< Receives every line.
    bool                         is_healthy_ = false; ///< Whether the engine answered its last request in time.
};

} // namespace uci_host::match
