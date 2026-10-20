// The window's sizes from the View menu (WindowSizes.h) on the suites' own platform: each size ticked while it is the
// size drawn and none once the person drags the frame, the window sized to the design times the size and the content
// drawn at exactly that scale, kept on its screen, a size the screen does not take dim and refused, the shortcuts
// stepping through the sizes and stopping at the ends, and the size kept across launches.

#include "app/WindowSizes.h"
#include "app/Commands.h"
#include "app/Faces.h"
#include "app/MainController.h"
#include "app/MainView.h"
#include "app/Menus.h"
#include "app/PieceSet.h"
#include "app/Theme.h"

#include "AppRoot.h"
#include "FakeAppPlatform.h"
#include "JsonFile.h"
#include "ScratchPath.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace amphitheater::app
{
namespace
{

using reckless::app::MenuEntry;
using reckless::app::Shortcut;
using reckless::gfx::PointI;

/** The pieces, read once for every test. */
const PieceSet &Pieces()
{
    static const PieceSet pieces = [] {
        InstallFaces();
        return PieceSet::FromEmbedded();
    }();
    return pieces;
}

/** The layout's design size, which 100 % is. */
constexpr PointI kDesign{MainView::kDesignWidth, MainView::kDesignHeight};

/** The window as the app holds it on the suites' platform: the view in its root, the sizes over the root, the window
    opened round the root - its resizes followed by the root, as RunApp follows them - on a screen of 3000 x 2000. */
struct WindowRig
{
    explicit WindowRig(std::filesystem::path file = {}) : sizes(root, kDesign, std::move(file))
    {
        sizes.Restore();
        reckless::app::WindowOptions options;
        options.width    = root.width();
        options.height   = root.height();
        options.position = std::pair<int, int>{40, 30};
        options.resized  = [this](int width, int height) { root.SetBounds({width, height}); };
        window           = platform.OpenWindow(options);
        fake().work_area = reckless::gfx::RectI{0, 0, 3000, 2000};
        sizes.SetWindow(window.get());
    }

    /** @return The window as the suites' platform holds it. */
    reckless::test::FakeAppWindow &fake()
    {
        return *platform.windows().front();
    }

    /** @return The window's content, as the platform holds it. */
    PointI WindowSize()
    {
        return {fake().options().width, fake().options().height};
    }

    MainView               view{Pieces()};
    reckless::app::AppRoot root{
        view, theme::Shell().Tooltip(), [] { return 0.0; }, {MainView::kLayoutWidth, MainView::kLayoutHeight}};
    WindowSizes                               sizes;
    reckless::test::FakeAppPlatform           platform;
    std::unique_ptr<reckless::app::AppWindow> window;
};

/**
 * The View menu of a bar.
 * @param bar The bar.
 * @return Its View menu's entries.
 */
const std::vector<MenuEntry> &ViewMenu(const reckless::app::MenuBar &bar)
{
    return bar.menus[6].submenu;
}

/**
 * An entry of a menu by its command.
 * @param entries The menu's entries.
 * @param command The command.
 * @return The entry, or null.
 */
const MenuEntry *Find(const std::vector<MenuEntry> &entries, int command)
{
    for (const MenuEntry &entry : entries)
    {
        if (entry.id == command && entry.submenu.empty() && !entry.separator)
        {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

TEST(WindowSizes, TheViewMenuOffersFiveSizesUnderARuleTicksTheOneInForceAndDimsThoseTheScreenRefuses)
{
    MenuState                    state;
    std::vector<std::string>     titles;
    const reckless::app::MenuBar plain = BuildMenuBar(state); // held: the loop reads into it
    for (const MenuEntry &entry : ViewMenu(plain))
    {
        titles.push_back(entry.separator ? "-" : entry.title);
    }
    EXPECT_EQ(titles, (std::vector<std::string>{"Flip the board", "Engine log", "Clear the engine log", "-", "50%",
                                                "75%", "100%", "125%", "150%", "-", "Larger", "Smaller"}));
    for (const int command : {kWindowSize50, kWindowSize100, kWindowSize150, kWindowLarger, kWindowSmaller})
    {
        EXPECT_FALSE(Find(ViewMenu(BuildMenuBar(state)), command)->enabled) << "no window to size: " << command;
    }

    state.window_sizes.has_window      = true;
    state.window_sizes.in_force        = 125;
    state.window_sizes.fits            = {true, true, true, true, false};
    state.window_sizes.larger          = 0;
    state.window_sizes.smaller         = 100;
    const reckless::app::MenuBar  bar  = BuildMenuBar(state);
    const std::vector<MenuEntry> &view = ViewMenu(bar);
    EXPECT_TRUE(Find(view, kWindowSize125)->checked) << "the size drawn";
    EXPECT_FALSE(Find(view, kWindowSize100)->checked);
    EXPECT_TRUE(Find(view, kWindowSize50)->enabled);
    EXPECT_FALSE(Find(view, kWindowSize150)->enabled) << "a size the screen does not take is dim";
    EXPECT_FALSE(Find(view, kWindowLarger)->enabled) << "nothing larger fits";
    EXPECT_TRUE(Find(view, kWindowSmaller)->enabled);
    EXPECT_EQ(Find(view, kWindowSize100)->shortcut.character, U'0');
    EXPECT_TRUE(Find(view, kWindowSize100)->shortcut.primary);
    EXPECT_EQ(Find(view, kWindowLarger)->shortcut.character, U'=');
    EXPECT_EQ(Find(view, kWindowLarger)->other_shortcuts.size(), 2u) << "the plus key and the keypad's";
    EXPECT_EQ(Find(view, kWindowSmaller)->shortcut.character, U'-');
    EXPECT_EQ(reckless::app::ShortcutText(Find(view, kWindowLarger)->shortcut, false), "Ctrl+=") << "shown in the menu";

    state.window_sizes.in_force = 0;
    for (const int command : {kWindowSize50, kWindowSize75, kWindowSize100, kWindowSize125, kWindowSize150})
    {
        EXPECT_FALSE(Find(ViewMenu(BuildMenuBar(state)), command)->checked) << "dragged to another size: none ticked";
    }
}

TEST(WindowSizes, EachSizeMakesTheWindowTheDesignTimesItDrawnAtExactlyThatScale)
{
    WindowRig rig;
    EXPECT_EQ(rig.sizes.InForce(), 100) << "a first launch: the design size, as before";
    EXPECT_EQ(rig.WindowSize(), kDesign);
    EXPECT_EQ(rig.sizes.Menu().in_force, 100);

    for (const int percent : kWindowSizes)
    {
        ASSERT_TRUE(rig.sizes.Choose(percent)) << percent;
        const PointI expected{kDesign.x * percent / 100, kDesign.y * percent / 100};
        EXPECT_EQ(rig.WindowSize(), expected) << percent;
        EXPECT_EQ(rig.root.width(), expected.x) << "the root follows the window";
        EXPECT_EQ(rig.root.height(), expected.y);
        EXPECT_FLOAT_EQ(rig.root.zoom().zoom(), static_cast<float>(percent) / 100.0f) << "drawn at exactly the size";
        EXPECT_EQ(rig.view.width(), MainView::kDesignWidth) << "laid out at the design size, whatever the size";
        EXPECT_EQ(rig.view.height(), MainView::kDesignHeight);
        EXPECT_EQ(rig.sizes.InForce(), percent);
        const WindowSizeMenu menu = rig.sizes.Menu();
        EXPECT_EQ(menu.in_force, percent) << "ticked";
    }
    EXPECT_EQ(rig.WindowSize(), (PointI{2070, 1680}));
    EXPECT_FALSE(rig.sizes.Choose(80)) << "a size the menu does not offer";

    rig.fake().Resize(1000, 900); // the person drags the frame
    EXPECT_EQ(rig.sizes.InForce(), 0);
    const WindowSizeMenu dragged = rig.sizes.Menu();
    EXPECT_EQ(dragged.in_force, 0) << "none ticked";
    EXPECT_EQ(dragged.larger, 75) << "the next size past the window's 72 %";
    EXPECT_EQ(dragged.smaller, 50);
    EXPECT_FLOAT_EQ(rig.root.zoom().zoom(), 1000.0f / MainView::kLayoutWidth) << "the layout's own sizing again";
}

TEST(WindowSizes, ASizeTheScreenDoesNotTakeIsDimAndRefusedAndAChosenOneStaysOnTheScreen)
{
    WindowRig rig;
    rig.fake().work_area = reckless::gfx::RectI{0, 0, 1600, 1300};
    rig.sizes.Frame(WindowSizes::kRefreshSeconds); // the screen's room asked again: the window moved to a smaller one
    const WindowSizeMenu menu = rig.sizes.Menu();
    EXPECT_EQ(menu.fits, (std::array<bool, 5>{true, true, true, false, false})) << "125 % is 1725 wide";
    EXPECT_EQ(menu.larger, 0) << "from 100 %, nothing larger fits";
    EXPECT_EQ(menu.smaller, 75);
    EXPECT_FALSE(rig.sizes.Choose(125));
    EXPECT_EQ(rig.WindowSize(), kDesign) << "refused, not shrunk";
    EXPECT_EQ(rig.sizes.InForce(), 100);

    ASSERT_TRUE(rig.sizes.Choose(50));
    rig.fake().work_area = reckless::gfx::RectI{0, 0, 1600, 1300};
    ASSERT_TRUE(rig.sizes.Choose(100));
    EXPECT_EQ(rig.window->Position(), (std::pair<int, int>{40, 30})) << "its top left kept where the window fits";
    rig.fake().work_area = reckless::gfx::RectI{0, 0, 1400, 1200};
    ASSERT_TRUE(rig.sizes.Choose(50));
    ASSERT_TRUE(rig.sizes.Choose(100));
    EXPECT_EQ(rig.window->Position(), (std::pair<int, int>{20, 30})) << "moved in from the right edge it crossed";
    EXPECT_EQ(rig.fake().on_screen_resizes(), 4);
}

TEST(WindowSizes, TheShortcutsStepThroughTheSizesAndStopAtTheEnds)
{
    WindowRig          rig;
    ControllerServices services;
    services.window_sizes       = [&rig] { return rig.sizes.Menu(); };
    services.choose_window_size = [&rig](int percent) { rig.sizes.Choose(percent); };
    MainView       view(Pieces());
    MainController controller(view, ControllerFiles{{}}, services);
    ASSERT_NE(controller.own_menus(), nullptr);
    reckless::app::MenuBarController &menus = *controller.own_menus();
    const auto                        press = [&](const Shortcut &shortcut) {
        const bool chosen = menus.PressShortcut(shortcut);
        controller.Frame(0.0); // the menus set again for what is possible now
        return chosen;
    };

    controller.Frame(0.0);
    EXPECT_TRUE(press(Shortcut::Primary('=')));
    EXPECT_EQ(rig.sizes.InForce(), 125);
    EXPECT_TRUE(press(Shortcut::Primary('=', true))) << "the plus key";
    EXPECT_EQ(rig.sizes.InForce(), 150);
    EXPECT_FALSE(press(Shortcut::Primary('+'))) << "the top: Larger is dim and its keys do nothing";
    EXPECT_EQ(rig.sizes.InForce(), 150);

    const std::vector<int> down{125, 100, 75, 50};
    for (const int percent : down)
    {
        EXPECT_TRUE(press(Shortcut::Primary('-')));
        EXPECT_EQ(rig.sizes.InForce(), percent);
    }
    EXPECT_FALSE(press(Shortcut::Primary('-'))) << "the bottom";
    EXPECT_EQ(rig.sizes.InForce(), 50);
    EXPECT_TRUE(press(Shortcut::Primary('0')));
    EXPECT_EQ(rig.sizes.InForce(), 100) << "the design size";
    EXPECT_TRUE(Find(ViewMenu(controller.menu_bar()), kWindowSize100)->checked);

    rig.fake().Resize(1700, 1300); // dragged between 100 % and 125 %
    controller.Frame(0.0);
    EXPECT_FALSE(Find(ViewMenu(controller.menu_bar()), kWindowSize100)->checked);
    EXPECT_TRUE(press(Shortcut::Primary('-')));
    EXPECT_EQ(rig.sizes.InForce(), 100) << "from a dragged size, the next size down past it";
    rig.fake().Resize(1700, 1300);
    controller.Frame(0.0);
    EXPECT_TRUE(press(Shortcut::Primary('=')));
    EXPECT_EQ(rig.sizes.InForce(), 125) << "and the next up";

    controller.Command(kWindowSize75);
    EXPECT_EQ(rig.sizes.InForce(), 75) << "a size chosen from the menu";
}

TEST(WindowSizes, TheSizeChosenIsKeptAndALaunchOpensAtIt)
{
    const std::filesystem::path file = reckless::test::ScratchPath("amphitheater-window-sizes") / "window.json";
    EXPECT_EQ(WindowSizes::Load(file), std::nullopt) << "no file";
    {
        WindowRig rig(file);
        EXPECT_EQ(rig.sizes.InForce(), 100) << "none kept: the design size";
        ASSERT_TRUE(rig.sizes.Choose(75));
        EXPECT_EQ(WindowSizes::Load(file), std::optional<int>(75)) << "kept as it is chosen";
    }
    {
        WindowRig rig(file);
        EXPECT_EQ(rig.sizes.InForce(), 75) << "a launch opens at the size kept";
        EXPECT_EQ(rig.WindowSize(), (PointI{1035, 840}));
        EXPECT_FLOAT_EQ(rig.root.zoom().zoom(), 0.75f);
        rig.fake().Resize(1200, 1000);
        rig.sizes.Save(); // as the app ends
        EXPECT_EQ(WindowSizes::Load(file), std::nullopt) << "dragged to another size: none kept";
    }
    {
        WindowRig rig(file);
        EXPECT_EQ(rig.sizes.InForce(), 100) << "and the next launch opens as a first one does";
        ASSERT_TRUE(rig.sizes.Choose(150));
        rig.sizes.Save();
        EXPECT_EQ(WindowSizes::Load(file), std::optional<int>(150));
    }

    reckless::core::json::Value odd = reckless::core::json::Value::MakeObject();
    odd.Set("format", 1);
    odd.Set("size", 80);
    ASSERT_TRUE(reckless::core::WriteJsonFile(file, odd));
    EXPECT_EQ(WindowSizes::Load(file), std::nullopt) << "a size the menu does not offer is none";
    WindowRig rig(file);
    EXPECT_EQ(rig.sizes.InForce(), 100);
}

} // namespace amphitheater::app
