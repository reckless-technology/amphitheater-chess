// Variations in the window: a move away from the line shown keeps the line and adds a variation, the line's own next
// move steps on along it, the moves list shows each variation under the move it leaves and a press enters it, a
// variation is promoted or deleted from the Game menu, a take-back takes the line's last move off the game, and a
// game's variations go through a save and come back from a file.

#include "app/Commands.h"
#include "app/Faces.h"
#include "app/MainController.h"
#include "app/MainView.h"
#include "app/PieceSet.h"
#include "chess/Notation.h"

#include <gtest/gtest.h>

#include <string>
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

/** A window with its controller, showing 1.e4 e5 2.Nf3. */
struct Rig
{
    MainView       view{Pieces()};
    MainController controller{view, ControllerFiles::None()};

    Rig()
    {
        view.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});
        chess::Game game;
        for (const char *san : {"e4", "e5", "Nf3"})
        {
            EXPECT_TRUE(game.PlaySan(san));
        }
        controller.ShowGame(game, {});
    }

    /**
     * A person's move on the board, in the position shown.
     * @param san The move.
     */
    void Play(const std::string &san)
    {
        const auto move = chess::ParseSan(controller.game().PositionAt(controller.view_ply()), san);
        ASSERT_TRUE(move) << san;
        controller.BoardMove(*move);
    }

    /** @return The line shown, in SAN parted by spaces. */
    std::string Line() const
    {
        std::string text;
        for (const std::string &san : controller.game().SanMoves())
        {
            text += (text.empty() ? "" : " ") + san;
        }
        return text;
    }

    /**
     * @param command A command.
     * @return Its item in the menu bar, or null.
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
};

} // namespace

TEST(Variations, AMoveAwayFromTheLineKeepsItAndAddsAVariation)
{
    Rig rig;
    rig.controller.ShowPly(1);
    rig.Play("c5");
    EXPECT_EQ(rig.Line(), "e4 c5");
    EXPECT_EQ(rig.controller.view_ply(), 2);
    const chess::GameTree &tree = rig.controller.tree();
    EXPECT_EQ(tree.GameOf(tree.MainLine()).SanMoves(), (std::vector<std::string>{"e4", "e5", "Nf3"}))
        << "the old line kept, as the main line";

    const MovesModel &moves = rig.view.moves().model();
    ASSERT_EQ(moves.RowCount(), 2u) << "the moves' row and the main line's under it";
    EXPECT_EQ(moves.CellText(1, 1), "main line: (1...e5 2.Nf3)");
    EXPECT_TRUE(moves.VariationAt(1));
    EXPECT_FALSE(moves.MoveAt(1, 1));
    rig.controller.Frame(0.0); // the menu bar follows what the window can do once a frame
    EXPECT_TRUE(rig.Item(kPromoteVariation)->enabled) << "a variation shown";

    rig.Play("Nf3");
    EXPECT_EQ(rig.Line(), "e4 c5 Nf3") << "the variation goes on";
    rig.controller.ShowPly(1);
    rig.Play("c5");
    EXPECT_EQ(rig.Line(), "e4 c5 Nf3") << "the line's own next move steps on along it";
    EXPECT_EQ(rig.controller.view_ply(), 2);
    rig.controller.ShowPly(0);
    rig.Play("e4");
    EXPECT_EQ(rig.Line(), "e4 c5 Nf3") << "the line shown kept, not the main line";
}

TEST(Variations, APressOnAVariationRowEntersItAtItsFirstMove)
{
    Rig rig;
    rig.controller.ShowPly(1);
    rig.Play("c5");
    rig.controller.ShowPly(1);
    rig.Play("e6");
    EXPECT_EQ(rig.Line(), "e4 e6");
    const MovesModel &moves = rig.view.moves().model();
    ASSERT_EQ(moves.RowCount(), 3u) << "the main line and 1...c5 under the moves' row";
    EXPECT_EQ(moves.CellText(1, 1), "main line: (1...e5 2.Nf3)");
    EXPECT_EQ(moves.CellText(2, 1), "(1...c5)");

    rig.view.on_variation(*moves.VariationAt(2));
    EXPECT_EQ(rig.Line(), "e4 c5");
    EXPECT_EQ(rig.controller.view_ply(), 2);
    rig.view.on_variation(*rig.view.moves().model().VariationAt(1));
    EXPECT_EQ(rig.Line(), "e4 e5 Nf3") << "back on the main line, at its move";
    EXPECT_EQ(rig.controller.view_ply(), 2);
    rig.controller.Frame(0.0);
    EXPECT_FALSE(rig.Item(kPromoteVariation)->enabled) << "the main line: nothing to promote";
}

TEST(Variations, AVariationIsPromotedOrDeletedFromTheGameMenu)
{
    Rig rig;
    rig.controller.ShowPly(1);
    rig.Play("c5");
    rig.controller.Command(kPromoteVariation);
    const chess::GameTree &tree = rig.controller.tree();
    EXPECT_EQ(tree.GameOf(tree.MainLine()).SanMoves(), (std::vector<std::string>{"e4", "c5"}));
    EXPECT_EQ(rig.Line(), "e4 c5") << "the line shown stays";
    EXPECT_EQ(rig.view.moves().model().CellText(1, 1), "(1...e5 2.Nf3)") << "the old main line a variation";

    rig.view.on_variation(*rig.view.moves().model().VariationAt(1));
    EXPECT_EQ(rig.Line(), "e4 e5 Nf3");
    rig.controller.ShowPly(3);
    rig.controller.Command(kDeleteVariation);
    EXPECT_EQ(rig.Line(), "e4 c5") << "the line it left shown";
    EXPECT_EQ(rig.controller.view_ply(), 1) << "at the position it left the line";
    EXPECT_EQ(rig.view.moves().model().RowCount(), 1u) << "no variation left";
}

TEST(Variations, ATakeBackTakesTheLinesLastMoveOffTheGame)
{
    Rig rig;
    rig.controller.ShowPly(1);
    rig.Play("c5");
    rig.controller.Command(kTakeBack);
    EXPECT_EQ(rig.Line(), "e4");
    EXPECT_EQ(rig.controller.tree().node(rig.controller.line().back()).children.size(), 1u) << "1...c5 gone";
    rig.controller.ShowPly(1);
    rig.Play("e5");
    EXPECT_EQ(rig.Line(), "e4 e5 Nf3") << "the main line still there";
}

TEST(Variations, AGamesVariationsGoThroughASaveAndComeBackFromAFile)
{
    Rig rig;
    rig.controller.ShowPly(1);
    rig.Play("c5");
    rig.Play("Nf3");
    const std::string text = chess::WritePgn(rig.controller.CurrentPgn());
    EXPECT_NE(text.find("1. e4 e5 (1... c5 2. Nf3) 2. Nf3"), std::string::npos) << text;

    Rig other;
    other.controller.ShowPgnGame(chess::ReadPgn(text).front());
    EXPECT_EQ(other.Line(), "e4 e5 Nf3");
    ASSERT_EQ(other.view.moves().model().RowCount(), 3u);
    EXPECT_EQ(other.view.moves().model().CellText(1, 1), "(1...c5 2.Nf3)");
    EXPECT_EQ(chess::WritePgn(other.controller.CurrentPgn()), text) << "read and written back as it was";
}

TEST(MovesModel, VariationRowsGoUnderTheirMovesRowAndTheCellsSkipThem)
{
    MovesModel model;
    model.SetMoves({"e4", "e5", "Nf3", "Nc6"}, 1, false, {{0, "(1.d4)", 7}, {3, "(2...d6)", 9}});
    ASSERT_EQ(model.RowCount(), 4u);
    EXPECT_EQ(model.VariationAt(1), std::optional<int>(7)) << "under the first moves' row";
    EXPECT_EQ(model.VariationAt(3), std::optional<int>(9));
    EXPECT_EQ(model.CellOf(2), (std::pair<std::size_t, std::size_t>{2, 1})) << "2.Nf3 a row down for the variation";
    EXPECT_EQ(model.MoveAt(2, 2), std::optional<int>(3));
    EXPECT_FALSE(model.MoveAt(1, 1));
    EXPECT_EQ(model.CellText(2, 0), "2.");
    EXPECT_EQ(model.CellText(1, 0), "");
}

} // namespace amphitheater::app
