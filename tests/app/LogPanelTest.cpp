// The engine log under its header: seven rows as it first opens, folded away to the header by a click on it or by
// View > Engine log, made taller or shorter in whole rows by a drag on it - never shorter than two rows nor so tall the
// board's squares go under their least - and its height and whether it shows kept across launches.

#include "app/Commands.h"
#include "app/Faces.h"
#include "app/LayoutSettings.h"
#include "app/MainController.h"
#include "app/MainView.h"
#include "app/PieceSet.h"
#include "app/Showcase.h"

#include "ScratchPath.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace amphitheater::app
{
namespace
{

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

/** @return A row of the log, in design pixels. */
int Row(MainView &view)
{
    return view.log().view().style().row_height;
}

/**
 * A press on the log's header and its release, the pointer moved by some travel down between them.
 * @param view The window.
 * @param travel How far the pointer moves down while pressed, in pixels; negative is up; 0 is a click.
 */
void PressHeader(MainView &view, float travel)
{
    LogHeader                  &header = view.log_header();
    const reckless::gfx::PointF down{static_cast<float>(header.width()) / 3.0f,
                                     static_cast<float>(header.height()) / 2.0f};
    MouseEvent                  e;
    e.position = down;
    e.down     = down;
    header.MouseDown(e);
    if (travel != 0.0f)
    {
        e.position = {down.x, down.y + travel / 2.0f};
        header.MouseDrag(e);
        e.position = {down.x, down.y + travel};
        header.MouseDrag(e);
    }
    header.MouseUp(e);
}

/**
 * @param bar The menu bar.
 * @param command An item's command.
 * @return The item, wherever it is in the bar; null for none.
 */
const reckless::app::MenuEntry *Find(const reckless::app::MenuBar &bar, int command)
{
    for (const reckless::app::MenuEntry &menu : bar.menus)
    {
        for (const reckless::app::MenuEntry &item : menu.submenu)
        {
            if (item.id == command)
            {
                return &item;
            }
        }
    }
    return nullptr;
}

/**
 * @param name The test's own name.
 * @return A file of the test's own in an empty scratch folder.
 */
std::filesystem::path FreshFile(const std::string &name)
{
    const std::filesystem::path folder = reckless::test::ScratchPath(name);
    std::filesystem::remove_all(folder);
    return folder / "layout.json";
}

} // namespace

TEST(LogPanel, OpensSevenRowsTallUnderItsHeaderBelowTheAnalysis)
{
    MainView view(Pieces());
    ShowSample(view);
    view.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});
    const reckless::gfx::RectI log    = view.log().view().bounds();
    const reckless::gfx::RectI header = view.log_header().bounds();
    EXPECT_TRUE(view.is_log_open());
    EXPECT_TRUE(view.log().view().visible());
    EXPECT_EQ(log.height(), 7 * Row(view)) << "seven rows as it first opens";
    EXPECT_EQ(header.bottom(), log.y()) << "the header right over the log";
    EXPECT_EQ(header.y() - view.analysis().bounds().bottom(), MainView::kLogHeaderGap);
    EXPECT_EQ(log.bottom(), view.status().bounds().y() - MainView::kMargin);
    EXPECT_EQ(log.width(), MainView::kDesignWidth - 2 * MainView::kMargin) << "the full width";
    EXPECT_EQ(header.width(), log.width());
    EXPECT_EQ(view.log_header().cursor, reckless::view::Cursor::kUpDown) << "a drag resizes it";
}

TEST(LogPanel, AClickOnTheHeaderFoldsTheLogAwayAndGivesTheBoardTheRoom)
{
    MainView       view(Pieces());
    MainController controller(view, ControllerFiles::None());
    ShowSample(view);
    view.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});
    const float open_square = view.board().SquareSize();
    const int   open_height = view.log().view().height();
    EXPECT_TRUE(Find(controller.menu_bar(), kShowLog)->checked);

    PressHeader(view, 0.0f);
    EXPECT_FALSE(view.is_log_open());
    EXPECT_FALSE(view.log().view().visible());
    EXPECT_FALSE(view.log_header().open());
    EXPECT_EQ(view.log_header().bounds().bottom(), view.status().bounds().y() - MainView::kMargin)
        << "the header alone at the foot";
    EXPECT_GT(view.board().SquareSize(), open_square) << "the board takes the log's room";
    EXPECT_FALSE(Find(controller.menu_bar(), kShowLog)->checked) << "View > Engine log unticked";
    EXPECT_EQ(view.log_header().cursor, reckless::view::Cursor::kPointingHand) << "folded, it only opens";

    PressHeader(view, 0.0f);
    EXPECT_TRUE(view.is_log_open());
    EXPECT_TRUE(view.log().view().visible());
    EXPECT_EQ(view.log().view().height(), open_height) << "back at the height it had";
    EXPECT_EQ(view.board().SquareSize(), open_square);
}

TEST(LogPanel, AShortWobbleIsAClickAndADragOnAFoldedHeaderResizesNothing)
{
    MainView view(Pieces());
    view.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});
    const int height = view.log_height();
    PressHeader(view, LogHeader::kDragThreshold - 1.0f);
    EXPECT_FALSE(view.is_log_open()) << "a press that hardly moved is a click";
    EXPECT_EQ(view.log_height(), height);

    view.log_header().MouseDown(MouseEvent{{10.0f, 10.0f}, {10.0f, 10.0f}});
    view.log_header().MouseDrag(MouseEvent{{10.0f, -80.0f}, {10.0f, 10.0f}});
    EXPECT_FALSE(view.log_header().dragging()) << "folded, the header does not drag";
    EXPECT_EQ(view.log_height(), height);
}

