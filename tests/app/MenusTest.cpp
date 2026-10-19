// The menu bar and its menus: every menu the mockup names, in its order, each item enabled as the window's state says,
// the games a person may follow as a submenu, every shortcut the window answers to and the command it chooses, the
// whole bar waiting while a dialog is open, and the window's own bar drawn across its top where no platform shows one.

#include "app/Menus.h"
#include "app/Commands.h"
#include "app/Faces.h"
#include "app/MainController.h"
#include "app/MainView.h"
#include "app/PieceSet.h"
#include "chess/Notation.h"

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <vector>

namespace uci_host::app
{
namespace
{

using reckless::app::MenuEntry;
using reckless::app::Shortcut;
using reckless::app::ShortcutKey;

/**
 * An entry of a menu by its command, its submenus searched too.
 * @param entries The menu's entries.
 * @param command The command.
 * @return The entry, or null.
 */
const MenuEntry *Find(const std::vector<MenuEntry> &entries, int command)
{
    for (const MenuEntry &entry : entries)
    {
        if (entry.id == command && entry.submenu.empty())
        {
            return &entry;
        }
        if (const MenuEntry *found = Find(entry.submenu, command))
        {
            return found;
        }
    }
    return nullptr;
}

/** The pieces, read once for every test. */
const PieceSet &Pieces()
{
    static const PieceSet pieces = [] {
        InstallFaces();
        return PieceSet::FromEmbedded();
    }();
    return pieces;
}

} // namespace

TEST(Menus, TheBarHoldsTheMockupsMenusAndEachItemFollowsTheWindowsState)
{
    MenuState                state;
    reckless::app::MenuBar   bar = BuildMenuBar(state);
    std::vector<std::string> titles;
    for (const MenuEntry &menu : bar.menus)
    {
        titles.push_back(menu.title);
        EXPECT_TRUE(menu.enabled);
    }
    EXPECT_EQ(titles,
              (std::vector<std::string>{"File", "Edit", "Game", "Engines", "Match", "Tournament", "View", "Help"}));

    EXPECT_FALSE(Find(bar.menus[2].submenu, kTakeBack)->enabled);
    EXPECT_FALSE(Find(bar.menus[2].submenu, kForward)->enabled);
    state.can_take_back    = true;
    state.can_step_forward = true;
    state.is_game_running  = true;
    bar                    = BuildMenuBar(state);
    EXPECT_TRUE(Find(bar.menus[2].submenu, kTakeBack)->enabled);
    EXPECT_TRUE(Find(bar.menus[2].submenu, kStopGame)->enabled);
    EXPECT_TRUE(Find(bar.menus[2].submenu, kLast)->enabled);

    EXPECT_FALSE(Find(bar.menus[3].submenu, kAnalyse)->enabled) << "no engine configured";
    state.has_engines  = true;
    state.is_analysing = true;
    bar                = BuildMenuBar(state);
    EXPECT_TRUE(Find(bar.menus[3].submenu, kAnalyse)->enabled);
    EXPECT_TRUE(Find(bar.menus[3].submenu, kAnalyse)->checked);

    state.is_match_running = true;
    state.watchable_games  = {{3, "Game 3: A - B"}, {4, "Game 4: B - A"}};
    bar                    = BuildMenuBar(state);
    EXPECT_FALSE(Find(bar.menus[4].submenu, kNewMatch)->enabled);
    EXPECT_TRUE(Find(bar.menus[4].submenu, kStopMatch)->enabled);
    const MenuEntry &watch = bar.menus[4].submenu.back();
    ASSERT_EQ(watch.submenu.size(), 2u);
    EXPECT_EQ(watch.submenu[1].id, kWatchGameBase + 4);

    EXPECT_FALSE(Find(bar.menus[5].submenu, kNewTournament)->enabled) << "not while a match runs";
    EXPECT_FALSE(Find(bar.menus[5].submenu, kShowStandings)->enabled);
    EXPECT_NE(Find(bar.menus[0].submenu, kOpenPgn), nullptr);
    EXPECT_NE(Find(bar.menus[1].submenu, kPasteFen), nullptr);
    EXPECT_NE(Find(bar.menus[6].submenu, kFlip), nullptr);
    EXPECT_NE(Find(bar.menus[7].submenu, kAbout), nullptr);

    state.is_dialog_open = true;
    for (const MenuEntry &menu : BuildMenuBar(state).menus)
    {
        EXPECT_FALSE(menu.enabled) << menu.title << " waits for the dialog";
    }
}

TEST(Menus, EveryShortcutChoosesItsCommandThroughTheBar)
{
    MainView                         view(Pieces());
    MainController                   controller(view, ControllerFiles{{}});
    std::vector<int>                 told;
    reckless::app::MenuBarController menus;
    MenuState                        state;
    state.can_take_back    = true;
    state.can_step_back    = true;
    state.can_step_forward = true;
    menus.Set(BuildMenuBar(state), [&](int id) { told.push_back(id); });

    const struct
    {
        Shortcut shortcut;
        int      command;
    } expected[] = {{Shortcut::Primary('n'), kNewGame},           {Shortcut::Primary('o'), kOpenPgn},
                    {Shortcut::Primary('s'), kSavePgn},           {Shortcut::Primary('q'), kQuit},
                    {Shortcut::Primary('c'), kCopyFen},           {Shortcut::Primary('c', true), kCopyPgn},
                    {Shortcut::Primary('v'), kPasteFen},          {Shortcut::Primary('z'), kTakeBack},
                    {Shortcut::Primary('e'), kEngineManager},     {Shortcut::Plain(ShortcutKey::kHome), kFirst},
                    {Shortcut::Plain(ShortcutKey::kLeft), kBack}, {Shortcut::Plain(ShortcutKey::kRight), kForward},
                    {Shortcut::Plain(ShortcutKey::kEnd), kLast},  {Shortcut::Plain(U'f'), kFlip}};

    for (const auto &[shortcut, command] : expected)
    {
        told.clear();
        EXPECT_TRUE(menus.PressShortcut(shortcut)) << command;
        EXPECT_EQ(told, std::vector<int>{command});
    }
    EXPECT_FALSE(menus.PressShortcut(Shortcut::Primary('k'))) << "no entry";
    EXPECT_FALSE(menus.PressShortcut(Shortcut::Plain(U'o'))) << "O alone is no shortcut";

    // The window's own bar: drawn across its top, set by the controller, its shortcuts choosing the controller's
    // commands - here Command-Z, which takes back a move made on the board.
    ASSERT_NE(controller.own_menus(), nullptr);
    ASSERT_NE(view.menu_bar(), nullptr);
    view.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});
    EXPECT_EQ(view.menu_bar()->bounds().height(), MainView::kMenuHeight);
    view.board().on_move(*chess::ParseUciMove(controller.game().CurrentPosition(), "e2e4"));
    controller.Frame(0.0);
    EXPECT_TRUE(controller.own_menus()->PressShortcut(Shortcut::Primary('z')));
    EXPECT_EQ(controller.game().PlyCount(), 0);
    EXPECT_EQ(controller.menu_bar().menus.size(), 8u);
}

