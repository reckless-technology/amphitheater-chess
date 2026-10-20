#pragma once
/// @file Perft.h Counting the leaves of the legal move tree, the move generator's proof.

#include "Move.h"
#include "Position.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace amphitheater::chess
{

/**
 * The number of move sequences of a length from a position (the leaves of the legal move tree at that depth); the
 * last level is counted from the length of the legal move list, not made.
 * @param position The position.
 * @param depth The length, 0 or more (depth 0 counts the position itself, 1).
 * @return The count.
 */
inline std::uint64_t Perft(const Position &position, int depth)
{
    if (depth <= 0)
    {
        return 1;
    }
    const MoveList moves = position.GenerateLegalMoves();
    if (depth == 1)
    {
        return moves.size();
    }
    std::uint64_t nodes = 0;
    for (const Move &move : moves)
    {
        nodes += Perft(position.MakeMove(move), depth - 1);
    }
    return nodes;
}

/**
 * Perft split by the first move, for finding where two generators disagree.
 * @param position The position.
 * @param depth The length, 1 or more.
 * @return Each legal move with the count below it, in generation order.
 */
inline std::vector<std::pair<Move, std::uint64_t>> PerftDivide(const Position &position, int depth)
{
    std::vector<std::pair<Move, std::uint64_t>> counts;
    for (const Move &move : position.GenerateLegalMoves())
    {
        counts.emplace_back(move, Perft(position.MakeMove(move), depth - 1));
    }
    return counts;
}

} // namespace amphitheater::chess
