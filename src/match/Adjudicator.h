#pragma once
/// @file Adjudicator.h Ending a game early on the engines' own evaluations, or at a length.
///
/// Resignation: a side loses when its engine has reported a score of at most -score for move_count of its moves in
/// a row - and, two-sided, its opponent has reported at least +score for its last move_count moves too. Draw: once
/// move_number moves have been played, the game is drawn when both engines have reported scores within score of zero
/// for move_count of their moves in a row each. A mate score counts as larger than any score in pawns. A move with no
/// score breaks a run. Maximum length: a draw once each side has made max_moves moves.

#include "chess/Game.h"
#include "uci/UciProtocol.h"

#include <array>
#include <optional>

namespace amphitheater::match
{

/** When to resign a game for a side. */
struct ResignAdjudication
{
    bool is_enabled   = false; ///< Whether to adjudicate resignations.
    int  move_count   = 3;     ///< Moves in a row.
    int  score        = 600;   ///< In centipawns: a score of at most minus this.
    bool is_two_sided = false; ///< Whether the opponent must agree, with at least plus this.
};

/** When to declare a game drawn. */
struct DrawAdjudication
{
    bool is_enabled  = false; ///< Whether to adjudicate draws.
    int  move_number = 40;    ///< The full move from which it may happen.
    int  move_count  = 8;     ///< Moves in a row, for each side.
    int  score       = 10;    ///< In centipawns: scores within this of zero.
};

/** Every adjudication rule. */
struct AdjudicationSettings
{
    ResignAdjudication resign;        ///< Resignation.
    DrawAdjudication   draw;          ///< Draws.
    int                max_moves = 0; ///< A draw once each side has made this many moves; 0 for no limit.
};

/** Watches a game's scores and decides when it is over. */
class Adjudicator
{
  public:
    /**
     * An adjudicator for one game.
     * @param settings The rules.
     */
    explicit Adjudicator(AdjudicationSettings settings) : settings_(settings)
    {
    }

    /**
     * Note a move's score and judge the game after it.
     * @param game The game, the move played.
     * @param mover The side that moved.
     * @param score The score its engine reported, from its own side, if any.
     * @return The outcome when the game is to end here.
     */
    std::optional<chess::Outcome> Judge(const chess::Game &game, chess::Color mover,
                                        const std::optional<uci::Score> &score);

    /**
     * A score as centipawns for comparison: a mate counts as more than any material.
     * @param score The score.
     * @return Centipawns.
     */
    static int Centipawns(const uci::Score &score);

  private:
    AdjudicationSettings settings_;       ///< The rules.
    std::array<int, 2>   losing_runs_{};  ///< Each side's run of scores at most -resign score.
    std::array<int, 2>   winning_runs_{}; ///< Each side's run of scores at least +resign score.
    std::array<int, 2>   drawn_runs_{};   ///< Each side's run of scores within the draw score.
};

} // namespace amphitheater::match
