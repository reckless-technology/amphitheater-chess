#pragma once
/// @file Schedule.h The order of a tournament's games: who plays whom, with which colour and which opening.
///
/// Pairings come from the circle method: with n engines (and a bye for an odd number), engine 0 holds its place
/// while the rest rotate one step a round, so in n - 1 rounds every engine meets every other once. Engine 0's colour
/// flips each round, and on every other board the engine on the left of the circle has white; since each rotating
/// engine spends as many rounds on the left as on the right, every engine's whites and blacks in a cycle differ by at
/// most one. Every later game of an encounter swaps the colours of the one before it. A double round robin plays
/// each cycle a second time with every colour reversed. A match is the round robin of two engines.
///
/// Openings advance one per game, or one per pair of games when each opening is played with both colours (repeat).

#include <utility>
#include <vector>

namespace amphitheater::match
{

/** One game in the schedule. */
struct ScheduledGame
{
    int number  = 0; ///< Its number in the schedule, from 1.
    int round   = 0; ///< Its round, from 1.
    int white   = 0; ///< White's index among the engines.
    int black   = 0; ///< Black's index.
    int opening = 0; ///< The opening's index (wrapping round the book is the caller's).
};

/** The shape of a tournament. */
struct ScheduleSettings
{
    int  engines             = 2;     ///< How many engines.
    int  rounds              = 1;     ///< How many cycles of every engine meeting every other.
    int  games_per_encounter = 1;     ///< Games between two engines each time they meet, colours alternating.
    bool is_double           = false; ///< Whether each cycle is played twice, the second with colours reversed.
    bool is_repeat           = false; ///< Whether each opening is played twice in a row, with colours swapped.
};

/**
 * The circle method's pairings for one cycle.
 * @param engines How many engines.
 * @return One list of pairings (white, black) per round; with an odd number each round leaves one engine out.
 */
std::vector<std::vector<std::pair<int, int>>> CirclePairings(int engines);

/**
 * Every game of a tournament, in order.
 * @param settings Its shape.
 * @return The games.
 */
std::vector<ScheduledGame> MakeSchedule(const ScheduleSettings &settings);

} // namespace amphitheater::match
