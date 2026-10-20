// Game > Analyse the game: the dialog's choice read and refused, the analysis over the line shown writing each score as
// an [%eval] command on the move that led to the position, a move that threw a game away marked ?? with the engine's
// line beside it, the graph and the status bar following it, the menu items, and a stop.

#include "app/Commands.h"
#include "app/Evaluations.h"
#include "app/Faces.h"
#include "app/GameAnalysisDialog.h"
#include "app/MainController.h"
#include "app/MainView.h"
#include "app/PieceSet.h"
#include "chess/Notation.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace amphitheater::app
{
namespace
{

/** The pieces, read once for every test. */
const PieceSet &Pieces()
{
    static const PieceSet pieces = [] {
        InstallFaces();
        return PieceSet::FromEmbedded();
    }();
    return pieces;
}

/**
 * The fake engine, as the engine list holds it.
 * @param arguments Its arguments.
 * @return The entry.
 */
EngineEntry Fake(std::vector<std::string> arguments)
{
    EngineEntry entry;
    entry.config.executable = AMPHITHEATER_FAKE_ENGINE;
    entry.config.arguments  = std::move(arguments);
    entry.config.name       = "Fake";
    return entry;
}

/** A window with its controller, showing 1.e4 e5 2.Qh5. */
struct Rig
{
    MainView       view{Pieces()};
    MainController controller{view, ControllerFiles{{}}};

    Rig()
    {
        view.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});
        chess::Game game;
        for (const char *san : {"e4", "e5", "Qh5"})
        {
            EXPECT_TRUE(game.PlaySan(san));
        }
        controller.ShowGame(game, {});
    }

    /**
     * Frames until the analysis is over.
     * @return Whether it ended within half a minute.
     */
    bool Finish()
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (controller.IsAnalysingGame() && std::chrono::steady_clock::now() < deadline)
        {
            controller.Frame(0.016);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        controller.Frame(0.016);
        return !controller.IsAnalysingGame();
    }

    /**
     * @param command A command.
     * @return Its menu item, or null.
     */
    const reckless::app::MenuEntry *Item(int command) const
    {
        for (const auto &menu : controller.menu_bar().menus)
        {
            for (const auto &entry : menu.submenu)
            {
                if (entry.id == command)
                {
                    return &entry;
                }
            }
        }
        return nullptr;
    }

    /** @return The status bar's phrases, joined. */
    std::string Status()
    {
        std::string text;
        for (const std::string &item : view.status().items())
        {
            text += item + " | ";
        }
        return text;
    }
};

/**
 * The choice to analyse with the fake engine, quickly.
 * @param arguments Its arguments.
 * @return The choice.
 */
GameAnalysisChoice Quick(std::vector<std::string> arguments)
{
    GameAnalysisChoice choice;
    choice.engine_name        = "Fake";
    choice.settings.engine    = Fake(std::move(arguments)).config;
    choice.settings.move_time = std::chrono::milliseconds(20);
    return choice;
}

} // namespace

TEST(GameAnalysisWindow, TheDialogReadsItsChoiceAndRefusesWhatItCannotUse)
{
    GameAnalysisDialog dialog({Fake({}), Fake({"--score", "5"})});
    auto               choice = dialog.Choice();
    ASSERT_TRUE(choice);
    EXPECT_EQ(choice->settings.move_time, std::chrono::milliseconds(1000)) << "a second a move at first";
    EXPECT_FALSE(choice->settings.depth);
    EXPECT_TRUE(choice->does_mark_moves);
    EXPECT_TRUE(choice->does_add_lines);

    dialog.depth().SetText("12");
    choice = dialog.Choice();
    ASSERT_TRUE(choice);
    EXPECT_EQ(choice->settings.depth, std::optional<int>(12)) << "a depth instead of the time";
    EXPECT_FALSE(choice->settings.move_time);

    dialog.depth().SetText("");
    dialog.seconds().SetText("soon");
    std::string error;
    EXPECT_FALSE(dialog.Choice(&error));
    EXPECT_NE(error.find("'soon'"), std::string::npos) << error;
    dialog.seconds().SetText("0.5");
    dialog.mark().Toggle();
    choice = dialog.Choice();
    ASSERT_TRUE(choice);
    EXPECT_EQ(choice->settings.move_time, std::chrono::milliseconds(500));
    EXPECT_FALSE(choice->does_mark_moves);
}

