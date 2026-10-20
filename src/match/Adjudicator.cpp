#include "Adjudicator.h"

#include <cstdlib>

namespace amphitheater::match
{

int Adjudicator::Centipawns(const uci::Score &score)
{
    if (score.kind == uci::Score::Kind::kMate)
    {
        // Mate in n: further than any material, nearer mates further still.
        const int distance = std::abs(score.value);
        return (score.value >= 0 ? 1 : -1) * (100000 - distance);
    }
    return score.value;
}

std::optional<chess::Outcome> Adjudicator::Judge(const chess::Game &game, chess::Color mover,
                                                 const std::optional<uci::Score> &score)
{
    const int side = mover;
    if (score)
    {
        const int centipawns = Centipawns(*score);
        losing_runs_[side]   = centipawns <= -settings_.resign.score ? losing_runs_[side] + 1 : 0;
        winning_runs_[side]  = centipawns >= settings_.resign.score ? winning_runs_[side] + 1 : 0;
        drawn_runs_[side]    = std::abs(centipawns) <= settings_.draw.score ? drawn_runs_[side] + 1 : 0;
    }
    else
    {
        losing_runs_[side] = winning_runs_[side] = drawn_runs_[side] = 0;
    }

    const auto &resign = settings_.resign;
    if (resign.is_enabled && losing_runs_[side] >= resign.move_count &&
        (!resign.is_two_sided || winning_runs_[1 - side] >= resign.move_count))
    {
        const bool is_white = mover == chess::kWhite;
        return chess::Outcome{is_white ? chess::GameResult::kBlackWins : chess::GameResult::kWhiteWins,
                              chess::Termination::kResignation, is_white ? "White resigns" : "Black resigns"};
    }
    const auto &draw = settings_.draw;
    // The move number of the move just made: a black move leaves the position's number one higher.
    const int move_number = game.CurrentPosition().FullmoveNumber() - (mover == chess::kBlack ? 1 : 0);
    if (draw.is_enabled && move_number >= draw.move_number && drawn_runs_[0] >= draw.move_count &&
        drawn_runs_[1] >= draw.move_count)
    {
        return chess::Outcome{chess::GameResult::kDraw, chess::Termination::kAdjudication, "Draw by adjudication"};
    }
    if (settings_.max_moves > 0 && mover == chess::kBlack && move_number >= settings_.max_moves)
    {
        return chess::Outcome{chess::GameResult::kDraw, chess::Termination::kAdjudication,
                              "Draw by adjudication: the maximum game length"};
    }
    return std::nullopt;
}

} // namespace amphitheater::match
