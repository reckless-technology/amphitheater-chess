// Matches and tournaments from the window, against the fake engine: the dialogs written as uci-host-cli's arguments and
// read by its own reader, a match played to its end with the board following its games and the score on the card, a
// game in progress chosen to watch, a tournament's standings and crosstable, and the dialogs' refusals.

#include "DialogButtons.h"
#include "app/Commands.h"
#include "app/CompetitionDialog.h"
#include "app/CompetitionSession.h"
#include "app/Faces.h"
#include "app/MainController.h"
#include "app/MainView.h"
#include "app/PieceSet.h"
#include "app/StandingsDialog.h"

#include <gtest/gtest.h>

#include <algorithm>
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
EngineEntry Fake(std::string name, std::vector<std::string> arguments = {})
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

    /** Frames until a condition holds, or thirty seconds. */
    bool FramesUntil(const std::function<bool()> &condition)
    {
        const auto deadline = std::chrono::steady_clock::now() + 30s;
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

    /** @return The card's line with this label, or empty. */
    std::string Card(const std::string &label)
    {
        for (const CardLine &line : view.card().lines())
        {
            if (line.label == label)
            {
                return line.value;
            }
        }
        return {};
    }
};

} // namespace

TEST(Competition, AnEnginesArgumentsAreTheCommandLinesKeys)
{
    EngineEntry entry        = Fake("Alpha", {"--seed", "3"});
    entry.config.options     = {{"Hash", "64"}};
    entry.config.does_ponder = true;
    entry.book_path          = "book.bin";
    entry.book_depth         = 12;
    entry.depth              = 9;
    const auto arguments     = EngineArguments(entry);
    EXPECT_EQ(arguments,
              (std::vector<std::string>{"cmd=" + std::string(UCI_HOST_FAKE_ENGINE), "name=Alpha", "arg=--seed", "arg=3",
                                        "option.Hash=64", "depth=9", "book=book.bin", "bookdepth=12", "ponder"}));
}

TEST(Competition, AMatchIsPlayedToItsEndWithTheBoardFollowingItsGames)
{
    Rig rig;
    // Each move takes 20 ms, so a game lasts many frames however fast the machine: an engine that answers at once can
    // finish both games between two frames, and the board never sees one in progress.
    rig.controller.catalogue().engines = {Fake("Alpha", {"--think", "20"}),
                                          Fake("Beta", {"--seed", "5", "--think", "20"})};
    rig.controller.Command(kNewMatch);
    auto *dialog = dynamic_cast<CompetitionDialog *>(rig.controller.dialog());
    ASSERT_NE(dialog, nullptr);
    dialog->Field("Time control")->SetText("10+0.05");
    dialog->Field("Games")->SetText("2");
    dialog->Field("Rounds")->SetText("1");
    dialog->Field("Max moves")->SetText("8");
    const auto arguments = dialog->Arguments();
    EXPECT_NE(std::find(arguments.begin(), arguments.end(), "-repeat"), arguments.end());
    EXPECT_EQ(std::count(arguments.begin(), arguments.end(), "-engine"), 2);
    const auto options = dialog->Options();
    ASSERT_TRUE(options.has_value());
    EXPECT_EQ(options->settings.adjudication.max_moves, 8);
    dialog->Start();
    rig.controller.Frame(0.0);
    EXPECT_EQ(rig.controller.dialog(), nullptr);
    ASSERT_NE(rig.controller.competition(), nullptr);
    EXPECT_TRUE(rig.controller.Menus().is_match_running);

    bool has_followed = false;
    ASSERT_TRUE(rig.FramesUntil([&] {
        has_followed |= rig.controller.watched_game() != 0;
        return rig.controller.competition()->IsFinished();
    }));
    rig.controller.Frame(0.0);
    EXPECT_TRUE(has_followed) << "the board followed a game in progress";
    EXPECT_EQ(rig.controller.competition()->Results().size(), 2u);
    EXPECT_NE(rig.Card("Match score").find("Alpha"), std::string::npos);
    EXPECT_EQ(rig.view.board().banner(), "Finished");
    EXPECT_FALSE(rig.controller.Menus().is_match_running);
    EXPECT_FALSE(rig.view.log().model().RowCount() == 0);
}

