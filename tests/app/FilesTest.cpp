// File and Edit through the controller: a PGN file opened - its one game shown, or a list to pick from - and saved
// back with its tags, comments and variations; a game played on the board saved with its clocks and scores; FEN and
// PGN copied to the clipboard and pasted from it.

#include "DialogButtons.h"
#include "app/Commands.h"
#include "app/Faces.h"
#include "app/GameFiles.h"
#include "app/GamePickerDialog.h"
#include "app/MainController.h"
#include "app/MainView.h"
#include "app/PieceSet.h"
#include "chess/Notation.h"
#include "match/Openings.h"

#include "ScratchPath.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace amphitheater::app
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

/** A window and its controller on a clipboard and a file dialog of the suite's own. */
struct Rig
{
    std::string    clipboard;
    std::string    chosen; ///< What the file dialog answers.
    MainView       view{Pieces()};
    MainController controller{view, ControllerFiles{{}}, Services()};

    /** @return The services, on this rig's clipboard and dialog. */
    ControllerServices Services()
    {
        ControllerServices services;
        services.read_clipboard  = [this] { return clipboard; };
        services.write_clipboard = [this](const std::string &text) {
            clipboard = text;
            return true;
        };
        services.choose_file = [this](const reckless::view::FileRequest &,
                                      std::function<void(const std::string &)> picked) { picked(chosen); };
        return services;
    }
};

/** A file of the suite's own with this text, its path. */
std::string WriteFile(const std::string &name, const std::string &text)
{
    const auto path = reckless::test::ScratchPath(name);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
    return path.string();
}

const char *const kAnnotated = R"([Event "Club championship"]
[Site "Here"]
[Date "2026.10.01"]
[Round "3"]
[White "Alpha"]
[Black "Beta"]
[Result "1-0"]
[ECO "C20"]
[Opening "King's pawn game"]

1. e4 {the best by test} e5 (1... c5 2. Nf3) 2. Qh5 Nc6 3. Bc4 Nf6 $2 4. Qxf7# 1-0

)";

} // namespace

TEST(Files, AGameOpenedFromPgnIsShownAndSavedBackWithItsCommentsAndVariations)
{
    Rig               rig;
    const std::string file = WriteFile("amphitheater-files/annotated.pgn", kAnnotated);
    rig.chosen             = file;
    rig.controller.Command(kOpenPgn);
    EXPECT_EQ(rig.controller.game().PlyCount(), 7);
    EXPECT_EQ(rig.view.bottom_strip().state().name, "Alpha");
    EXPECT_TRUE(rig.controller.game().IsOver());
    bool has_opening = false;
    for (const CardLine &line : rig.view.card().lines())
    {
        has_opening |= line.label == "Opening" && line.value == "C20 King's pawn game";
    }
    EXPECT_TRUE(has_opening);

    const std::string out = reckless::test::ScratchPath("amphitheater-files/saved.pgn").string();
    rig.chosen            = out;
    rig.controller.Command(kSavePgn);
    const auto saved = match::ReadTextFile(out);
    ASSERT_TRUE(saved);
    const auto original = chess::ReadPgn(kAnnotated);
    ASSERT_EQ(original.size(), 1u);
    EXPECT_EQ(*saved, chess::WritePgn(original.front())) << "the round trip keeps the file's game whole";
    EXPECT_NE(saved->find("{the best by test}"), std::string::npos);
    EXPECT_NE(saved->find("(1... c5 2. Nf3)"), std::string::npos);

    // A move made from the middle of the game is a variation: the save keeps the game whole, the move beside its own.
    rig.controller.ShowPly(2);
    rig.view.board().on_move(*chess::ParseUciMove(rig.controller.game().PositionAt(2), "g1f3"));
    const std::string changed = chess::WritePgn(rig.controller.CurrentPgn());
    EXPECT_NE(changed.find("the best by test"), std::string::npos);
    EXPECT_NE(changed.find("2. Qh5 (2. Nf3) 2... Nc6"), std::string::npos) << changed;
    EXPECT_NE(changed.find("[White \"Alpha\"]"), std::string::npos);
    EXPECT_NE(changed.find("1-0"), std::string::npos) << "the main line's result kept";
}

