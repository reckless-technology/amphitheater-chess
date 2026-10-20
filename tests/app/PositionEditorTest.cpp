// Edit > Set up position: opened on the position shown, its board edited by click, right-click and drag with the piece
// chosen from the palette, its fields held to what the pieces allow, a FEN typed in read, the reason a position is not
// legal shown and Set up held back until it is, and the position set up shown on the window's board.

#include "app/PositionEditor.h"
#include "app/Commands.h"
#include "app/Faces.h"
#include "app/MainController.h"
#include "app/MainView.h"
#include "app/PieceSet.h"

#include <gtest/gtest.h>

#include <string>

namespace amphitheater::app
{
namespace
{

using chess::SetupPiece;
using chess::Square;
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

/** A window with its controller, laid out at the design size. */
struct Rig
{
    MainView       view{Pieces()};
    MainController controller{view, ControllerFiles{{}}};

    Rig()
    {
        view.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});
    }

    /** @return The open editor, or null. */
    PositionEditorDialog *Editor()
    {
        return dynamic_cast<PositionEditorDialog *>(controller.dialog());
    }
};

/**
 * @param board The setup board.
 * @param square A square.
 * @return Its middle, in the board's coordinates.
 */
reckless::gfx::PointF Middle(SetupBoardView &board, Square square)
{
    return board.SquareBounds(square).Centre();
}

/**
 * A click on a square.
 * @param board The setup board.
 * @param square The square.
 * @param is_right Whether it is the secondary button's.
 */
void Click(SetupBoardView &board, Square square, bool is_right = false)
{
    MouseEvent e;
    e.position = Middle(board, square);
    e.down     = e.position;
    e.right    = is_right;
    board.MouseDown(e);
    board.MouseUp(e);
}

/**
 * A drag from a square to a point.
 * @param board The setup board.
 * @param from The square the press begins on.
 * @param to Where it is let go, in the board's coordinates.
 */
void Drag(SetupBoardView &board, Square from, reckless::gfx::PointF to)
{
    MouseEvent e;
    e.position = Middle(board, from);
    e.down     = e.position;
    board.MouseDown(e);
    e.position = to;
    board.MouseDrag(e);
    board.MouseUp(e);
}

} // namespace

TEST(PositionEditor, OpensFromTheEditMenuOnThePositionShown)
{
    Rig         rig;
    chess::Game game;
    for (const char *san : {"e4", "c5", "Nf3"})
    {
        ASSERT_TRUE(game.PlaySan(san));
    }
    rig.controller.ShowGame(game, {});
    rig.controller.ShowPly(2);
    rig.controller.Command(kFlip);
    const reckless::app::MenuEntry *item = nullptr;
    for (const auto &entry : rig.controller.menu_bar().menus[1].submenu)
    {
        item = entry.id == kSetUpPosition ? &entry : item;
    }
    ASSERT_NE(item, nullptr) << "under Edit";
    EXPECT_TRUE(item->shortcut.primary && item->shortcut.character == U'p');

    rig.controller.Command(kSetUpPosition);
    PositionEditorDialog *editor = rig.Editor();
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->setup().ToFen(), game.PositionAt(2).ToFen()) << "the position shown, not the game's last";
    EXPECT_TRUE(editor->board().flipped()) << "turned as the window's board is";
    EXPECT_TRUE(editor->can_set_up());
    EXPECT_EQ(editor->message(), "A legal position, White to move.");
    EXPECT_EQ(editor->en_passant().choices(), (std::vector<std::string>{"None", "c6"}));
    EXPECT_EQ(editor->en_passant().Text(), "c6");
}

TEST(PositionEditor, TheBoardTakesClicksRightClicksAndDrags)
{
    Rig rig;
    rig.controller.Command(kSetUpPosition);
    PositionEditorDialog &editor = *rig.Editor();
    SetupBoardView       &board  = editor.board();

    // The palette chooses the piece a click puts; a click where it stands takes it off.
    PiecePalette &palette = editor.palette();
    palette.MouseDown(MouseEvent{palette.CellBounds({chess::kBlack, chess::kQueen}).Centre(), {}});
    EXPECT_EQ(palette.chosen(), (SetupPiece{chess::kBlack, chess::kQueen}));
    Click(board, Square{"d4"});
    EXPECT_EQ(editor.setup().At(Square{"d4"}), (SetupPiece{chess::kBlack, chess::kQueen}));
    Click(board, Square{"d4"});
    EXPECT_EQ(editor.setup().At(Square{"d4"}), SetupPiece{}) << "clicked again: taken off";
    Click(board, Square{"e2"});
    EXPECT_EQ(editor.setup().At(Square{"e2"}), (SetupPiece{chess::kBlack, chess::kQueen})) << "the pawn replaced";

    Click(board, Square{"e2"}, true);
    EXPECT_EQ(editor.setup().At(Square{"e2"}), SetupPiece{}) << "a right-click empties the square";

    Drag(board, Square{"g1"}, Middle(board, Square{"f3"}));
    EXPECT_EQ(editor.setup().At(Square{"g1"}), SetupPiece{});
    EXPECT_EQ(editor.setup().At(Square{"f3"}), (SetupPiece{chess::kWhite, chess::kKnight}));
    Drag(board, Square{"f3"}, {-40.0f, -40.0f});
    EXPECT_EQ(editor.setup().At(Square{"f3"}), SetupPiece{}) << "dragged off the board: taken off";
    EXPECT_EQ(editor.fen().text(), editor.setup().ToFen()) << "the FEN follows every edit";
}