TEST(Menus, AWindowWhoseBarThePlatformShowsDrawsNoneAndLaysOutFromTheTop)
{
    MainView                            view(Pieces());
    std::vector<int>                    told;
    std::function<void(int)>            chosen;
    std::vector<reckless::app::MenuBar> set;
    ControllerServices                  services;
    services.set_menu_bar = [&](const reckless::app::MenuBar &bar, std::function<void(int id)> on_chosen) {
        set.push_back(bar);
        chosen = std::move(on_chosen);
    };
    view.SetMenuBarView(nullptr);
    MainController controller(view, ControllerFiles{{}}, services);
    EXPECT_EQ(controller.own_menus(), nullptr);
    EXPECT_EQ(view.menu_bar(), nullptr);
    ASSERT_EQ(set.size(), 1u) << "the platform's bar set once the window is made";
    controller.Frame(0.0);
    EXPECT_EQ(set.size(), 1u) << "and not again while nothing it shows has changed";
    view.board().on_move(*chess::ParseUciMove(controller.game().CurrentPosition(), "e2e4"));
    controller.Frame(0.0);
    ASSERT_EQ(set.size(), 2u) << "a move to take back: set again";
    EXPECT_TRUE(Find(set.back().menus[2].submenu, kTakeBack)->enabled);
    ASSERT_TRUE(chosen);
    chosen(kFlip);
    EXPECT_TRUE(controller.flipped()) << "a platform's choice is the controller's command";

    view.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});
    EXPECT_LT(view.top_strip().bounds().y(), MainView::kMenuHeight) << "no bar drawn: the layout starts at the top";
}

} // namespace uci_host::app
