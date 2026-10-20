// File > Open book: a Polyglot book's moves for the position shown listed under the game's card, the heaviest first
// with each one's share, drawn faintly on the board; a move pressed in the list played as on the board; the book kept
// in the layout file for the next launch; and File > Close book taking it all away.

#include "app/BookPanel.h"
#include "app/Commands.h"
#include "app/Faces.h"
#include "app/LayoutSettings.h"
#include "app/MainController.h"
#include "app/MainView.h"
#include "app/PieceSet.h"
#include "app/Theme.h"

#include "chess/Notation.h"
#include "chess/PolyglotBook.h"

#include "ScratchPath.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

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
 * A position's entry.
 * @param position The position.
 * @param uci Its move.
 * @param weight Its weight.
 * @return The entry.
 */
chess::PolyglotEntry Entry(const chess::Position &position, const char *uci, std::uint16_t weight)
{
    const auto move = chess::ParseUciMove(position, uci);
    EXPECT_TRUE(move) << uci;
    return {chess::PolyglotKey(position), chess::PolyglotBook::EncodeMove(*move), weight, 0};
}

/**
 * A small book written to a scratch file: 1.e4 (60), 1.d4 (30), 1.Nf3 (8) and 1.c4 (2) from the start, and 1...d5
 * after 1.d4.
 * @param name The scratch folder's name.
 * @return The file.
 */
std::filesystem::path WriteBook(const std::string &name)
{
    const chess::Position start    = chess::Position::Start();
    const chess::Position after_d4 = start.MakeMove(*chess::ParseUciMove(start, "d2d4"));
    const auto            bytes =
        chess::PolyglotBook::Encode({Entry(start, "e2e4", 60), Entry(start, "d2d4", 30), Entry(start, "g1f3", 8),
                                     Entry(start, "c2c4", 2), Entry(after_d4, "d7d5", 1)});
    const auto folder = reckless::test::ScratchPath(name);
    std::filesystem::create_directories(folder);
    const auto file = folder / "small.bin";
    std::ofstream(file, std::ios::binary | std::ios::trunc)
        .write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return file;
}

/** @return The SANs of the book's moves the controller shows. */
std::vector<std::string> Sans(const MainController &controller)
{
    std::vector<std::string> sans;
    for (const BookMove &move : controller.book_moves())
    {
        sans.push_back(move.san);
    }
    return sans;
}

} // namespace

TEST(BookWindow, ABooksMovesAreListedUnderTheCardTheHeaviestFirstWithTheirShares)
{
    MainView       view(Pieces());
    MainController controller(view, ControllerFiles{{}});
    const int      card_alone = view.card().height();
    EXPECT_FALSE(view.is_book_shown());
    EXPECT_FALSE(view.book().view().visible());

    ASSERT_TRUE(controller.OpenBook(WriteBook("amphitheater-book-list").string()));
    EXPECT_TRUE(view.is_book_shown());
    EXPECT_TRUE(view.book().view().visible());
    EXPECT_EQ(view.book_label().detail(), "small.bin");
    EXPECT_EQ(Sans(controller), (std::vector<std::string>{"e4", "d4", "Nf3", "c4"}));
    const BookModel &model = view.book().model();
    ASSERT_EQ(model.RowCount(), 4u);
    EXPECT_EQ(model.CellText(0, 0), "e4");
    EXPECT_EQ(model.CellText(0, 1), "60.0 %");
    EXPECT_EQ(model.CellText(0, 2), "60");
    EXPECT_EQ(model.CellText(3, 1), "2.0 %");

    // The card gives up its spare height to the book, which sits under it in the same column.
    EXPECT_LT(view.card().height(), card_alone);
    EXPECT_GT(view.book().view().bounds().y(), view.card().bounds().bottom());
    EXPECT_EQ(view.book().view().bounds().x(), view.card().bounds().x());
    const auto &style = view.book().view().style();
    EXPECT_EQ((view.book().view().height() - style.header_height) % style.row_height, 0) << "whole rows";
    EXPECT_GE(view.book().view().height(), style.header_height + 4 * style.row_height) << "the four moves";
}