TEST(LogPanel, ADragOnTheHeaderMakesTheLogTallerOrShorterInWholeRows)
{
    MainView view(Pieces());
    ShowSample(view);
    view.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});
    int told           = 0;
    view.on_log_layout = [&told] { ++told; };
    const int   row    = Row(view);
    const int   before = view.log().view().height();
    const float square = view.board().SquareSize();

    PressHeader(view, -3.0f * static_cast<float>(row)); // up three rows
    EXPECT_EQ(view.log().view().height(), before + 3 * row);
    EXPECT_EQ(view.log().view().height() % row, 0) << "whole rows";
    EXPECT_EQ(view.log_header().bounds().bottom(), view.log().view().bounds().y()) << "the header rides on the log";
    EXPECT_LT(view.board().SquareSize(), square) << "the board gives the room";
    EXPECT_TRUE(view.is_log_open()) << "a drag is not a click";
    EXPECT_EQ(told, 1) << "told once, as the drag is let go";

    PressHeader(view, 2.0f * static_cast<float>(row)); // down two
    EXPECT_EQ(view.log().view().height(), before + row);
    EXPECT_EQ(told, 2);
}

TEST(LogPanel, ADragIsHeldBetweenTwoRowsAndTheBoardsLeastSquares)
{
    MainView view(Pieces());
    ShowSample(view);
    for (const int window : {MainView::kDesignHeight, MainView::kLayoutHeight, MainView::kDesignHeight + 400})
    {
        view.SetBounds({MainView::kDesignWidth, window});
        PressHeader(view, -5000.0f);
        EXPECT_GE(view.board().SquareSize(), static_cast<float>(MainView::kLeastSquare)) << window;
        EXPECT_LE(view.log().view().height(), view.LogMostHeight()) << window;
        EXPECT_GT(view.log().view().height(), 7 * Row(view)) << "much taller than it opens, at " << window;

        PressHeader(view, 5000.0f);
        EXPECT_EQ(view.log().view().height(), 2 * Row(view)) << "two rows at least, at " << window;
    }
}

TEST(LogPanel, ViewEngineLogFoldsItAndTheLayoutIsKeptAcrossLaunches)
{
    const auto file = FreshFile("amphitheater-log-layout");
    {
        MainView        view(Pieces());
        ControllerFiles files = ControllerFiles::None();
        files.layout          = file;
        MainController controller(view, files);
        view.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});
        const reckless::app::MenuEntry *item = Find(controller.menu_bar(), kShowLog);
        ASSERT_NE(item, nullptr);
        EXPECT_TRUE(item->shortcut.primary && item->shortcut.character == U'l') << "Ctrl+L, Command-L on a Mac";
        EXPECT_TRUE(item->checked);

        PressHeader(view, -2.0f * static_cast<float>(Row(view)));
        EXPECT_EQ(LayoutSettings::Load(file).log_height, view.log_height()) << "kept as the drag is let go";
        controller.Command(kShowLog);
        EXPECT_FALSE(view.is_log_open());
        EXPECT_FALSE(LayoutSettings::Load(file).is_log_open) << "kept as it is folded";
        EXPECT_FALSE(Find(controller.menu_bar(), kShowLog)->checked);
    }
    {
        MainView        view(Pieces());
        ControllerFiles files = ControllerFiles::None();
        files.layout          = file;
        MainController controller(view, files);
        EXPECT_FALSE(view.is_log_open()) << "the next launch opens as it was left";
        EXPECT_EQ(view.log_height(), LayoutSettings::Load(file).log_height);
        controller.Command(kShowLog);
        EXPECT_TRUE(view.is_log_open());
        EXPECT_TRUE(LayoutSettings::Load(file).is_log_open);
    }
}

TEST(LogPanel, AControllerWithNoLayoutFileKeepsNothing)
{
    MainView       view(Pieces());
    MainController controller(view, ControllerFiles::None());
    controller.Command(kShowLog);
    EXPECT_FALSE(view.is_log_open());
    EXPECT_TRUE(controller.last_error().empty());
}

TEST(LayoutSettings, AreKeptAndReadBack)
{
    const auto file = FreshFile("amphitheater-layout-settings");
    EXPECT_EQ(LayoutSettings::Load(file), LayoutSettings{}) << "no file: the defaults";
    LayoutSettings settings;
    settings.log_height  = 300;
    settings.is_log_open = false;
    ASSERT_TRUE(settings.Save(file));
    EXPECT_EQ(LayoutSettings::Load(file), settings);
    EXPECT_EQ(LayoutSettings::Load({}), LayoutSettings{});
}

TEST(LayoutSettings, ReadAFileTheyCannotUseAsTheDefaults)
{
    const auto file = FreshFile("amphitheater-layout-settings-bad");
    std::filesystem::create_directories(file.parent_path());
    for (const char *text :
         {"not json", "[]", "{\"format\": 2, \"engine_log\": {\"height\": 300, \"open\": false}}",
          "{\"format\": 1, \"engine_log\": []}", "{\"format\": 1, \"engine_log\": {\"height\": 3, \"open\": \"no\"}}",
          "{\"format\": 1, \"engine_log\": {\"height\": 9999999}}"})
    {
        std::ofstream(file, std::ios::trunc) << text;
        EXPECT_EQ(LayoutSettings::Load(file), LayoutSettings{}) << text;
    }
    std::ofstream(file, std::ios::trunc) << "{\"format\": 1, \"engine_log\": {\"open\": false}}";
    EXPECT_FALSE(LayoutSettings::Load(file).is_log_open) << "what it holds is taken, the rest defaulted";
    EXPECT_EQ(LayoutSettings::Load(file).log_height, MainView::kLogDefaultHeight);
}

} // namespace amphitheater::app
