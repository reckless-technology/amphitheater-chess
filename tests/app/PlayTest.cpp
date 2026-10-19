// Games on the board, through the controller as the window drives it, against the fake engine: the person's moves
// handed to the game's thread, the engine's replies taken in once a frame, the clocks, a take-back, a stop, a game
// that ends, a move on the board with no game played, and the new game's dialog.

#include "app/Commands.h"
#include "app/Faces.h"
#include "app/MainController.h"
#include "app/MainView.h"
#include "app/NewGameDialog.h"
#include "app/PieceSet.h"
#include "app/PlaySession.h"
#include "app/SearchTracker.h"
#include "chess/Notation.h"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <string>
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
EngineEntry Fake(std::vector<std::string> arguments = {}, std::string name = "Fake")
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

    /**
     * Frames until a condition holds, or ten seconds.
     * @param condition The condition.
     * @return Whether it came to hold.
     */
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

    /** @return Whether the board waits for the person at its last position. */
    bool PersonToMove()
    {
        return controller.IsPlaying() && view.board().interactive();
    }
};

/**
 * A move in UCI notation, in the controller's game's current position.
 * @param rig The rig.
 * @param text The move.
 * @return The move.
 */
chess::Move MoveOf(Rig &rig, const char *text)
{
    return *chess::ParseUciMove(rig.controller.game().CurrentPosition(), text);
}

} // namespace

TEST(Play, APersonPlaysAnEngineOnTheBoardAndTakesAMoveBack)
{
    Rig          rig;
    PlaySettings settings;
    settings.white.is_human = true;
    settings.black.is_human = false;
    settings.black.engine   = Fake({"--name", "Fake"});
    settings.time_control   = *chess::TimeControl::Parse("60+1");
    rig.controller.StartGame(settings);
    ASSERT_TRUE(rig.FramesUntil([&] { return rig.PersonToMove(); }));
    EXPECT_FALSE(rig.controller.flipped()) << "the person plays White from the bottom";
    EXPECT_EQ(rig.view.bottom_strip().state().name, "You");
    EXPECT_FALSE(rig.view.bottom_strip().state().clock.empty()) << "a clock under a time control";
    EXPECT_TRUE(rig.view.bottom_strip().state().is_running) << "White's clock runs";

    rig.view.board().on_move(MoveOf(rig, "e2e4"));
    EXPECT_EQ(rig.controller.game().PlyCount(), 1) << "shown at once";
    ASSERT_TRUE(rig.FramesUntil([&] { return rig.controller.game().PlyCount() == 2 && rig.PersonToMove(); }));
    EXPECT_FALSE(rig.view.log().model().RowCount() == 0) << "the engine's lines are in the log";
    EXPECT_TRUE(rig.view.top_strip().state().name == "Fake");

    rig.controller.Command(kTakeBack);
    ASSERT_TRUE(rig.FramesUntil([&] { return rig.controller.game().PlyCount() == 0 && rig.PersonToMove(); }))
        << "the person's move and the engine's reply both go";
    rig.view.board().on_move(MoveOf(rig, "d2d4"));
    ASSERT_TRUE(rig.FramesUntil([&] { return rig.controller.game().PlyCount() == 2; }));
    EXPECT_EQ(rig.controller.game().San(0), "d4");

    rig.controller.Command(kStopGame);
    EXPECT_FALSE(rig.controller.IsPlaying());
    EXPECT_EQ(rig.view.board().banner(), "Stopped");
    rig.controller.Command(kBack);
    EXPECT_EQ(rig.controller.view_ply(), 1);
}

