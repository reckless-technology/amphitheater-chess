// View > Pieces: the board drawn with the cburnett set at first, the Staunton set chosen from the menu and ticked
// there, and the set chosen kept in the layout file across launches; a set the binary does not carry read as the
// default.

#include "app/Commands.h"
#include "app/Faces.h"
#include "app/LayoutSettings.h"
#include "app/MainController.h"
#include "app/MainView.h"
#include "app/PieceSet.h"

#include "ScratchPath.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace amphitheater::app
{
namespace
{

/** The pieces the view is made with, read once for every test. */
const PieceSet &Pieces()
{
    static const PieceSet pieces = [] {
        InstallFaces();
        return PieceSet::FromEmbedded();
    }();
    return pieces;
}

/**
 * @param bar The menu bar.
 * @return View > Pieces' entries.
 */
const std::vector<reckless::app::MenuEntry> &PiecesMenu(const reckless::app::MenuBar &bar)
{
    for (const auto &menu : bar.menus)
    {
        for (const auto &entry : menu.submenu)
        {
            if (entry.title == "Pieces")
            {
                return entry.submenu;
            }
        }
    }
    static const std::vector<reckless::app::MenuEntry> none;
    return none;
}

} // namespace

TEST(Pieces, TheBoardIsDrawnWithCburnettAndViewPiecesChoosesStaunton)
{
    const auto folder = reckless::test::ScratchPath("amphitheater-pieces");
    std::filesystem::remove_all(folder);
    const auto file = folder / "layout.json";
    {
        MainView        view(Pieces());
        ControllerFiles files = ControllerFiles::None();
        files.layout          = file;
        MainController controller(view, files);
        EXPECT_EQ(controller.piece_set(), "cburnett");
        const auto &menu = PiecesMenu(controller.menu_bar());
        ASSERT_EQ(menu.size(), 2u);
        EXPECT_EQ(menu[0].title, "Cburnett");
        EXPECT_TRUE(menu[0].checked);
        EXPECT_FALSE(menu[1].checked);

        const PieceSet *before = &view.board().pieces();
        controller.Command(kPiecesBase + 1);
        controller.Frame(0.0);
        EXPECT_EQ(controller.piece_set(), "staunton");
        EXPECT_NE(&view.board().pieces(), before) << "the board drawn with the other set";
        EXPECT_TRUE(view.board().pieces().IsComplete());
        EXPECT_TRUE(PiecesMenu(controller.menu_bar())[1].checked);
        EXPECT_EQ(LayoutSettings::Load(file).pieces, "staunton") << "kept as it is chosen";
    }
    {
        MainView        view(Pieces());
        ControllerFiles files = ControllerFiles::None();
        files.layout          = file;
        MainController controller(view, files);
        EXPECT_EQ(controller.piece_set(), "staunton") << "the next launch draws with it";
    }
}

TEST(Pieces, ASetTheBinaryDoesNotCarryIsReadAsTheDefault)
{
    const auto folder = reckless::test::ScratchPath("amphitheater-pieces-unknown");
    std::filesystem::create_directories(folder);
    const auto file = folder / "layout.json";
    std::ofstream(file, std::ios::trunc) << R"({"format": 1, "pieces": "merida"})";
    EXPECT_EQ(LayoutSettings::Load(file).pieces, "cburnett");
    std::ofstream(file, std::ios::trunc) << R"({"format": 1, "pieces": "staunton"})";
    EXPECT_EQ(LayoutSettings::Load(file).pieces, "staunton") << "read without an engine log's settings";
}

} // namespace amphitheater::app
