// The window's views and its controller with no window: the pieces read from the binary, the board's geometry and a
// person's moves on it, the moves list, the graph, the clocks' faces, the controller's navigation, and the whole window
// drawn through Skia - its review image written where RECKLESS_SCREENSHOT_DIR names.

#include "app/BoardView.h"
#include "app/Commands.h"
#include "app/EngineLog.h"
#include "app/EvalGraph.h"
#include "app/Faces.h"
#include "app/MainController.h"
#include "app/MainView.h"
#include "app/MovesList.h"
#include "app/Panels.h"
#include "app/PieceSet.h"
#include "app/Showcase.h"
#include "app/Theme.h"
#include "chess/Notation.h"

#include "AppUpdates.h"
#include "SkiaCanvas.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace uci_host::app
{
namespace
{

using reckless::gfx::PointF;
using reckless::view::MouseEvent;

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
 * A press, a drag or a release at a point.
 * @param at Where, in the view's coordinates.
 * @param down Where the press began.
 * @return The event.
 */
MouseEvent At(PointF at, std::optional<PointF> down = std::nullopt)
{
    MouseEvent event;
    event.position = at;
    event.down     = down.value_or(at);
    return event;
}

/** A board of 8 x 60 px squares, showing a position, a person to move. */
struct BoardRig
{
    BoardView                board{Pieces()};
    std::vector<chess::Move> moves;

    explicit BoardRig(const std::string &fen = "startpos")
    {
        board.SetBounds({0, 0, BoardView::kRankGutter + 2 * BoardView::kFrame + 480,
                         BoardView::kFileGutter + 2 * BoardView::kFrame + 480});
        board.SetPosition(fen == "startpos" ? chess::Position::Start() : *chess::Position::FromFen(fen), std::nullopt);
        board.SetInteractive(true);
        board.on_move = [this](chess::Move move) { moves.push_back(move); };
    }

    /** @return The centre of a square. */
    PointF Centre(const char *square) const
    {
        return board.SquareBounds(chess::Square(square)).Centre();
    }

    /** A click on a square: a press and its release. */
    void Click(const char *square)
    {
        board.MouseDown(At(Centre(square)));
        board.MouseUp(At(Centre(square)));
    }
};

} // namespace

TEST(Pieces, EveryPieceIsReadFromTheBinary)
{
    EXPECT_TRUE(Pieces().IsComplete());
    EXPECT_EQ(PieceSet::FileName(chess::kWhite, chess::kKing), "pieces/wK.svg");
    EXPECT_EQ(PieceSet::FileName(chess::kBlack, chess::kKnight), "pieces/bN.svg");
}

TEST(Board, SquaresMapBothWaysAndTurnWithTheBoard)
{
    BoardRig rig;
    EXPECT_FLOAT_EQ(rig.board.SquareSize(), 60.0f);
    for (int index = 0; index < 64; ++index)
    {
        const chess::Square square(index);
        EXPECT_EQ(rig.board.SquareAt(rig.board.SquareBounds(square).Centre()), std::optional<chess::Square>(square));
    }
    const auto a1 = rig.board.SquareBounds(chess::Square("a1"));
    EXPECT_FLOAT_EQ(a1.x(), static_cast<float>(BoardView::kRankGutter + BoardView::kFrame)) << "a1 at the bottom left";
    EXPECT_GT(a1.y(), rig.board.SquareBounds(chess::Square("a8")).y());
    rig.board.SetFlipped(true);
    EXPECT_LT(rig.board.SquareBounds(chess::Square("a1")).y(), rig.board.SquareBounds(chess::Square("a8")).y())
        << "flipped, a1 at the top right";
    EXPECT_GT(rig.board.SquareBounds(chess::Square("a1")).x(), rig.board.SquareBounds(chess::Square("h1")).x());
    EXPECT_EQ(rig.board.SquareAt({1.0f, 1.0f}), std::nullopt) << "the rank gutter is off the board";
}

TEST(Board, AClickSelectsAPieceMarksItsTargetsAndASecondClickMoves)
{
    BoardRig rig;
    rig.Click("e2");
    ASSERT_EQ(rig.board.selected(), std::optional<chess::Square>(chess::Square("e2")));
    EXPECT_EQ(rig.board.targets().size(), 2u) << "e3 and e4";
    rig.Click("e4");
    ASSERT_EQ(rig.moves.size(), 1u);
    EXPECT_EQ(chess::MoveToUci(rig.moves.front()), "e2e4");
    EXPECT_FALSE(rig.board.selected().has_value());
}

TEST(Board, ADragOntoATargetMovesAndOntoAnythingElseGoesBack)
{
    BoardRig     rig;
    const PointF g1 = rig.Centre("g1");
    rig.board.MouseDown(At(g1));
    rig.board.MouseDrag(At(rig.Centre("e4"), g1));
    rig.board.MouseUp(At(rig.Centre("e4"), g1));
    EXPECT_TRUE(rig.moves.empty()) << "a knight cannot reach e4";
    EXPECT_EQ(rig.board.selected(), std::optional<chess::Square>(chess::Square("g1"))) << "back, still selected";
    rig.board.MouseDown(At(g1));
    rig.board.MouseDrag(At(rig.Centre("f3"), g1));
    rig.board.MouseUp(At(rig.Centre("f3"), g1));
    ASSERT_EQ(rig.moves.size(), 1u);
    EXPECT_EQ(chess::MoveToUci(rig.moves.front()), "g1f3");
}

TEST(Board, OnlyTheSideToMovesPiecesAreTakenAndNotWhenItIsNotInteractive)
{
    BoardRig rig;
    rig.Click("e7");
    EXPECT_FALSE(rig.board.selected().has_value()) << "Black's pawn, White to move";
    rig.board.SetInteractive(false);
    rig.Click("e2");
    EXPECT_FALSE(rig.board.selected().has_value());
    EXPECT_TRUE(rig.moves.empty());
}

TEST(Board, APromotionOpensThePickerAndThePieceChosenIsTheMove)
{
    BoardRig rig("8/4P3/8/8/8/8/k7/4K3 w - - 0 1");
    rig.Click("e7");
    rig.Click("e8");
    ASSERT_EQ(rig.board.promotion_choices().size(), 4u);
    EXPECT_TRUE(rig.moves.empty());
    EXPECT_EQ(rig.board.promotion_choices().front().PromotedPiece(), chess::kQueen);
    const PointF knight = rig.board.PromotionChoiceBounds(1).Centre();
    rig.board.MouseDown(At(knight));
    ASSERT_EQ(rig.moves.size(), 1u);
    EXPECT_EQ(chess::MoveToUci(rig.moves.front()), "e7e8n");
    EXPECT_TRUE(rig.board.promotion_choices().empty());

    rig.Click("e7");
    rig.Click("e8");
    rig.board.MouseDown(At(rig.Centre("a1")));
    EXPECT_TRUE(rig.board.promotion_choices().empty()) << "a press elsewhere closes it";
    EXPECT_EQ(rig.moves.size(), 1u);
}

namespace
{

/**
 * A board drawn through Skia.
 * @param board The board.
 * @return Its picture as a PNG.
 */
std::vector<std::uint8_t> Drawn(BoardView &board)
{
    reckless::gfx::Image image = reckless::gfx::MakeImage(board.width(), board.height());
    {
        const auto canvas = reckless::gfx::MakeImageCanvas(image, 1.0f);
        if (canvas)
        {
            board.Paint(*canvas);
        }
    }
    return reckless::gfx::EncodePng(image);
}

} // namespace

TEST(Board, EachStateItShowsChangesItsPicture)
{
    // White's king in check from the rook on e8, White's pawn one step from promoting.
    BoardRig   rig("4r2k/1P6/8/8/8/8/8/4K3 w - - 0 1");
    const auto plain = Drawn(rig.board);
    ASSERT_GT(plain.size(), 1000u);

    rig.Click("e1");
    ASSERT_TRUE(rig.board.selected().has_value());
    const auto selected = Drawn(rig.board);
    EXPECT_NE(selected, plain) << "the selection and its targets";

    const PointF e1 = rig.Centre("e1");
    rig.board.MouseDown(At(e1));
    rig.board.MouseDrag(At(rig.Centre("d3"), e1));
    const auto dragged = Drawn(rig.board);
    EXPECT_NE(dragged, selected) << "the king under the pointer, off its square";
    rig.board.MouseUp(At(rig.Centre("d3"), e1));

    rig.board.SetPosition(*chess::Position::FromFen("8/1P6/8/8/8/8/8/k3K3 w - - 0 1"), std::nullopt);
    rig.Click("b7");
    rig.Click("b8");
    ASSERT_EQ(rig.board.promotion_choices().size(), 4u);
    const auto picker = Drawn(rig.board);
    rig.board.MouseDown(At(rig.Centre("h1")));
    EXPECT_NE(picker, Drawn(rig.board)) << "the picker over the dimmed board";

    rig.board.SetFlipped(true);
    rig.Click("b7");
    rig.Click("b8");
    EXPECT_LT(rig.board.PromotionChoiceBounds(1).y(), rig.board.PromotionChoiceBounds(0).y())
        << "flipped, the eighth rank at the bottom: the picker runs up from it";
    EXPECT_NE(Drawn(rig.board), picker);
    rig.board.MouseDown(At(rig.Centre("h1")));

    rig.board.SetArrows({{chess::Square("e1"), chess::Square("e2"), theme::kAmber}});
    rig.board.SetBanner("1-0", "White mates");
    const auto banner = Drawn(rig.board);
    EXPECT_EQ(rig.board.banner(), "1-0");
    rig.board.MouseDown(At(rig.Centre("a8")));
    EXPECT_TRUE(rig.board.banner().empty()) << "a press hides the card";
    EXPECT_NE(Drawn(rig.board), banner);
}

TEST(Board, TheLastMoveLightsBothItsSquaresWhateverTheirShade)
{
    // b5 is light and b4 dark: each lit stands well clear of its plain shade, and the two lit shades differ.
    const auto distance = [](reckless::gfx::Color a, reckless::gfx::Color b) {
        return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
    };
    for (const char *name : {"b5", "b4", "a1", "h1"})
    {
        const chess::Square square(name);
        EXPECT_GE(distance(SquareFill(square, true), SquareFill(square, false)), 60) << name;
    }
    EXPECT_NE(SquareFill(chess::Square("b5"), true), SquareFill(chess::Square("b4"), true));
    EXPECT_EQ(SquareFill(chess::Square("a1"), false), theme::kDarkSquare);
    EXPECT_EQ(SquareFill(chess::Square("b1"), false), theme::kLightSquare);
}

TEST(MainView, TheMovesAndTheLogShowWholeRowsKeptOnTheMoveShownAndTheNewestLine)
{
    MainView view(Pieces());
    ShowSample(view);
    for (const int height : {MainView::kDesignHeight, MainView::kLayoutHeight, MainView::kDesignHeight - 23})
    {
        view.SetBounds({MainView::kDesignWidth, height});
        const auto &list  = view.moves().view();
        const auto &style = list.style();
        EXPECT_EQ((list.height() - style.header_height) % style.row_height, 0) << height;
        EXPECT_GT(list.scroll(), 0) << "the sample's moves outrun the list";
        EXPECT_EQ(list.scroll() % style.row_height, 0) << "no row half under the header at " << height;
        const auto &log = view.log().view();
        EXPECT_EQ(log.height() % log.style().row_height, 0) << height;
        EXPECT_EQ(log.scroll() % log.style().row_height, 0) << "following the newest line at " << height;
    }
}

TEST(EngineLog, LongLinesWrapAtASpaceIndentedUnderTheirText)
{
    const std::string text = "< Pawnstar  info depth 24 pv e2e4 e7e5 g1f3";
    const auto        rows = WrapLine(text, 24, 12);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(text.substr(rows[0].first, rows[0].second), "< Pawnstar  info depth");
    EXPECT_EQ(text.substr(rows[1].first, rows[1].second), "24 pv e2e4");
    EXPECT_EQ(text.substr(rows[2].first, rows[2].second), "e7e5 g1f3");
    const auto unbroken = WrapLine("abcdefghijklmnopqrstuvwxyz", 10, 0);
    ASSERT_EQ(unbroken.size(), 3u) << "a word longer than a row breaks where the row runs out";
    EXPECT_EQ(unbroken[2], (std::pair<std::size_t, std::size_t>{20, 6}));
    EXPECT_EQ(WrapLine("short", 40, 4).size(), 1u);
    EXPECT_EQ(WrapLine(text, 0, 12).size(), 1u) << "no width, no wrapping";

    EngineLogModel model;
    model.Add("Pawnstar", LogDirection::kFromEngine, "info depth 24 pv e2e4 e7e5 g1f3");
    model.Add("Zenith", LogDirection::kToEngine, "isready");
    EXPECT_EQ(model.RowCount(), 2u);
    EXPECT_TRUE(model.SetWrapColumns(24));
    EXPECT_EQ(model.RowCount(), 4u);
    EXPECT_EQ(model.CellText(1, 0), "            24 pv e2e4");
    EXPECT_EQ(model.CellColour(1, 0), model.CellColour(0, 0)) << "a wrapped part in its line's colour";
    EXPECT_NE(model.CellColour(3, 0), model.CellColour(0, 0));
    EXPECT_FALSE(model.SetWrapColumns(24));
    EXPECT_EQ(model.LineCount(), 2u);
    EXPECT_EQ(model.Line(0), text);

    for (std::size_t line = 0; line < EngineLogModel::kMostLines; ++line)
    {
        model.Add("Pawnstar", LogDirection::kFromEngine, "info depth 24 pv e2e4 e7e5 g1f3");
    }
    EXPECT_EQ(model.LineCount(), EngineLogModel::kMostLines);
    EXPECT_EQ(model.RowCount(), 3 * EngineLogModel::kMostLines) << "the oldest lines' rows go with them";
}

TEST(EngineLog, TheLogWrapsToItsWidthByDefaultAndCanCut)
{
    MainView view(Pieces());
    ShowSample(view);
    view.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});
    EngineLog &log = view.log();
    EXPECT_TRUE(log.is_wrapping());
    EXPECT_GT(log.model().wrap_columns(), 40);
    EXPECT_GT(log.model().RowCount(), log.model().LineCount()) << "the sample's long lines wrap";
    log.SetWrapping(false);
    EXPECT_EQ(log.model().RowCount(), log.model().LineCount());
    log.SetWrapping(true);
    const int wide = log.model().wrap_columns();
    view.SetBounds({MainView::kDesignWidth - 300, MainView::kDesignHeight});
    EXPECT_LT(log.model().wrap_columns(), wide) << "a narrower window, shorter rows";
}