TEST(Play, ATakeBackResumesBothClocksFromTheTimesRecordedAtThatMove)
{
    Rig          rig;
    PlaySettings settings;
    settings.white.is_human = true;
    settings.black.is_human = false;
    settings.black.engine   = Fake({"--name", "Fake"});
    settings.time_control   = *chess::TimeControl::Parse("60+1");
    rig.controller.StartGame(settings);
    ASSERT_TRUE(rig.FramesUntil([&] { return rig.PersonToMove(); }));
    std::this_thread::sleep_for(300ms);
    rig.view.board().on_move(MoveOf(rig, "e2e4"));
    ASSERT_TRUE(rig.FramesUntil([&] { return rig.controller.game().PlyCount() == 2 && rig.PersonToMove(); }));
    std::this_thread::sleep_for(300ms);
    rig.view.board().on_move(MoveOf(rig, "d2d4"));
    ASSERT_TRUE(rig.FramesUntil([&] { return rig.controller.game().PlyCount() == 4 && rig.PersonToMove(); }));

    const PlaySnapshot played = rig.controller.play()->Snapshot();
    ASSERT_EQ(played.clocks.size(), 5u) << "a reading at the start and after every move";
    EXPECT_EQ(played.clocks[0].white_remaining, 60000ms);
    EXPECT_EQ(played.clocks[0].black_remaining, 60000ms);
    EXPECT_NE(played.clocks[2].white_remaining, 60000ms) << "White's thinking and increment are on it";
    const ClockReading at_two = played.clocks[2];

    std::this_thread::sleep_for(300ms); // White's clock runs on: none of this comes back
    rig.controller.Command(kTakeBack);
    ASSERT_TRUE(rig.FramesUntil([&] { return rig.controller.game().PlyCount() == 2 && rig.PersonToMove(); }));
    const PlaySettings &once = rig.controller.play()->settings();
    ASSERT_TRUE(once.white_resumed && once.black_resumed);
    EXPECT_EQ(once.white_resumed->sessions[0].time, at_two.white_remaining);
    EXPECT_EQ(once.black_resumed->sessions[0].time, at_two.black_remaining);
    EXPECT_EQ(once.resumed_clocks.size(), 3u);
    EXPECT_EQ(rig.controller.play()->Snapshot().clocks.size(), 3u);

    rig.controller.Command(kTakeBack);
    ASSERT_TRUE(rig.FramesUntil([&] { return rig.controller.game().PlyCount() == 0 && rig.PersonToMove(); }));
    const PlaySettings &twice = rig.controller.play()->settings();
    ASSERT_TRUE(twice.white_resumed && twice.black_resumed);
    EXPECT_EQ(twice.white_resumed->sessions[0].time, 60000ms) << "back to the start: the clocks as they began";
    EXPECT_EQ(twice.black_resumed->sessions[0].time, 60000ms);
    rig.controller.Command(kStopGame);
}

TEST(Play, AnEngineGameEndsAndTheResultIsShown)
{
    Rig          rig;
    PlaySettings settings;
    settings.white.is_human = false;
    settings.white.engine   = Fake({"--seed", "3"}, "Alpha");
    settings.black.is_human = false;
    settings.black.engine   = Fake({"--crash-at", "3"}, "Beta");
    settings.time_control   = *chess::TimeControl::Parse("10+0.1");
    rig.controller.StartGame(settings);
    ASSERT_TRUE(rig.FramesUntil([&] { return !rig.controller.IsPlaying(); }));
    EXPECT_EQ(rig.controller.game().GetOutcome().result, chess::GameResult::kWhiteWins);
    EXPECT_EQ(rig.view.board().banner(), "1-0");
    EXPECT_FALSE(rig.view.board().interactive() && rig.controller.IsPlaying());
    EXPECT_GE(rig.controller.game().PlyCount(), 4);
}

