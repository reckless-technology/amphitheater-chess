#pragma once
/// @file Game.h A game: a starting position, the moves played from it, and how it ended.
///
/// The rules that end a game by themselves - checkmate, stalemate, insufficient material, threefold repetition and
/// the fifty-move rule - are judged after every move, as a host that plays engines against each other applies them
/// (no player has to claim the draw). Anything else that ends a game - a flag falling, an illegal move, a resignation,
/// an adjudication - is set from outside with SetOutcome.

#include "Move.h"
#include "Position.h"
#include "Result.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace uci_host::chess
{

/** A game's result as PGN writes it. */
enum class GameResult
{
    kNone,      ///< Not over, or over without a result: "*".
    kWhiteWins, ///< "1-0".
    kBlackWins, ///< "0-1".
    kDraw,      ///< "1/2-1/2".
};

/**
 * A result's PGN text.
 * @param result The result.
 * @return "1-0", "0-1", "1/2-1/2" or "*".
 */
std::string ResultText(GameResult result);

/**
 * Read a result's PGN text.
 * @param text "1-0", "0-1", "1/2-1/2" or "*".
 * @return The result; kNone for anything else.
 */
GameResult ParseResultText(std::string_view text);

/** Why a game ended. */
enum class Termination
{
    kNone,                 ///< It has not ended.
    kCheckmate,            ///< The side to move is checkmated.
    kStalemate,            ///< The side to move has no legal move and is not in check.
    kInsufficientMaterial, ///< Neither side can mate.
    kThreefoldRepetition,  ///< The same position, with the same side to move, for the third time.
    kFiftyMoves,           ///< Fifty moves by each side without a capture or a pawn move.
    kTimeForfeit,          ///< A flag fell.
    kIllegalMove,          ///< A player made an illegal move.
    kResignation,          ///< A player resigned, or was adjudicated lost on its own evaluation.
    kAdjudication,         ///< Ended by the host's rules: a drawn evaluation, or the game's length.
    kDisconnection,        ///< A player's engine crashed or quit.
    kStalled,              ///< A player stopped responding.
    kAbandoned,            ///< Stopped by the operator before it finished.
    kRecorded, ///< Over as a record says, for no reason the rules give: a resignation or an agreed draw in PGN.
};

/**
 * PGN's Termination tag for a reason: "normal", "time forfeit", "rules infraction", "adjudication", "abandoned" or
 * "unterminated".
 * @param termination The reason.
 * @return The tag's value.
 */
std::string TerminationTag(Termination termination);

/** How a game ended. */
struct Outcome
{
    GameResult  result      = GameResult::kNone;  ///< The result.
    Termination termination = Termination::kNone; ///< Why.
    std::string description;                      ///< In words: "White mates", "Black loses on time".

    /** @return Whether the game is over. */
    bool IsOver() const
    {
        return termination != Termination::kNone;
    }
};

/** A game of chess from a starting position. */
class Game
{
  public:
    /** A game from the starting position. */
    Game();

    /**
     * A game from a position.
     * @param start The position before the first move.
     */
    explicit Game(const Position &start);

    /** @return The position before the first move. */
    const Position &StartPosition() const
    {
        return positions_.front();
    }

    /** @return The position after the last move. */
    const Position &CurrentPosition() const
    {
        return positions_.back();
    }

    /**
     * The position after some of the moves.
     * @param ply 0 for the start, up to PlyCount().
     * @return The position.
     */
    const Position &PositionAt(int ply) const
    {
        return positions_[ply];
    }

    /** @return Whether the game starts from the classical starting position. */
    bool StartsFromStandardPosition() const;

    /** @return The moves played, in order. */
    const std::vector<Move> &Moves() const
    {
        return moves_;
    }

    /** @return How many half-moves have been played. */
    int PlyCount() const
    {
        return static_cast<int>(moves_.size());
    }

    /**
     * Play a move. Refused once the game is over; an undo reopens it.
     * @param move The move.
     * @return Whether it was legal and played.
     */
    bool Play(Move move);

    /**
     * Play a move written in UCI notation.
     * @param text The move.
     * @return The move played, or why it was not.
     */
    Result<Move> PlayUci(std::string_view text);

    /**
     * Play a move written in SAN (read tolerantly, as ParseSan does).
     * @param text The move.
     * @return The move played, or why it was not.
     */
    Result<Move> PlaySan(std::string_view text);

    /**
     * Take back the last move, and with it any outcome.
     * @return False when there is no move to take back.
     */
    bool Undo();

    /**
     * A move in SAN, as it was written in the position it was played in.
     * @param ply 0 for the first move.
     * @return The SAN.
     */
    std::string San(int ply) const;

    /** @return Every move in SAN. */
    std::vector<std::string> SanMoves() const;

    /** @return Every move in UCI notation. */
    std::vector<std::string> UciMoves() const;

    /** @return How many times the current position has occurred, counting now. */
    int RepetitionCount() const;

    /** @return The outcome the rules give the current position, or no outcome when they let play go on. */
    Outcome RulesOutcome() const;

    /** @return How the game ended, or an outcome with no termination while it goes on. */
    const Outcome &GetOutcome() const
    {
        return outcome_;
    }

    /** @return Whether the game is over. */
    bool IsOver() const
    {
        return outcome_.IsOver();
    }

    /**
     * End the game for a reason outside the rules of play (a flag, an illegal move, an adjudication).
     * @param outcome How it ended.
     */
    void SetOutcome(Outcome outcome)
    {
        outcome_ = std::move(outcome);
    }

  private:
    std::vector<Position> positions_; ///< The start, then the position after each move.
    std::vector<Move>     moves_;     ///< The moves.
    Outcome               outcome_;   ///< How it ended, if it has.
};

} // namespace uci_host::chess