TEST(MovesModel, MovesFillRowsFromWhitesColumnOrBlacksAndTheCurrentOneIsLit)
{
    MovesModel model;
    model.SetMoves({"e4", "e5", "Nf3"}, 1, false);
    EXPECT_EQ(model.RowCount(), 2u);
    EXPECT_EQ(model.CellText(0, 0), "1.");
    EXPECT_EQ(model.CellText(1, 1), "Nf3");
    EXPECT_EQ(model.CellText(1, 2), "");
    model.SetCurrentPly(2);
    EXPECT_TRUE(model.CellBackground(0, 2).has_value());
    EXPECT_FALSE(model.CellBackground(0, 1).has_value());

    model.SetMoves({"e5", "Nf3"}, 12, true);
    EXPECT_EQ(model.CellText(0, 0), "12.");
    EXPECT_EQ(model.CellText(0, 1), "...");
    EXPECT_EQ(model.CellText(0, 2), "e5");
    EXPECT_EQ(model.MoveAt(1, 1), std::optional<int>(1));
    EXPECT_EQ(model.MoveAt(0, 1), std::nullopt);
}

TEST(Panels, ClocksShowTenthsUnderAnHourAndSecondsOverIt)
{
    EXPECT_EQ(ClockText(251600), "4:11.6");
    EXPECT_EQ(ClockText(19650), "0:19.6");
    EXPECT_EQ(ClockText(3723000), "1:02:03");
    EXPECT_EQ(ClockText(-5), "0:00.0");
}