TEST(Play, WithNoGamePlayedAMoveOnTheBoardTakesTheGameOnFromThePositionShown)
{
    Rig         rig;
    chess::Game game;
    for (const char *san : {"e4", "e5", "Nf3"})
    {
        game.PlaySan(san);
    }
    rig.controller.ShowGame(game, {});
    rig.controller.ShowPly(2);
    EXPECT_TRUE(rig.view.board().interactive());
    rig.view.board().on_move(*chess::ParseUciMove(rig.controller.game().PositionAt(2), "f2f4"));
    EXPECT_EQ(rig.controller.game().PlyCount(), 3);
    EXPECT_EQ(rig.controller.game().San(2), "f4") << "Nf3 is cut";
    rig.controller.Command(kTakeBack);
    EXPECT_EQ(rig.controller.game().PlyCount(), 2);
}

TEST(Play, TheNewGameDialogOffersThePersonAndEveryEngineAndReadsTheTimeControl)
{
    Rig rig;
    rig.controller.catalogue().engines = {Fake({}, "Alpha"), Fake({}, "Beta")};
    rig.controller.Command(kNewGame);
    auto *dialog = dynamic_cast<NewGameDialog *>(rig.controller.dialog());
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->white().choices().size(), 3u);
    EXPECT_EQ(dialog->black().Text(), "Alpha");
    const int start_row = dialog->start().bounds().y();
    dialog->time().Choose(static_cast<int>(TimeControlPresets().size()) - 1);
    EXPECT_TRUE(dialog->custom().visible());
    EXPECT_GT(dialog->start().bounds().y(), start_row) << "the custom line takes a row of its own";
    dialog->custom().SetText("not a clock");
    std::string error;
    EXPECT_FALSE(dialog->Settings(&error).has_value());
    EXPECT_NE(error.find("cannot be read"), std::string::npos);
    dialog->custom().SetText("120+2");
    const auto settings = dialog->Settings();
    ASSERT_TRUE(settings.has_value());
    EXPECT_TRUE(settings->white.is_human);
    EXPECT_EQ(settings->black.engine.DisplayName(), "Alpha");
    EXPECT_EQ(settings->time_control.ToString(), "120+2");
    dialog->on_cancel();
    rig.controller.Frame(0.0);
    EXPECT_EQ(rig.controller.dialog(), nullptr);
}

TEST(Play, ATimeControlResumedFromAClockStartsFromWhatIsLeft)
{
    const auto control = *chess::TimeControl::Parse("40/5400,1800+30");
    const auto resumed = ResumedTimeControl(control, 1234ms, 12, 0ms);
    ASSERT_EQ(resumed.sessions.size(), 2u);
    EXPECT_EQ(resumed.sessions[0].moves, 12);
    EXPECT_EQ(resumed.sessions[0].time, 1234ms);
    EXPECT_EQ(resumed.sessions[1], control.sessions.back());
    const auto single = ResumedTimeControl(*chess::TimeControl::Parse("300+3"), 5000ms, 0, 3000ms);
    ASSERT_EQ(single.sessions.size(), 1u);
    EXPECT_EQ(single.sessions[0].increment, 3000ms);
    EXPECT_TRUE(ResumedTimeControl(chess::TimeControl{}, 1ms, 0, 0ms).sessions.empty());
}

