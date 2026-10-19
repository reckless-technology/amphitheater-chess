// Perft on the standard suite (tests/data/standard-perft.epd: the standard.epd collection the Ethereal engine
// publishes, as pawnstar's perft test carries it): every position to the deepest depth whose count fits the node
// budget - 1 million nodes a position in a debug build, 20 million in an optimised one, UCI_HOST_PERFT_BUDGET to choose
// another - and the start position to depth 6 in an optimised build.

#include "chess/Perft.h"
#include "TestSupport.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace uci_host::chess
{
namespace
{

struct PerftCase
{
    std::string                           fen;
    std::vector<std::pair<int, uint64_t>> depths;
};

std::vector<PerftCase> LoadSuite()
{
    std::ifstream          file(std::string(UCI_HOST_TEST_DATA_DIR) + "/standard-perft.epd");
    std::vector<PerftCase> cases;
    std::string            line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        std::stringstream fields(line);
        std::string       field;
        PerftCase         entry;
        std::getline(fields, entry.fen, ';');
        while (std::getline(fields, field, ';'))
        {
            const auto space = field.find(' ');
            entry.depths.emplace_back(std::stoi(field.substr(1, space - 1)), std::stoull(field.substr(space + 1)));
        }
        if (!entry.fen.empty())
        {
            cases.push_back(entry);
        }
    }
    return cases;
}

uint64_t NodeBudget()
{
    if (const auto text = testing::EnvironmentVariable("UCI_HOST_PERFT_BUDGET"))
    {
        return std::stoull(*text);
    }
#ifdef NDEBUG
    return 20'000'000;
#else
    return 1'000'000;
#endif
}

TEST(Perft, StandardSuite)
{
    const auto cases = LoadSuite();
    ASSERT_GE(cases.size(), 100u) << "the suite did not load";
    const uint64_t budget = NodeBudget();
    uint64_t       total  = 0;
    for (const auto &entry : cases)
    {
        const auto position = Position::FromFen(entry.fen, FenMode::kTolerant);
        ASSERT_TRUE(position) << entry.fen << ": " << position.ErrorMessage();
        for (const auto &[depth, expected] : entry.depths)
        {
            if (expected > budget)
            {
                break;
            }
            EXPECT_EQ(Perft(*position, depth), expected) << entry.fen << " depth " << depth;
            total += expected;
        }
    }
    std::printf("perft: %zu positions, %llu leaves within a budget of %llu a position\n", cases.size(),
                static_cast<unsigned long long>(total), static_cast<unsigned long long>(budget));
}

#ifdef NDEBUG
TEST(Perft, StartPositionDepthSix)
{
    EXPECT_EQ(Perft(Position::Start(), 6), 119'060'324u);
}
#endif

TEST(Perft, DivideSumsToPerft)
{
    const auto position = *Position::FromFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    uint64_t   sum      = 0;
    const auto divided  = PerftDivide(position, 3);
    for (const auto &[move, count] : divided)
    {
        sum += count;
    }
    EXPECT_EQ(divided.size(), 48u);
    EXPECT_EQ(sum, 97'862u);
    EXPECT_EQ(Perft(position, 0), 1u);
}

} // namespace
} // namespace uci_host::chess
