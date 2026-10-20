// Against a real engine, when AMPHITHEATER_TEST_ENGINE names one (skipped otherwise): the handshake, a search, and a
// short match of the engine against itself with no game lost to a crash, a stall or an illegal move.

#include "TestSupport.h"
#include "chess/Notation.h"
#include "match/Tournament.h"
#include "uci/Engine.h"

#include <gtest/gtest.h>

namespace amphitheater
{
namespace
{

using namespace std::chrono_literals;

std::optional<std::string> RealEngine()
{
    return testing::EnvironmentVariable("AMPHITHEATER_TEST_ENGINE");
}

TEST(RealEngine, HandshakeAndSearch)
{
    const auto path = RealEngine();
    if (!path)
    {
        GTEST_SKIP() << "AMPHITHEATER_TEST_ENGINE names no engine";
    }
    uci::EngineConfig config;
    config.executable = *path;
    uci::Engine engine(config);
    const auto  started = engine.Start();
    ASSERT_TRUE(started) << started.ErrorMessage();
    EXPECT_FALSE(engine.Name().empty());
    std::printf("engine: %s by %s, %zu options\n", engine.Name().c_str(), engine.Author().c_str(),
                engine.Options().size());
    ASSERT_TRUE(engine.NewGame());
    // An endgame no opening book holds, so the engine searches and says so.
    const std::string fen = "8/5pk1/6p1/8/3R4/6P1/5PK1/3r4 w - - 0 40";
    engine.SetPosition(fen, {"d4d7"});
    uci::GoParameters parameters;
    parameters.move_time = 200ms;
    const auto result    = engine.Search(parameters, std::chrono::steady_clock::now() + 10s);
    ASSERT_EQ(result.ending, uci::SearchResult::Ending::kBestMove);
    auto position = *chess::Position::FromFen(fen);
    position      = position.MakeMove(*chess::ParseUciMove(position, "d4d7"));
    EXPECT_TRUE(chess::ParseUciMove(position, result.best_move.move)) << result.best_move.move;
    EXPECT_TRUE(result.info.depth.has_value());
    EXPECT_TRUE(result.info.score.has_value());
}

TEST(RealEngine, AShortMatchAgainstItself)
{
    const auto path = RealEngine();
    if (!path)
    {
        GTEST_SKIP() << "AMPHITHEATER_TEST_ENGINE names no engine";
    }
    match::TournamentSettings settings;
    for (const char *name : {"First", "Second"})
    {
        match::TournamentEngine engine;
        engine.config.executable = *path;
        engine.config.name       = name;
        settings.engines.push_back(engine);
    }
    settings.time_control                 = *chess::TimeControl::Parse("2+0.05");
    settings.schedule.games_per_encounter = 2;
    settings.concurrency                  = 2;
    settings.time_margin                  = 100ms;
    match::Tournament tournament(settings);
    ASSERT_TRUE(tournament.Start());
    while (auto event = tournament.Events().PopFor(120s))
    {
        if (event->kind == match::TournamentEvent::Kind::kGameFinished)
        {
            std::printf("game %d: %s {%s}\n", event->game_number, chess::ResultText(event->outcome.result).c_str(),
                        event->outcome.description.c_str());
            EXPECT_NE(event->outcome.termination, chess::Termination::kDisconnection);
            EXPECT_NE(event->outcome.termination, chess::Termination::kStalled);
            EXPECT_NE(event->outcome.termination, chess::Termination::kIllegalMove);
            EXPECT_NE(event->outcome.termination, chess::Termination::kAbandoned);
        }
    }
    EXPECT_EQ(tournament.Results().size(), 2u);
}

} // namespace
} // namespace amphitheater