TEST(GameAnalysisWindow, AMoveThatThrowsTheGameAwayIsMarkedWithTheEnginesLineBesideIt)
{
    Rig rig;
    rig.controller.Frame(0.0);
    EXPECT_FALSE(rig.Item(kAnalyseGame)->enabled) << "no engine configured";
    rig.controller.catalogue().engines = {Fake({})};
    rig.controller.Frame(0.0);
    EXPECT_TRUE(rig.Item(kAnalyseGame)->enabled);

    // The engine's own scores, each from the side to move: +0.30, -0.30, +0.40, then +3.00 for Black after 2.Qh5.
    rig.controller.StartGameAnalysis(Quick({"--scores", "30,-30,40,300"}));
    EXPECT_TRUE(rig.controller.IsAnalysingGame());
    rig.controller.Frame(0.0);
    EXPECT_TRUE(rig.Item(kStopGameAnalysis)->enabled);
    EXPECT_NE(rig.Status().find("Analysing the game with Fake"), std::string::npos) << rig.Status();
    ASSERT_TRUE(rig.Finish());

    const chess::GameTree &tree = rig.controller.tree();
    const auto             line = rig.controller.line();
    ASSERT_EQ(line.size(), 3u);
    EXPECT_EQ(tree.node(line[0]).comment, "[%eval 0.30]") << "White's side";
    EXPECT_EQ(tree.node(line[1]).comment, "[%eval 0.40]") << "after 1...e5, White to move";
    EXPECT_EQ(tree.node(line[2]).comment, "[%eval -3.00]");
    EXPECT_EQ(tree.node(line[2]).nags, std::vector<int>{4}) << "a blunder";
    EXPECT_TRUE(tree.node(line[0]).nags.empty()) << "a good move unmarked";
    EXPECT_EQ(rig.view.moves().model().CellText(1, 1), "Qh5??") << "the glyph in the moves list";
    EXPECT_NE(rig.Status().find("Game analysed by Fake: 1 blunder, 0 mistakes, 0 inaccuracies"), std::string::npos)
        << rig.Status();

    // The engine's line beside the blunder, from the position before it.
    const auto &alternatives = tree.node(line[1]).children;
    ASSERT_EQ(alternatives.size(), 2u) << "the move played, and the engine's";
    EXPECT_EQ(tree.node(alternatives[1]).comment, "[%eval 0.40]");

    ASSERT_EQ(rig.view.graph().series().size(), 1u);
    EXPECT_EQ(rig.view.graph().series()[0].name, "Fake");
    ASSERT_TRUE(rig.view.graph().series()[0].points[2]);
    EXPECT_DOUBLE_EQ(rig.view.graph().series()[0].points[2]->pawns, -3.0);

    const std::string pgn = chess::WritePgn(rig.controller.CurrentPgn());
    EXPECT_NE(pgn.find("Qh5 $4 {[%eval -3.00]}"), std::string::npos) << pgn;
}

TEST(GameAnalysisWindow, MarksAndLinesAreLeftOutWhenNotAskedForAndAStopEndsIt)
{
    Rig                rig;
    GameAnalysisChoice choice = Quick({"--scores", "30,-30,40,300"});
    choice.does_mark_moves    = false;
    choice.does_add_lines     = false;
    rig.controller.StartGameAnalysis(choice);
    ASSERT_TRUE(rig.Finish());
    const auto line = rig.controller.line();
    EXPECT_TRUE(rig.controller.tree().node(line[2]).nags.empty());
    EXPECT_EQ(rig.controller.tree().node(line[1]).children.size(), 1u);
    EXPECT_EQ(rig.controller.tree().node(line[2]).comment, "[%eval -3.00]") << "the scores written all the same";

    GameAnalysisChoice slow = Quick({"--think", "400"});
    slow.settings.move_time = std::chrono::milliseconds(400);
    rig.controller.StartGameAnalysis(slow);
    rig.controller.Command(kStopGameAnalysis);
    ASSERT_TRUE(rig.Finish());
    EXPECT_NE(rig.Status().find("Game analysed by Fake"), std::string::npos) << rig.Status();
}

TEST(GameAnalysisWindow, AnEvalCommandReadsBackAsThePointItWasWrittenFrom)
{
    for (const EvalPoint point : {EvalPoint{0.35, std::nullopt}, EvalPoint{-1.2, std::nullopt},
                                  EvalPoint{kMatePawns, 3}, EvalPoint{-kMatePawns, -2}})
    {
        const std::string command = EvalCommand(point);
        const auto        read    = EvalPointOfComment(command);
        ASSERT_TRUE(read) << command;
        EXPECT_EQ(*read, point) << command;
    }
    EXPECT_EQ(EvalCommand(EvalPoint{0.35, std::nullopt}), "[%eval 0.35]");
    EXPECT_EQ(EvalCommand(EvalPoint{kMatePawns, 0}), "") << "a mate in 0 has none";
}

} // namespace amphitheater::app
