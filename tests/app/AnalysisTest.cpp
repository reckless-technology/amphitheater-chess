// Analyse, through the controller as the window drives it, against the fake engine: the engines started off the
// window's thread, their lines and their best move's arrow for the position shown, the search moved on as the person
// steps through the game, the graph filled in, an engine that will not start named, and the analysis stopped.

#include "app/AnalysisSession.h"
#include "app/Commands.h"
#include "app/Faces.h"
#include "app/MainController.h"
#include "app/MainView.h"
#include "app/PieceSet.h"
#include "chess/Notation.h"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <thread>

namespace uci_host::app
{
namespace
{

using namespace std::chrono_literals;

/** The pieces, read once for every test. */
const PieceSet &Pieces()
{
    static const PieceSet pieces = [] {
        InstallFaces();
        return PieceSet::FromEmbedded();
    }();
    return pieces;
}

/** The fake engine as a configured engine. */
EngineEntry Fake(std::vector<std::string> arguments, std::string name)
{
    EngineEntry entry;
    entry.config.executable = UCI_HOST_FAKE_ENGINE;
    entry.config.arguments  = std::move(arguments);
    entry.config.name       = std::move(name);
    return entry;
}

/** A window and its controller, with no engine list on disk. */
struct Rig
{
    MainView       view{Pieces()};
    MainController controller{view, ControllerFiles{{}}};

    /** Frames until a condition holds, or ten seconds. */
    bool FramesUntil(const std::function<bool()> &condition)
    {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            controller.Frame(1.0 / 60.0);
            if (condition())
            {
                return true;
            }
            std::this_thread::sleep_for(5ms);
        }
        return false;
    }

    /** @return The first engine block's best line's score, or empty. */
    std::string BestScore()
    {
        const auto &blocks = view.analysis().blocks();
        return blocks.empty() || blocks.front().lines.empty() ? std::string() : blocks.front().lines.front().score;
    }
};

} // namespace

TEST(Analysis, TheEnginesThinkAboutThePositionShownAndFollowItThroughTheGame)
{
    Rig rig;
    rig.controller.catalogue().engines = {Fake({"--score", "35"}, "Fake")};
    chess::Game game;
    game.PlaySan("e4");
    game.PlaySan("e5");
    rig.controller.ShowGame(game, {});
    rig.controller.ShowPly(0);
    rig.controller.Command(kAnalyse);
    ASSERT_NE(rig.controller.analysis(), nullptr);
    EXPECT_TRUE(rig.view.Plate(kAnalyse)->on());
    ASSERT_TRUE(rig.FramesUntil([&] { return rig.BestScore() == "+0.35"; }));
    EXPECT_EQ(rig.view.analysis().blocks().front().name, "Fake");
    EXPECT_EQ(rig.view.board().arrows().size(), 1u) << "the best move's arrow on the position shown";
    EXPECT_NE(rig.view.analysis().blocks().front().state.find("thinking"), std::string::npos);

    rig.controller.ShowPly(1);
    ASSERT_TRUE(rig.FramesUntil([&] { return rig.BestScore() == "-0.35"; })) << "Black to move: White's side";
    ASSERT_TRUE(rig.FramesUntil([&] { return rig.view.graph().scores().size() == 2 && rig.view.graph().scores()[0]; }));
    EXPECT_DOUBLE_EQ(*rig.view.graph().scores()[0], -0.35);

    rig.controller.Command(kAnalyse);
    EXPECT_EQ(rig.controller.analysis(), nullptr);
    EXPECT_FALSE(rig.view.Plate(kAnalyse)->on());
    EXPECT_TRUE(rig.view.analysis().blocks().empty());
}

TEST(Analysis, AnEngineThatWillNotStartIsNamedAndTheRestAnalyse)
{
    Rig         rig;
    EngineEntry missing;
    missing.config.executable          = "no-such-engine-anywhere";
    missing.config.name                = "Missing";
    rig.controller.catalogue().engines = {missing, Fake({}, "Fake")};
    rig.controller.SetAnalysing(true);
    ASSERT_TRUE(rig.FramesUntil([&] {
        for (const std::string &item : rig.view.status().items())
        {
            if (item.find("Missing: it could not be started") != std::string::npos)
            {
                return !rig.view.analysis().blocks().empty();
            }
        }
        return false;
    }));
    EXPECT_EQ(rig.view.analysis().blocks().size(), 1u);
}

TEST(Analysis, OnlyTheEnginesMarkedForItAnalyseAndWithNoneConfiguredItSaysSo)
{
    Rig rig;
    rig.controller.SetAnalysing(true);
    EXPECT_EQ(rig.controller.analysis(), nullptr);
    EXPECT_NE(rig.controller.last_error().find("No engine"), std::string::npos);

    EngineEntry marked                 = Fake({}, "Marked");
    EngineEntry unmarked               = Fake({}, "Unmarked");
    unmarked.is_analysing              = false;
    rig.controller.catalogue().engines = {unmarked, marked};
    rig.controller.SetAnalysing(true);
    ASSERT_NE(rig.controller.analysis(), nullptr);
    EXPECT_EQ(rig.controller.analysis()->Names(), std::vector<std::string>{"Marked"});
}

TEST(Analysis, TheReaperQuitsEnginesOffTheWindowsThread)
{
    EngineReaper reaper;
    {
        AnalysisSession session({Fake({}, "One"), Fake({}, "Two")}, 2, reaper);
        session.SetPosition(chess::Game{}, 0);
        session.Poll(std::chrono::steady_clock::now());
    }
    EXPECT_EQ(reaper.Pending(), 2u) << "both handed over as the session went";
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (reaper.Pending() > 0 && std::chrono::steady_clock::now() < deadline)
    {
        reaper.Collect();
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_EQ(reaper.Pending(), 0u);
}

} // namespace uci_host::app