TEST(PositionEditor, SetUpWaitsForALegalPositionAndSaysWhatIsWrong)
{
    Rig rig;
    rig.controller.Command(kSetUpPosition);
    PositionEditorDialog &editor = *rig.Editor();
    editor.Edit([](chess::BoardSetup &setup) { setup.Clear(); });
    EXPECT_FALSE(editor.can_set_up());
    EXPECT_EQ(editor.message(), "White must have exactly one king, not 0.");
    editor.SetUp();
    EXPECT_EQ(rig.Editor(), &editor) << "Set up does nothing while the position is not legal";

    editor.palette().Choose({chess::kWhite, chess::kKing});
    Click(editor.board(), Square{"g1"});
    editor.palette().Choose({chess::kBlack, chess::kKing});
    Click(editor.board(), Square{"g8"});
    editor.palette().Choose({chess::kWhite, chess::kRook});
    Click(editor.board(), Square{"a8"});
    EXPECT_EQ(editor.message(), "The side not to move is in check.");
    editor.side().Choose(1);
    EXPECT_EQ(editor.setup().side_to_move(), chess::kBlack);
    EXPECT_TRUE(editor.can_set_up());
    EXPECT_EQ(editor.message(), "A legal position, Black to move.");
    EXPECT_FALSE(editor.castling(chess::kWhite, true).IsEnabled()) << "no king on e1: no castling to offer";
}

TEST(PositionEditor, TheFieldsFollowWhatThePiecesAllow)
{
    Rig rig;
    rig.controller.Command(kSetUpPosition);
    PositionEditorDialog &editor = *rig.Editor();
    EXPECT_TRUE(editor.castling(chess::kWhite, true).checked());
    Drag(editor.board(), Square{"h1"}, Middle(editor.board(), Square{"h4"}));
    EXPECT_FALSE(editor.castling(chess::kWhite, true).checked()) << "the rook left its corner";
    EXPECT_FALSE(editor.castling(chess::kWhite, true).IsEnabled());
    EXPECT_TRUE(editor.castling(chess::kWhite, false).IsEnabled());
    editor.castling(chess::kWhite, false).Toggle();
    EXPECT_FALSE(editor.setup().castling(chess::kWhite, false)) << "a box cleared takes the right away";

    EXPECT_FALSE(editor.en_passant().IsEnabled()) << "no double step to offer";
    editor.move_number().on_commit("17");
    EXPECT_EQ(editor.setup().move_number(), 17);
    editor.move_number().on_commit("nonsense");
    EXPECT_EQ(editor.setup().move_number(), 17) << "what is not a number is ignored";
    EXPECT_EQ(editor.move_number().text(), "17");
}

TEST(PositionEditor, AFenTypedInIsReadAndOneThatCannotBeSaysWhy)
{
    Rig rig;
    rig.controller.Command(kSetUpPosition);
    PositionEditorDialog &editor = *rig.Editor();
    editor.fen().on_commit("8/8/8/4k3/8/8/8/R3K3 w Q -"); // an EPD's four fields: read tolerantly
    EXPECT_EQ(editor.setup().ToFen(), "8/8/8/4k3/8/8/8/R3K3 w Q - 0 1");
    EXPECT_TRUE(editor.castling(chess::kWhite, false).checked());
    EXPECT_TRUE(editor.can_set_up());

    EXPECT_FALSE(editor.ReadFen("not a fen"));
    EXPECT_EQ(editor.message().rfind("That FEN cannot be read: ", 0), 0u) << editor.message();
    EXPECT_EQ(editor.setup().ToFen(), "8/8/8/4k3/8/8/8/R3K3 w Q - 0 1") << "the setup kept";
}

TEST(PositionEditor, SetUpShowsThePositionOnTheWindowsBoardAndCancelChangesNothing)
{
    Rig rig;
    rig.controller.Command(kSetUpPosition);
    rig.Editor()->ReadFen("4k3/8/8/8/8/8/4P3/4K3 b - - 3 40");
    rig.Editor()->SetUp();
    rig.controller.Frame(0.0);
    EXPECT_EQ(rig.controller.dialog(), nullptr);
    EXPECT_EQ(rig.controller.game().StartPosition().ToFen(), "4k3/8/8/8/8/8/4P3/4K3 b - - 3 40");
    EXPECT_EQ(rig.controller.game().PlyCount(), 0);
    EXPECT_EQ(rig.view.board().position().ToFen(), "4k3/8/8/8/8/8/4P3/4K3 b - - 3 40");

    rig.controller.Command(kSetUpPosition);
    rig.Editor()->Edit([](chess::BoardSetup &setup) { setup.Clear(); });
    rig.Editor()->on_cancel();
    rig.controller.Frame(0.0);
    EXPECT_EQ(rig.controller.dialog(), nullptr);
    EXPECT_EQ(rig.controller.game().StartPosition().ToFen(), "4k3/8/8/8/8/8/4P3/4K3 b - - 3 40") << "unchanged";
}

} // namespace amphitheater::app