TEST(EvalGraph, TheScaleGrowsWithTheLargestScoreAndAPressPicksAPly)
{
    EvalGraph graph;
    graph.SetBounds({0, 0, 300, 160});
    graph.SetScores({0.3, -0.2, std::nullopt, 0.5}, 2, "");
    EXPECT_DOUBLE_EQ(graph.Scale(), 1.0);
    graph.SetScores({0.3, -2.4, 40.0}, 1, "");
    EXPECT_DOUBLE_EQ(graph.Scale(), 10.0) << "held to ten pawns";
    std::optional<int> chosen;
    graph.on_select = [&](int ply) { chosen = ply; };
    graph.MouseDown(At({290.0f, 50.0f}));
    EXPECT_EQ(chosen, std::optional<int>(3));
    graph.MouseDown(At({0.0f, 50.0f}));
    EXPECT_EQ(chosen, std::optional<int>(1));
}

TEST(MainController, NavigationStepsThroughTheGameAndFlipTurnsTheBoard)
{
    MainView       view(Pieces());
    MainController controller(view, ControllerFiles{{}});
    chess::Game    game;
    for (const char *san : {"e4", "e5", "Nf3", "Nc6"})
    {
        ASSERT_TRUE(game.PlaySan(san));
    }
    controller.ShowGame(game, {});
    EXPECT_EQ(controller.view_ply(), 4);
    controller.Command(kBack);
    EXPECT_EQ(controller.view_ply(), 3);
    EXPECT_EQ(view.board().position().ToFen(), game.PositionAt(3).ToFen());
    controller.Command(kFirst);
    EXPECT_EQ(controller.view_ply(), 0);
    controller.Command(kBack);
    EXPECT_EQ(controller.view_ply(), 0) << "held at the start";
    controller.ShowPly(99);
    EXPECT_EQ(controller.view_ply(), 4);
    EXPECT_TRUE(controller.Menus().can_step_back);
    EXPECT_FALSE(controller.Menus().can_step_forward);
    view.on_select_ply(2);
    EXPECT_EQ(controller.view_ply(), 2) << "a move pressed in the list";
    controller.Command(kFlip);
    EXPECT_TRUE(view.board().flipped());
    EXPECT_EQ(view.top_strip().state().name, "White") << "White's strip moves to the top";
    EXPECT_TRUE(view.KeyPressed({reckless::view::Key::kRight, 0, {}}));
    EXPECT_EQ(controller.view_ply(), 3);
}