TEST(Competition, AGameInProgressIsChosenToWatch)
{
    Rig                       rig;
    match::TournamentSettings settings;
    settings.engines                      = {{Fake("Alpha").config}, {Fake("Beta", {"--seed", "5"}).config}};
    settings.engines[0].config.arguments  = {"--think", "30"};
    settings.engines[1].config.arguments  = {"--think", "30"};
    settings.time_control                 = *chess::TimeControl::Parse("10+0.1");
    settings.schedule.games_per_encounter = 2;
    settings.concurrency                  = 2;
    settings.adjudication.max_moves       = 6;
    rig.controller.StartCompetition(settings, true);
    ASSERT_TRUE(rig.FramesUntil([&] { return rig.controller.competition()->live().size() == 2; }));
    EXPECT_EQ(rig.controller.Menus().watchable_games.size(), 2u);
    rig.controller.Command(kWatchGameBase + 2);
    EXPECT_EQ(rig.controller.watched_game(), 2);
    EXPECT_NE(rig.Card("Event").find("game 2 of 2"), std::string::npos);
    rig.controller.Command(kStopMatch);
    ASSERT_TRUE(rig.FramesUntil([&] { return rig.controller.competition()->IsFinished(); }));
}

TEST(Competition, ATournamentsStandingsAndCrosstableFollowItsResults)
{
    Rig rig;
    rig.controller.catalogue().engines = {Fake("Alpha"), Fake("Beta", {"--seed", "5"}), Fake("Gamma", {"--seed", "9"})};
    rig.controller.Command(kNewTournament);
    auto *dialog = dynamic_cast<CompetitionDialog *>(rig.controller.dialog());
    ASSERT_NE(dialog, nullptr);
    ASSERT_NE(dialog->EngineBox(2), nullptr);
    dialog->Field("Time control")->SetText("10+0.05");
    dialog->Field("Games")->SetText("1");
    dialog->Field("Max moves")->SetText("6");
    dialog->Field("Concurrency")->SetText("3");
    dialog->Box("Repeat")->SetChecked(false);
    dialog->Start();
    rig.controller.Frame(0.0);
    ASSERT_NE(rig.controller.competition(), nullptr);
    EXPECT_EQ(rig.controller.competition()->total_games(), 3);
    ASSERT_TRUE(rig.FramesUntil([&] { return rig.controller.competition()->IsFinished(); }));
    EXPECT_TRUE(rig.controller.Menus().has_standings);
    rig.controller.Command(kShowStandings);
    auto *standings = dynamic_cast<StandingsDialog *>(rig.controller.dialog());
    ASSERT_NE(standings, nullptr);
    EXPECT_EQ(standings->standings_model().RowCount(), 3u);
    EXPECT_EQ(standings->crosstable_model().RowCount(), 3u);
    EXPECT_EQ(standings->standings().columns().size(), 9u);
    EXPECT_EQ(standings->crosstable().columns().size(), 4u);
    int diagonal = 0;
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 1; column <= 3; ++column)
        {
            const std::string cell = standings->crosstable_model().CellText(row, column);
            diagonal += cell == "\xc2\xb7" ? 1 : 0;
            if (cell != "\xc2\xb7")
            {
                EXPECT_NE(cell.find("/1"), std::string::npos) << "one game against each";
            }
        }
    }
    EXPECT_EQ(diagonal, 3);
    EXPECT_FALSE(standings->standings_model().CellText(0, 1).empty());
}

