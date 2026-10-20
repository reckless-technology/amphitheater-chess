// An engine over a game's positions: every position searched in order on a thread of its own, its score turned to
// White's side, a mate or a stalemate scored by the rules, a stop heard between positions, an engine that will not
// start reported; and each move judged by what it lost.

#include "match/GameAnalysis.h"

#include "chess/Notation.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <vector>

namespace amphitheater::match
{
namespace
{

/**
 * The fake engine, scripted.
 * @param arguments Its arguments.
 * @return The settings: the engine, and a short time for each position.
 */
GameAnalysisSettings Fake(std::vector<std::string> arguments)
{
    GameAnalysisSettings settings;
    settings.engine.executable = AMPHITHEATER_FAKE_ENGINE;
    settings.engine.arguments  = std::move(arguments);
    settings.engine.name       = "Fake";
    settings.move_time         = std::chrono::milliseconds(20);
    return settings;
}

/**
 * Every position's analysis, the analyser left to finish.
 * @param analyser The analyser, started.
 * @return What it did, in order.
 */
std::vector<PositionAnalysis> Drain(GameAnalyser &analyser)
{
    std::vector<PositionAnalysis> done;
    const auto                    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline)
    {
        while (auto next = analyser.Next())
        {
            done.push_back(std::move(*next));
        }
        if (analyser.IsFinished())
        {
            while (auto next = analyser.Next())
            {
                done.push_back(std::move(*next));
            }
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return done;
}

/**
 * A position's analysis with a score.
 * @param side_to_move Whose move it is.
 * @param centipawns The score from White's side.
 * @return The analysis, its score from the side to move's point of view as UCI gives it.
 */
PositionAnalysis At(chess::Color side_to_move, int centipawns)
{
    PositionAnalysis analysis;
    analysis.side_to_move = side_to_move;
    uci::Score score;
    score.value    = side_to_move == chess::kWhite ? centipawns : -centipawns;
    analysis.score = score;
    return analysis;
}

/**
 * A position's analysis with a mate.
 * @param side_to_move Whose move it is.
 * @param moves The mate, from the side to move's point of view: 0 for mated.
 * @return The analysis.
 */
PositionAnalysis Mate(chess::Color side_to_move, int moves)
{
    PositionAnalysis analysis;
    analysis.side_to_move = side_to_move;
    uci::Score score;
    score.kind     = uci::Score::Kind::kMate;
    score.value    = moves;
    analysis.score = score;
    return analysis;
}

} // namespace

TEST(GameAnalysis, EveryPositionIsSearchedInOrderItsScoreFromWhitesSide)
{
    GameAnalyser analyser(Fake({"--score", "35"}), chess::Position::Start(), {"e2e4", "e7e5", "g1f3"});
    EXPECT_EQ(analyser.PositionCount(), 4);
    analyser.Start();
    const std::vector<PositionAnalysis> done = Drain(analyser);
    ASSERT_EQ(done.size(), 4u);
    EXPECT_TRUE(analyser.Failure().empty()) << analyser.Failure();
    EXPECT_FALSE(analyser.EngineName().empty());
    for (int ply = 0; ply < 4; ++ply)
    {
        const PositionAnalysis &position = done[static_cast<std::size_t>(ply)];
        EXPECT_EQ(position.ply, ply);
        ASSERT_TRUE(position.score);
        EXPECT_EQ(position.score->value, 35) << "the engine's own word";
        EXPECT_EQ(position.side_to_move, ply % 2 == 0 ? chess::kWhite : chess::kBlack);
        EXPECT_EQ(WhiteWinningChances(*position.score, position.side_to_move) > 0.0, ply % 2 == 0)
            << "+35 for the side to move: White's chances when White is to move, Black's when Black is";
        EXPECT_FALSE(position.pv.empty());
        EXPECT_GE(position.depth, 1);
    }
}

TEST(GameAnalysis, AGamesLastPositionIsScoredByTheRulesWhenItEndsTheGame)
{
    // The fool's mate: White is mated after 2...Qh4#.
    GameAnalyser analyser(Fake({}), chess::Position::Start(), {"f2f3", "e7e5", "g2g4", "d8h4"});
    analyser.Start();
    const std::vector<PositionAnalysis> done = Drain(analyser);
    ASSERT_EQ(done.size(), 5u);
    ASSERT_TRUE(done.back().score);
    EXPECT_EQ(done.back().score->kind, uci::Score::Kind::kMate);
    EXPECT_EQ(done.back().score->value, 0) << "the side to move mated";
    EXPECT_EQ(WhiteWinningChances(*done.back().score, done.back().side_to_move), -1.0) << "White mated: lost";
    EXPECT_TRUE(done.back().pv.empty());
}

TEST(GameAnalysis, AStopEndsItBetweenPositionsAndAnEngineThatWillNotStartIsReported)
{
    GameAnalysisSettings slow = Fake({"--think", "200"});
    slow.move_time            = std::chrono::milliseconds(200);
    {
        GameAnalyser analyser(slow, chess::Position::Start(), {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5"});
        analyser.Start();
        analyser.Stop();
        const std::vector<PositionAnalysis> done = Drain(analyser);
        EXPECT_TRUE(analyser.IsFinished());
        EXPECT_LT(done.size(), 6u) << "stopped before the end";
    }
    GameAnalysisSettings missing = Fake({});
    missing.engine.executable    = "no-such-engine-anywhere";
    GameAnalyser analyser(missing, chess::Position::Start(), {"e2e4"});
    analyser.Start();
    EXPECT_TRUE(Drain(analyser).empty());
    EXPECT_NE(analyser.Failure().find("could not be started"), std::string::npos) << analyser.Failure();
}

TEST(GameAnalysis, AMoveIsJudgedByTheWinningChancesItCostItsMover)
{
    using chess::kBlack;
    using chess::kWhite;
    EXPECT_EQ(JudgeMove(At(kWhite, 30), At(kBlack, 20)), MoveQuality::kGood);
    EXPECT_EQ(JudgeMove(At(kWhite, 30), At(kBlack, -30)), MoveQuality::kInaccuracy);
    EXPECT_EQ(JudgeMove(At(kWhite, 30), At(kBlack, -90)), MoveQuality::kMistake);
    EXPECT_EQ(JudgeMove(At(kWhite, 30), At(kBlack, -300)), MoveQuality::kBlunder);
    EXPECT_EQ(JudgeMove(At(kBlack, -50), At(kWhite, 200)), MoveQuality::kBlunder) << "Black's move, White better after";
    EXPECT_EQ(JudgeMove(At(kBlack, 30), At(kWhite, -300)), MoveQuality::kGood) << "good for Black";
    EXPECT_EQ(JudgeMove(Mate(kWhite, 3), At(kBlack, 900)), MoveQuality::kGood) << "still winning: nothing thrown away";
    EXPECT_EQ(JudgeMove(Mate(kWhite, 3), At(kBlack, 0)), MoveQuality::kBlunder) << "a mate let go";
    EXPECT_EQ(JudgeMove(At(kWhite, 0), Mate(kBlack, 0)), MoveQuality::kGood) << "a mate given: Black mated";
    EXPECT_FALSE(JudgeMove(PositionAnalysis{}, At(kBlack, 0))) << "no score, no judgement";

    EXPECT_EQ(WhiteWinningChances(Mate(kBlack, 0).score.value(), kBlack), 1.0);
    EXPECT_EQ(WhiteWinningChances(Mate(kWhite, -2).score.value(), kWhite), -1.0);
    EXPECT_NEAR(WhiteWinningChances(At(kWhite, 0).score.value(), kWhite), 0.0, 1e-12);
    EXPECT_EQ(NagOf(MoveQuality::kBlunder), 4);
    EXPECT_EQ(NagOf(MoveQuality::kMistake), 2);
    EXPECT_EQ(NagOf(MoveQuality::kInaccuracy), 6);
    EXPECT_EQ(NagOf(MoveQuality::kGood), 0);
}

} // namespace amphitheater::match