TEST(MainView, TheWindowDrawsThroughSkiaAndTheBoardGrowsWithIt)
{
    MainView view(Pieces());
    ShowSample(view);
    const float small = view.board().SquareSize();
    view.SetBounds({MainView::kDesignWidth + 200, MainView::kDesignHeight + 200});
    EXPECT_GT(view.board().SquareSize(), small);
    view.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});

    reckless::gfx::Image image = reckless::gfx::MakeImage(view.width(), view.height());
    {
        const auto canvas = reckless::gfx::MakeImageCanvas(image, 1.0f);
        ASSERT_TRUE(canvas);
        reckless::view::PaintTree(view, *canvas);
    }
    const std::vector<std::uint8_t> png = reckless::gfx::EncodePng(image);
    EXPECT_GT(png.size(), 10000u);
    if (const auto dir = reckless::app::EnvironmentVariable("RECKLESS_SCREENSHOT_DIR"); dir && !dir->empty())
    {
        std::ofstream file(std::filesystem::path(*dir) / "uci-host-main-window.png", std::ios::binary);
        file.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
    }
}

} // namespace uci_host::app

namespace uci_host::app
{

TEST(MainView, ShortcutsWithCommandTellTheirCommands)
{
    MainView         view(Pieces());
    std::vector<int> told;
    view.on_command  = [&](int command) { told.push_back(command); };
    const auto press = [&](char32_t letter, bool command, bool shift = false) {
        reckless::view::KeyEvent key;
        key.key               = reckless::view::Key::kCharacter;
        key.character         = letter;
        key.modifiers.command = command;
        key.modifiers.shift   = shift;
        return view.KeyPressed(key);
    };
    EXPECT_TRUE(press(U'o', true));
    EXPECT_TRUE(press(U'S', true));
    EXPECT_TRUE(press(U'c', true, true));
    EXPECT_TRUE(press(U'z', true));
    EXPECT_TRUE(press(U'f', false));
    EXPECT_FALSE(press(U'k', true));
    EXPECT_FALSE(press(U'o', false));
    EXPECT_EQ(told, (std::vector<int>{kOpenPgn, kSavePgn, kCopyPgn, kTakeBack, kFlip}));
}

} // namespace uci_host::app