TEST(SearchTracker, FollowsAnEnginesLinesIntoItsBlock)
{
    SearchTracker tracker("Pawnstar");
    const auto    now = std::chrono::steady_clock::now();
    tracker.Feed(true, "position startpos moves e2e4 c7c5", now);
    tracker.Feed(true, "go wtime 1000 btime 1000", now);
    EXPECT_TRUE(tracker.IsSearching());
    tracker.Feed(false,
                 "info depth 12 seldepth 18 multipv 1 score cp 31 nodes 2500000 nps 1000000 hashfull 341 pv g1f3 d7d6",
                 now);
    tracker.Feed(false, "info depth 12 multipv 2 score cp -15 pv b1c3", now);
    const EngineBlock block = tracker.Block(now + 1500ms);
    EXPECT_EQ(block.state, "thinking 1.5 s");
    EXPECT_EQ(block.stats, "depth 12/18 \xc2\xb7 2.5 Mn \xc2\xb7 1.0 Mn/s \xc2\xb7 hash 34 %");
    ASSERT_EQ(block.lines.size(), 2u);
    EXPECT_EQ(block.lines[0].pv, "2.Nf3 d6");
    EXPECT_EQ(block.lines[0].score, "+0.31");
    EXPECT_EQ(block.lines[1].sign, -1);
    EXPECT_EQ(chess::MoveToUci(*tracker.BestMove()), "g1f3");
    tracker.Feed(true, "position startpos moves e2e4 c7c5 g1f3", now);
    tracker.Feed(true, "go ponder wtime 1000 btime 1000", now);
    EXPECT_TRUE(tracker.IsPondering());
    EXPECT_EQ(tracker.Block(now).state, "pondering 2.Nf3");
    tracker.Feed(false, "info depth 3 score cp 20 pv d7d6", now);
    EXPECT_EQ(tracker.BestScoreText(), std::optional<std::string>("-0.20")) << "Black to move: White's side";
    tracker.Feed(true, "ponderhit", now);
    EXPECT_FALSE(tracker.IsPondering());
    tracker.Feed(false, "bestmove d7d6", now);
    EXPECT_FALSE(tracker.IsSearching());
    EXPECT_EQ(PvToSan(chess::Position::Start(), {"e2e4", "e7e5", "zzzz"}), "1.e4 e5");
    EXPECT_EQ(NodesText(812), "812 n");
    EXPECT_EQ(NodesText(1050000000), "1.05 Gn");
}

TEST(SearchTracker, ReadsAFensPositionMateScoresAndRefusesWhatItCannotRead)
{
    SearchTracker tracker("Mater");
    const auto    now = std::chrono::steady_clock::now();
    EXPECT_FALSE(tracker.BestWhitePawns().has_value());
    EXPECT_FALSE(tracker.BestScoreText().has_value());
    tracker.Feed(true, "", now);
    tracker.Feed(true, "position", now);
    tracker.Feed(true, "position fen not a fen at all", now);
    tracker.Feed(true, "position somewhere", now);
    tracker.Feed(true, "position startpos moves e2e5", now);
    EXPECT_EQ(tracker.position().ToFen(), chess::Position::Start().ToFen()) << "nothing it could not read was taken";

    // Black to move, a rook on the second rank.
    tracker.Feed(true, "position fen 6k1/5ppp/8/8/8/8/r4PPP/6K1 w - - 0 1 moves h2h3", now);
    tracker.Feed(true, "go infinite", now);
    tracker.Feed(false, "info depth 5 multipv 1 score mate 2 pv a2a1 g1h2 a1a3", now);
    tracker.Feed(false, "info depth 5 multipv 2 score mate -3 pv h7h6", now);
    tracker.Feed(false, "info depth 5 multipv 3 score cp 0 pv g7g6", now);
    EXPECT_EQ(tracker.BestScoreText(), std::optional<std::string>("#-2")) << "a mate for Black, from White's side";
    ASSERT_TRUE(tracker.BestWhitePawns().has_value());
    EXPECT_LT(*tracker.BestWhitePawns(), -10.0) << "a mate is off the graph's scale";
    const EngineBlock block = tracker.Block(now, 2);
    ASSERT_EQ(block.lines.size(), 2u) << "held to the lines asked for";
    EXPECT_EQ(block.lines[1].score, "#3");
    EXPECT_EQ(block.stats, "depth 5");
    EXPECT_EQ(PvToSan(tracker.position(), {"a2a1", "g1h2", "a1a3"}, 2), "1...Ra1+ 2.Kh2");

    const uci::Score white_mates{uci::Score::Kind::kMate, 1};
    EXPECT_GT(WhitePawns(white_mates, chess::kWhite), 10.0);
    EXPECT_EQ(WhiteScoreText(white_mates, chess::kWhite), "#1");
    tracker.End();
    EXPECT_FALSE(tracker.IsSearching());
}

} // namespace uci_host::app
