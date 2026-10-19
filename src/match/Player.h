#pragma once
/// @file Player.h A side in a game: an engine, or anything else that can answer with a move.
///
/// The game runner asks each player for its move in turn, on the game's own thread, and blocks until it answers or
/// its deadline passes. An engine answers through UCI (EnginePlayer); a person at the GUI will answer through a
/// CallbackPlayer whose callback waits for the GUI to hand it a move. After every move both players are told
/// (MoveMade), with the clocks as they then stand: an engine that has just moved starts pondering there, and goes on
/// pondering for as long as its opponent - engine or person - takes, since nothing of the opponent's waits on it.

#include "chess/Game.h"
#include "chess/Result.h"
#include "chess/TimeControl.h"
#include "uci/UciProtocol.h"

#include <chrono>
#include <functional>
#include <string>
#include <utility>

namespace uci_host::match
{

/** What a player is asked: the game so far, the clocks, and when its time is up. */
struct MoveRequest
{
    const chess::Game                    &game;        ///< The game, the player's side to move.
    const chess::Clock                   &white_clock; ///< White's clock.
    const chess::Clock                   &black_clock; ///< Black's clock.
    std::chrono::steady_clock::time_point deadline;    ///< When the runner stops waiting for an answer.
};

/** How a player answered. */
struct MoveReply
{
    /** What the answer was. */
    enum class Status
    {
        kMove,         ///< A move (legal or not: the runner judges it).
        kTimeout,      ///< No answer before the deadline.
        kDisconnected, ///< The player is gone (the engine crashed or quit).
    };

    Status              status = Status::kMove; ///< What the answer was.
    std::string         move;                   ///< The move in UCI notation, for kMove.
    uci::SearchInfo     info;                   ///< What the engine said of its search: score, depth, nodes, pv.
    chess::Milliseconds elapsed{0};             ///< How long the answer took.
    std::string         detail;                 ///< For kDisconnected: what is known (an exit code).
    std::string         ponder_move;            ///< The reply the engine expects, in UCI notation, if it named one.
};

/** A side in a game. */
class Player
{
  public:
    virtual ~Player() = default;

    /** @return The name to show and to record. */
    virtual std::string Name() const = 0;

    /**
     * Get ready for a new game (an engine is started, or restarted after a failure, and told "ucinewgame").
     * @param game The game about to start, at its starting position.
     * @return Ok, or why the player cannot play.
     */
    virtual Status StartGame(const chess::Game &game) = 0;

    /**
     * Choose a move.
     * @param request The game, the clocks and the deadline.
     * @return The answer.
     */
    virtual MoveReply RequestMove(const MoveRequest &request) = 0;

    /**
     * A move has been made, by either side, and the clocks charged; the game goes on.
     * @param state The game after the move (the other side to move), the clocks, and no deadline.
     */
    virtual void MoveMade(const MoveRequest &state)
    {
        static_cast<void>(state);
    }

    /**
     * The game is over.
     * @param game The game, its outcome set.
     */
    virtual void EndGame(const chess::Game &game)
    {
        static_cast<void>(game);
    }
};

/** A player whose moves come from a function: a person at the GUI, or a test. */
class CallbackPlayer : public Player
{
  public:
    /** The function that chooses the move, called on the game's thread. */
    using Chooser = std::function<MoveReply(const MoveRequest &request)>;

    /**
     * A player.
     * @param name Its name.
     * @param chooser What chooses its moves.
     */
    CallbackPlayer(std::string name, Chooser chooser) : name_(std::move(name)), chooser_(std::move(chooser))
    {
    }

    std::string Name() const override
    {
        return name_;
    }

    Status StartGame(const chess::Game &game) override
    {
        static_cast<void>(game);
        return Ok{};
    }

    MoveReply RequestMove(const MoveRequest &request) override
    {
        return chooser_(request);
    }

  private:
    std::string name_;    ///< Its name.
    Chooser     chooser_; ///< What chooses its moves.
};

} // namespace uci_host::match