TEST(Files, AFileOfManyGamesOffersAListAndShowsThePickedOne)
{
    Rig         rig;
    std::string text = kAnnotated;
    text += "[Event \"Second\"]\n[White \"Gamma\"]\n[Black \"Delta\"]\n[Result \"1/2-1/2\"]\n\n1. d4 d5 1/2-1/2\n\n";
    text += "[Event \"Broken\"]\n\n1. e4 Ke7 2. Nf9 *\n\n";
    rig.controller.OpenPgn(WriteFile("amphitheater-files/many.pgn", text));
    auto *picker = dynamic_cast<GamePickerDialog *>(rig.controller.dialog());
    ASSERT_NE(picker, nullptr);
    EXPECT_EQ(picker->games().size(), 2u) << "the broken game is skipped";
    GamesModel model(picker->games());
    EXPECT_EQ(model.CellText(1, 1), "Gamma");
    EXPECT_EQ(model.CellText(1, 3), "1/2-1/2");
    EXPECT_EQ(model.CellText(1, 6), "1");
    picker->Open(1);
    rig.controller.Frame(0.0);
    EXPECT_EQ(rig.controller.dialog(), nullptr);
    EXPECT_EQ(rig.controller.game().PlyCount(), 2);
    EXPECT_EQ(rig.view.bottom_strip().state().name, "Gamma");

    rig.controller.OpenPgn(WriteFile("amphitheater-files/empty.pgn", "nothing here"));
    EXPECT_NE(rig.controller.last_error().find("no game"), std::string::npos);
    rig.controller.OpenPgn(reckless::test::ScratchPath("amphitheater-files/missing.pgn").string());
    EXPECT_NE(rig.controller.last_error().find("could not be read"), std::string::npos);
}

TEST(Files, TheWindowsTitleFollowsTheGameAndItsFile)
{
    Rig rig;
    EXPECT_EQ(rig.controller.WindowTitle(), "White vs Black - Reckless Amphitheater") << "a fresh window";
    rig.controller.OpenPgn(WriteFile("amphitheater-files/title.pgn", kAnnotated));
    EXPECT_EQ(rig.controller.WindowTitle(), "Alpha vs Beta - title.pgn - Reckless Amphitheater");

    rig.chosen = reckless::test::ScratchPath("amphitheater-files/renamed.pgn").string();
    rig.controller.Command(kSavePgn);
    EXPECT_EQ(rig.controller.WindowTitle(), "Alpha vs Beta - renamed.pgn - Reckless Amphitheater")
        << "saved under a name";

    rig.clipboard = "8/8/8/8/8/8/8/k3K3 w - - 0 1";
    rig.controller.Command(kPasteFen);
    EXPECT_EQ(rig.controller.WindowTitle(), "White vs Black - Reckless Amphitheater") << "a pasted position is no file";

    std::string many = kAnnotated;
    many += "[Event \"Second\"]\n[White \"Gamma\"]\n[Black \"Delta\"]\n[Result \"*\"]\n\n1. d4 d5 *\n\n";
    rig.controller.OpenPgn(WriteFile("amphitheater-files/two.pgn", many));
    auto *picker = dynamic_cast<GamePickerDialog *>(rig.controller.dialog());
    ASSERT_NE(picker, nullptr);
    picker->Open(1);
    rig.controller.Frame(0.0);
    EXPECT_EQ(rig.controller.WindowTitle(), "Gamma vs Delta - two.pgn - Reckless Amphitheater") << "a game picked";
}

TEST(Files, FenAndPgnGoToTheClipboardAndComeBackFromIt)
{
    Rig rig;
    rig.controller.ShowPgnGame(chess::ReadPgn(kAnnotated).front());
    rig.controller.ShowPly(2);
    rig.controller.Command(kCopyFen);
    EXPECT_EQ(rig.clipboard, rig.controller.game().PositionAt(2).ToFen());
    rig.controller.Command(kCopyPgn);
    EXPECT_NE(rig.clipboard.find("Qxf7# 1-0"), std::string::npos) << rig.clipboard;

    rig.clipboard = "  r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1 \n";
    rig.controller.Command(kPasteFen);
    EXPECT_EQ(rig.controller.game().PlyCount(), 0);
    EXPECT_EQ(rig.controller.game().StartPosition().ToFen(), "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");

    rig.clipboard = "1. e4 e5 2. Nf3 *";
    rig.controller.Command(kPasteFen);
    EXPECT_EQ(rig.controller.game().PlyCount(), 3) << "movetext with no tags is a game";

    rig.clipboard = "this is not chess";
    rig.controller.Command(kPasteFen);
    EXPECT_NE(rig.controller.last_error().find("no FEN"), std::string::npos);
    EXPECT_EQ(rig.controller.game().PlyCount(), 3) << "nothing changed";
}

