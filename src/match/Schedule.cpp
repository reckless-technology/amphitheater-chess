#include "Schedule.h"

namespace uci_host::match
{

std::vector<std::vector<std::pair<int, int>>> CirclePairings(int engines)
{
    std::vector<std::vector<std::pair<int, int>>> rounds;
    if (engines < 2)
    {
        return rounds;
    }
    // An odd field gets a bye, numbered past the last engine; whoever meets it sits the round out.
    const int        size = engines % 2 == 0 ? engines : engines + 1;
    std::vector<int> circle(static_cast<std::size_t>(size));
    for (int index = 0; index < size; ++index)
    {
        circle[index] = index;
    }
    for (int round = 0; round < size - 1; ++round)
    {
        std::vector<std::pair<int, int>> pairings;
        for (int board = 0; board < size / 2; ++board)
        {
            int first  = circle[board];
            int second = circle[size - 1 - board];
            // The fixed engine alternates colours by round; on every other board the left of the circle has white.
            // A rotating engine spends as many rounds on the left as on the right, so with its one game against the
            // fixed engine its whites and blacks differ by at most one.
            const bool is_swapped = board == 0 && round % 2 == 1;
            if (is_swapped)
            {
                std::swap(first, second);
            }
            if (first < engines && second < engines)
            {
                pairings.emplace_back(first, second);
            }
        }
        rounds.push_back(std::move(pairings));
        // Rotate every position but the first one step.
        const int last = circle[size - 1];
        for (int index = size - 1; index > 1; --index)
        {
            circle[index] = circle[index - 1];
        }
        circle[1] = last;
    }
    return rounds;
}

std::vector<ScheduledGame> MakeSchedule(const ScheduleSettings &settings)
{
    std::vector<ScheduledGame> games;
    const auto                 cycle   = CirclePairings(settings.engines);
    const int                  cycles  = settings.is_double ? 2 : 1;
    int                        opening = 0;
    for (int round = 0; round < settings.rounds; ++round)
    {
        for (int pass = 0; pass < cycles; ++pass)
        {
            for (const auto &pairings : cycle)
            {
                for (const auto &[first, second] : pairings)
                {
                    for (int game = 0; game < settings.games_per_encounter; ++game)
                    {
                        // Within an encounter colours alternate game by game; an encounter of an odd number of
                        // games starts the other way round in the next round, so a long match stays balanced.
                        const bool    is_reversed = (game + pass + round * settings.games_per_encounter) % 2 == 1;
                        ScheduledGame scheduled;
                        scheduled.number  = static_cast<int>(games.size()) + 1;
                        scheduled.round   = round + 1;
                        scheduled.white   = is_reversed ? second : first;
                        scheduled.black   = is_reversed ? first : second;
                        scheduled.opening = settings.is_repeat ? opening / 2 : opening;
                        ++opening;
                        games.push_back(scheduled);
                    }
                }
            }
        }
    }
    return games;
}

} // namespace uci_host::match
