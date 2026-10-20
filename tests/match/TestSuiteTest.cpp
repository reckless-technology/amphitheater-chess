// EPD test suites: lines read with their best moves, moves to avoid, names and counters, lines that cannot be read
// skipped with the reason, what solves a position, and a suite run against the fake engine.

#include "match/TestSuite.h"

#include "chess/Notation.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace amphitheater::match
{
namespace
{

/** White's queen takes Black's, which the fake engine always plays: it takes the most valuable piece it can. */
constexpr const char *kQueenTrade = "3q3k/8/8/8/8/8/8/3Q3K w - -";

/**
 * Every outcome of a run, the runner left to finish.
 * @param runner The runner, started.
 * @return The outcomes, in order.
 */
std::vector<TestOutcome> Drain(TestSuiteRunner &runner)
{
    std::vector<TestOutcome> outcomes;
    const auto               deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline)
    {
        while (auto next = runner.Next())
        {
            outcomes.push_back(std::move(*next));
        }
        if (runner.IsFinished())
        {
            while (auto next = runner.Next())
            {
                outcomes.push_back(std::move(*next));
            }
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return outcomes;
}

} // namespace

TEST(TestSuite, ALineGivesItsPositionItsMovesAndItsName)
{
    const auto test = ParseTestPosition(
        R"(2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - - bm Qg6; id "WAC.001"; c0 "a quiet; tricky one";)",
        7);
    ASSERT_TRUE(test) << test.ErrorMessage();
    EXPECT_EQ(test->id, "WAC.001");
    EXPECT_EQ(test->line, 7);
    ASSERT_EQ(test->best_moves.size(), 1u);
    EXPECT_EQ(chess::MoveToSan(test->position, test->best_moves[0]), "Qg6");
    EXPECT_TRUE(test->avoid_moves.empty());

    const auto several = ParseTestPosition(std::string(kQueenTrade) + " am Qxd8+ Kg1; hmvc 7; fmvn 40;", 1);
    ASSERT_TRUE(several) << several.ErrorMessage();
    EXPECT_EQ(several->avoid_moves.size(), 2u);
    EXPECT_EQ(several->id, "line 1") << "no id: the line's number";
    EXPECT_EQ(several->position.HalfmoveClock(), 7);
    EXPECT_EQ(several->position.FullmoveNumber(), 40);
}

TEST(TestSuite, ALineThatCannotBeReadIsSkippedWithTheReason)
{
    EXPECT_FALSE(ParseTestPosition("not an epd", 1));
    EXPECT_NE(ParseTestPosition(std::string(kQueenTrade) + " bm Qd9;", 1).ErrorMessage().find("'Qd9'"),
              std::string::npos);
    EXPECT_NE(ParseTestPosition(std::string(kQueenTrade) + " id \"nothing to do\";", 1).ErrorMessage().find("no bm"),
              std::string::npos);

    std::vector<std::string> errors;
    const auto               suite = ParseTestSuite(std::string("# a comment\n\n") + kQueenTrade + " bm Qxd8+;\n" +
                                                        "8/8/8/8 w - - bm e4;\n" + kQueenTrade + " am Kg1;\r\n",
                                                    &errors);
    ASSERT_EQ(suite.size(), 2u);
    EXPECT_EQ(suite[0].line, 3);
    EXPECT_EQ(suite[1].line, 5);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].rfind("line 4: ", 0), 0u) << errors[0];
}

TEST(TestSuite, APositionIsSolvedByABestMoveAndFailedByAMoveToAvoid)
{
    const auto best  = ParseTestPosition(std::string(kQueenTrade) + " bm Qxd8+;", 1);
    const auto avoid = ParseTestPosition(std::string(kQueenTrade) + " am Qxd8+;", 1);
    ASSERT_TRUE(best && avoid);
    const chess::Move trade = *chess::ParseSan(best->position, "Qxd8+");
    const chess::Move quiet = *chess::ParseSan(best->position, "Kg1");
    EXPECT_TRUE(Solves(*best, trade));
    EXPECT_FALSE(Solves(*best, quiet));
    EXPECT_FALSE(Solves(*avoid, trade));
    EXPECT_TRUE(Solves(*avoid, quiet)) << "with only a move to avoid, any other solves";
}

TEST(TestSuite, ASuiteIsRunAgainstAnEngineEachPositionInTurn)
{
    TestSuiteSettings settings;
    settings.engine.executable = AMPHITHEATER_FAKE_ENGINE;
    settings.move_time         = std::chrono::milliseconds(20);
    std::vector<std::string> errors;
    auto suite = ParseTestSuite(std::string(kQueenTrade) + " bm Qxd8+; id \"trade\";\n" + kQueenTrade +
                                    " am Qxd8+; id \"no trade\";\n" + kQueenTrade + " bm Kg1;\n",
                                &errors);
    ASSERT_EQ(suite.size(), 3u);
    TestSuiteRunner runner(settings, std::move(suite));
    runner.Start();
    const std::vector<TestOutcome> outcomes = Drain(runner);
    ASSERT_EQ(outcomes.size(), 3u) << runner.Failure();
    EXPECT_TRUE(runner.Failure().empty());
    EXPECT_EQ(outcomes[0].move, "Qxd8+");
    EXPECT_TRUE(outcomes[0].is_solved);
    EXPECT_FALSE(outcomes[1].is_solved) << "it played the move to avoid";
    EXPECT_FALSE(outcomes[2].is_solved);
    EXPECT_EQ(outcomes[2].index, 2);
    EXPECT_GE(outcomes[0].depth, 1);

    TestSuiteSettings missing = settings;
    missing.engine.executable = "no-such-engine-anywhere";
    TestSuiteRunner failing(missing, ParseTestSuite(std::string(kQueenTrade) + " bm Qxd8+;"));
    failing.Start();
    EXPECT_TRUE(Drain(failing).empty());
    EXPECT_NE(failing.Failure().find("could not be started"), std::string::npos);
}

} // namespace amphitheater::match