TEST(Files, AGamePlayedOnTheBoardIsSavedWithItsClocksAndScores)
{
    Rig          rig;
    PlaySettings settings;
    settings.white.is_human                 = false;
    settings.white.engine.config.executable = AMPHITHEATER_FAKE_ENGINE;
    settings.white.engine.config.name       = "Alpha";
    settings.black.is_human                 = false;
    settings.black.engine.config.executable = AMPHITHEATER_FAKE_ENGINE;
    settings.black.engine.config.arguments  = {"--crash-at", "3"};
    settings.black.engine.config.name       = "Beta";
    settings.time_control                   = *chess::TimeControl::Parse("10+0.1");
    rig.controller.StartGame(settings);
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (rig.controller.IsPlaying() && std::chrono::steady_clock::now() < deadline)
    {
        rig.controller.Frame(0.0);
        std::this_thread::sleep_for(5ms);
    }
    ASSERT_FALSE(rig.controller.IsPlaying());
    const std::string pgn = chess::WritePgn(rig.controller.CurrentPgn());
    EXPECT_NE(pgn.find("[White \"Alpha\"]"), std::string::npos);
    EXPECT_NE(pgn.find("[Result \"1-0\"]"), std::string::npos);
    EXPECT_NE(pgn.find("[TimeControl \"10+0.1\"]"), std::string::npos);
    EXPECT_NE(pgn.find("s}"), std::string::npos) << "a move's comment: its score, depth and time\n" << pgn;
}

TEST(GameFiles, TheCardReadsTheTagsAndAFenOrAPgnIsReadFromText)
{
    const auto game = chess::ReadPgn(kAnnotated).front();
    const auto info = InfoFromPgn(game);
    EXPECT_EQ(info.white, "Alpha");
    EXPECT_EQ(info.event, "Club championship");
    EXPECT_EQ(info.opening, "C20 King's pawn game");
    EXPECT_EQ(ReadPasted("").games.size(), 0u);
    EXPECT_TRUE(ReadPasted("8/8/8/8/8/8/8/K6k w - - 0 1").position.has_value());
    EXPECT_EQ(TodayInPgn().size(), 10u);
    const chess::PgnGame fresh = PgnOf(chess::Game{}, GameInfo{}, std::nullopt, "2026.10.19");
    EXPECT_EQ(fresh.Tag("Date"), "2026.10.19");
    EXPECT_EQ(fresh.Tag("White"), "White");
}

TEST(Files, ThePickersRowsButtonsAndKeysOpenAGameOrCloseIt)
{
    std::vector<chess::PgnGame> games = chess::ReadPgn(kAnnotated);
    games.push_back(chess::ReadPgn("[White \"Gamma\"]\n[Black \"Delta\"]\n[Result \"*\"]\n\n1. d4 *\n").front());
    GamePickerDialog picker(games, "two.pgn", 3);
    picker.SetBounds({0, 0, 1200, 900});
    std::vector<std::string> opened;
    int                      closed = 0;
    picker.on_open                  = [&](const chess::PgnGame &game) { opened.push_back(game.Tag("White")); };
    picker.on_close                 = [&] { ++closed; };

    GamesModel model(picker.games());
    ASSERT_EQ(model.RowCount(), 2u);
    const std::vector<std::string> first = {"1", "Alpha", "Beta", "1-0", "Club championship", "2026.10.01", "4"};
    for (std::size_t column = 0; column < first.size(); ++column)
    {
        EXPECT_EQ(model.CellText(0, column), first[column]) << "column " << column;
    }

    auto *open   = test::ButtonOf(picker, "Open");
    auto *cancel = test::ButtonOf(picker, "Cancel");
    ASSERT_NE(open, nullptr);
    ASSERT_NE(cancel, nullptr);
    test::Press(*open);
    EXPECT_EQ(opened, std::vector<std::string>{"Alpha"}) << "the first game is selected as the list opens";
    picker.list().Select(1);
    EXPECT_TRUE(picker.KeyPressed({reckless::view::Key::kReturn, 0, {}}));
    EXPECT_EQ(opened.back(), "Gamma");
    test::Press(*cancel);
    EXPECT_TRUE(picker.KeyPressed({reckless::view::Key::kEscape, 0, {}}));
    EXPECT_EQ(closed, 2);

    GamePickerDialog none({}, "empty.pgn", 0);
    none.SetBounds({0, 0, 1200, 900});
    none.on_open = [&](const chess::PgnGame &) { opened.push_back("none"); };
    test::Press(*test::ButtonOf(none, "Open"));
    EXPECT_TRUE(none.KeyPressed({reckless::view::Key::kReturn, 0, {}}));
    EXPECT_EQ(opened.size(), 2u) << "with no game, Open opens nothing";
}

} // namespace amphitheater::app
