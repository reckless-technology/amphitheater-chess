// File > Make book from games: the dialog over a library of games (tests/data/library), the book made on its own thread
// with its progress shown, saved, opened and its moves listed; and what the dialog refuses.

#include "app/Commands.h"
#include "app/Faces.h"
#include "app/MainController.h"
#include "app/MainView.h"
#include "app/MakeBookDialog.h"
#include "app/PieceSet.h"

#include "ScratchPath.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <thread>

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

/** @return The test library's folder of games. */
std::string Library()
{
    return (std::filesystem::path(AMPHITHEATER_TEST_DATA_DIR) / "library" / "folder").string();
}

} // namespace

TEST(MakeBookWindow, ABookIsMadeFromALibrarySavedAndOpened)
{
    MainView       view(Pieces());
    MainController controller(view, ControllerFiles::None());
    controller.Command(kMakeBook);
    auto *dialog = dynamic_cast<MakeBookDialog *>(controller.dialog());
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->moves().text(), "7") << "seven moves of each side by default";
    EXPECT_EQ(dialog->least().text(), "1");

    const auto folder = reckless::test::ScratchPath("amphitheater-make-book");
    std::filesystem::create_directories(folder);
    const auto book = folder / "made.bin";
    std::filesystem::remove(book);
    dialog->games().SetText(Library());
    dialog->moves().SetText("1");
    dialog->out().SetText(book.string());
    ASSERT_TRUE(dialog->Make()) << dialog->status();
    EXPECT_TRUE(dialog->IsMaking());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (dialog->IsMaking() && std::chrono::steady_clock::now() < deadline)
    {
        controller.Frame(0.016);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_FALSE(dialog->IsMaking());
    EXPECT_NE(dialog->status().find("7 moves in 4 positions from 5 games in 4 files, and opened it"), std::string::npos)
        << dialog->status();
    EXPECT_TRUE(std::filesystem::exists(book));
    EXPECT_EQ(controller.book_path(), book.string());
    ASSERT_EQ(controller.book_moves().size(), 3u);
    EXPECT_EQ(controller.book_moves()[0].san, "e4") << "played in three games of five";
    EXPECT_EQ(controller.dialog(), dialog) << "the dialog stays, saying what it made";
}

TEST(MakeBookWindow, TheDialogRefusesWhatItCannotMakeABookFrom)
{
    MainView       view(Pieces());
    MainController controller(view, ControllerFiles::None());
    controller.Command(kMakeBook);
    auto *dialog = dynamic_cast<MakeBookDialog *>(controller.dialog());
    ASSERT_NE(dialog, nullptr);
    EXPECT_FALSE(dialog->Make());
    EXPECT_NE(dialog->status().find("Choose the games"), std::string::npos);
    dialog->games().SetText(Library());
    dialog->moves().SetText("0");
    EXPECT_FALSE(dialog->Make());
    EXPECT_NE(dialog->status().find("'0'"), std::string::npos);
    dialog->moves().SetText("7");
    dialog->least().SetText("1.5");
    EXPECT_FALSE(dialog->Make());
    EXPECT_NE(dialog->status().find("'1.5'"), std::string::npos);
    dialog->least().SetText("1");
    EXPECT_FALSE(dialog->Make());
    EXPECT_NE(dialog->status().find("where to save"), std::string::npos);

    dialog->out().SetText((reckless::test::ScratchPath("amphitheater-make-book-none") / "none.bin").string());
    dialog->games().SetText(Library() + "-missing");
    ASSERT_TRUE(dialog->Make());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (dialog->IsMaking() && std::chrono::steady_clock::now() < deadline)
    {
        controller.Frame(0.016);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_NE(dialog->status().find("No book was made"), std::string::npos) << dialog->status();
    EXPECT_TRUE(controller.book_path().empty()) << "no book opened";
}

TEST(MakeBookWindow, TheFileMenuOffersIt)
{
    MainView       view(Pieces());
    MainController controller(view, ControllerFiles::None());
    bool           is_offered = false;
    for (const auto &entry : controller.menu_bar().menus[0].submenu)
    {
        is_offered = is_offered || (entry.id == kMakeBook && entry.enabled);
    }
    EXPECT_TRUE(is_offered);
}

} // namespace amphitheater::app