TEST(Competition, TheDialogsRefuseWhatTheCommandLineRefuses)
{
    Rig rig;
    rig.controller.catalogue().engines = {Fake("Alpha"), Fake("Beta")};
    rig.controller.Command(kNewMatch);
    auto *dialog = dynamic_cast<CompetitionDialog *>(rig.controller.dialog());
    ASSERT_NE(dialog, nullptr);
    dialog->Field("Time control")->SetText("not a clock");
    dialog->Start();
    EXPECT_NE(dialog->note().find("not a clock"), std::string::npos) << dialog->note();
    EXPECT_EQ(rig.controller.competition(), nullptr);
    dialog->Field("Time control")->SetText("10+0.1");
    dialog->Field("Arguments")->SetText("-concurrency zero");
    std::string error;
    EXPECT_FALSE(dialog->Options(&error).has_value());
    EXPECT_FALSE(error.empty());
    dialog->on_cancel();

    match::TournamentSettings lonely;
    lonely.engines = {{Fake("Alpha").config}};
    rig.controller.StartCompetition(lonely, true);
    EXPECT_FALSE(rig.controller.last_error().empty());
    EXPECT_EQ(PointsText(2.5), "2\xc2\xbd");
    EXPECT_EQ(PointsText(0.5), "\xc2\xbd");
    EXPECT_EQ(PointsText(3.0), "3");
}

TEST(Competition, TheStandingsReadEveryColumnAndFollowTheResultsAsTheyCome)
{
    std::vector<std::string>            names = {"Alpha", "Beta", "Gamma"};
    std::vector<match::GameResultEntry> results;
    int                                 asked = 0;
    StandingsDialog                     dialog([&] {
        ++asked;
        return std::make_pair(names, results);
    });
    dialog.SetBounds({0, 0, 1200, 900});
    EXPECT_EQ(dialog.standings_model().RowCount(), 3u) << "every engine, before any game";

    results.push_back({0, 1, chess::GameResult::kWhiteWins});
    results.push_back({1, 0, chess::GameResult::kDraw});
    results.push_back({2, 0, chess::GameResult::kNone});
    dialog.Frame();
    dialog.Frame(); // nothing new: the tables stay as they are
    const StandingsModel &standings = dialog.standings_model();
    ASSERT_EQ(standings.RowCount(), 3u);
    const std::vector<std::string> leader = {"1", "Alpha", "1\xc2\xbd", "2", "75.0 %", "1", "1", "0"};
    for (std::size_t column = 0; column < leader.size(); ++column)
    {
        EXPECT_EQ(standings.CellText(0, column), leader[column]) << "column " << column;
    }
    EXPECT_NE(standings.CellText(0, 8).find('.'), std::string::npos) << "the Sonneborn-Berger figure";

    const CrosstableModel &crosstable = dialog.crosstable_model();
    EXPECT_EQ(crosstable.CellText(0, 0), "Alpha");
    EXPECT_EQ(crosstable.CellText(0, 1), "\xc2\xb7") << "against itself";
    EXPECT_EQ(crosstable.CellText(0, 2), "1\xc2\xbd/2");
    EXPECT_EQ(crosstable.CellText(0, 9), "") << "past the last column";
    std::size_t gamma = 0;
    for (std::size_t row = 0; row < 3; ++row)
    {
        gamma = crosstable.CellText(row, 0) == "Gamma" ? row : gamma;
    }
    EXPECT_EQ(crosstable.CellText(gamma, 1), "") << "an unfinished game counts for neither";

    int closed      = 0;
    dialog.on_close = [&] { ++closed; };
    test::Press(*test::ButtonOf(dialog, "Close"));
    EXPECT_TRUE(dialog.KeyPressed({reckless::view::Key::kReturn, 0, {}}));
    EXPECT_TRUE(dialog.KeyPressed({reckless::view::Key::kEscape, 0, {}}));
    EXPECT_EQ(closed, 3);
}

} // namespace uci_host::app