TEST(BookWindow, TheHeaviestMovesAreDrawnFaintlyOnTheBoardTheHeaviestClearest)
{
    MainView       view(Pieces());
    MainController controller(view, ControllerFiles{{}});
    ASSERT_TRUE(controller.OpenBook(WriteBook("amphitheater-book-arrows").string()));
    controller.Frame(0.0);
    const std::vector<BoardArrow> &arrows = view.board().arrows();
    ASSERT_EQ(arrows.size(), 3u) << "c4, at 2 %, is left off";
    EXPECT_EQ(arrows[0].from, chess::Square("e2"));
    EXPECT_EQ(arrows[0].to, chess::Square("e4"));
    EXPECT_GT(arrows[0].colour.AlphaFraction(), arrows[1].colour.AlphaFraction());
    EXPECT_GT(arrows[1].colour.AlphaFraction(), arrows[2].colour.AlphaFraction());
    EXPECT_LT(arrows[0].colour.AlphaFraction(), 0.7f) << "faint, under an engine's arrow";
}

TEST(BookWindow, AMovePressedInTheListIsPlayedAndTheListFollowsThePosition)
{
    MainView       view(Pieces());
    MainController controller(view, ControllerFiles{{}});
    ASSERT_TRUE(controller.OpenBook(WriteBook("amphitheater-book-play").string()));
    view.book().view().on_click(1, 0);
    controller.Frame(0.0);
    ASSERT_EQ(controller.game().PlyCount(), 1);
    EXPECT_EQ(chess::MoveToSan(controller.game().PositionAt(0), controller.game().Moves()[0]), "d4");
    EXPECT_EQ(Sans(controller), (std::vector<std::string>{"d5"}));
    EXPECT_EQ(view.book().model().moves()[0].share, 1.0);

    controller.Command(kBack);
    controller.Frame(0.0);
    EXPECT_EQ(Sans(controller).size(), 4u) << "back at the start";
}

TEST(BookWindow, CloseBookTakesItAwayAndTheMenuFollows)
{
    MainView       view(Pieces());
    MainController controller(view, ControllerFiles{{}});
    const auto     close_item = [&controller]() -> const reckless::app::MenuEntry     *{
        for (const auto &entry : controller.menu_bar().menus[0].submenu)
        {
            if (entry.id == kCloseBook)
            {
                return &entry;
            }
        }
        return nullptr;
    };
    ASSERT_NE(close_item(), nullptr);
    EXPECT_FALSE(close_item()->enabled);
    ASSERT_TRUE(controller.OpenBook(WriteBook("amphitheater-book-close").string()));
    EXPECT_TRUE(close_item()->enabled);
    controller.Command(kCloseBook);
    controller.Frame(0.0);
    EXPECT_FALSE(close_item()->enabled);
    EXPECT_FALSE(view.is_book_shown());
    EXPECT_TRUE(controller.book_moves().empty());
    EXPECT_TRUE(view.board().arrows().empty());
}

TEST(BookWindow, ABookThatCannotBeReadIsRefusedAndTheOneOpenStays)
{
    MainView       view(Pieces());
    MainController controller(view, ControllerFiles{{}});
    const auto     book = WriteBook("amphitheater-book-refused");
    ASSERT_TRUE(controller.OpenBook(book.string()));
    EXPECT_FALSE(controller.OpenBook((book.parent_path() / "missing.bin").string()));
    EXPECT_NE(controller.last_error().find("could not be read"), std::string::npos);
    EXPECT_TRUE(view.is_book_shown());
    EXPECT_EQ(view.book_label().detail(), "small.bin");
}

TEST(BookWindow, TheBookOpenIsKeptForTheNextLaunch)
{
    const auto book   = WriteBook("amphitheater-book-kept");
    const auto layout = book.parent_path() / "layout.json";
    std::filesystem::remove(layout);
    {
        MainView        view(Pieces());
        ControllerFiles files{{}};
        files.layout = layout;
        MainController controller(view, files);
        ASSERT_TRUE(controller.OpenBook(book.string()));
        controller.Command(kShowLog); // any change of the layout writes it, the book with it
    }
    EXPECT_EQ(LayoutSettings::Load(layout).book, book.string());
    {
        MainView        view(Pieces());
        ControllerFiles files{{}};
        files.layout = layout;
        MainController controller(view, files);
        EXPECT_TRUE(view.is_book_shown());
        EXPECT_EQ(Sans(controller).size(), 4u);
        controller.Command(kCloseBook);
    }
    EXPECT_EQ(LayoutSettings::Load(layout).book, "");
}

} // namespace amphitheater::app
