// The menu bar and its menus: every menu the mockup names, in its order, each item enabled as the window's state says,
// the games a person may follow as a submenu, and the bar's titles laid out and pressed.

#include "app/Menus.h"
#include "app/Commands.h"
#include "app/MenuBar.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace uci_host::app
{
namespace
{

/**
 * An item of a menu by its command.
 * @param items The menu's items.
 * @param command The command.
 * @return The item, or null.
 */
const reckless::view::MenuItem *Find(const std::vector<reckless::view::MenuItem> &items, int command)
{
    const auto found =
        std::find_if(items.begin(), items.end(), [command](const auto &item) { return item.id == command; });
    return found == items.end() ? nullptr : &*found;
}

} // namespace

TEST(Menus, TheBarHoldsTheMockupsMenusAndEachItemFollowsTheWindowsState)
{
    MenuState                state;
    const auto               menus = BuildMenus([&state] { return state; });
    std::vector<std::string> titles;
    for (const MenuBarEntry &entry : menus)
    {
        titles.push_back(entry.title);
    }
    EXPECT_EQ(titles,
              (std::vector<std::string>{"File", "Edit", "Game", "Engines", "Match", "Tournament", "View", "Help"}));

    auto game = menus[2].items();
    EXPECT_FALSE(Find(game, kTakeBack)->enabled);
    EXPECT_FALSE(Find(game, kForward)->enabled);
    state.can_take_back    = true;
    state.can_step_forward = true;
    state.is_game_running  = true;
    game                   = menus[2].items();
    EXPECT_TRUE(Find(game, kTakeBack)->enabled);
    EXPECT_TRUE(Find(game, kStopGame)->enabled);
    EXPECT_TRUE(Find(game, kLast)->enabled);

    auto engines = menus[3].items();
    EXPECT_FALSE(Find(engines, kAnalyse)->enabled) << "no engine configured";
    state.has_engines  = true;
    state.is_analysing = true;
    engines            = menus[3].items();
    EXPECT_TRUE(Find(engines, kAnalyse)->enabled);
    EXPECT_TRUE(Find(engines, kAnalyse)->ticked);

    state.is_match_running = true;
    state.watchable_games  = {{3, "Game 3: A - B"}, {4, "Game 4: B - A"}};
    const auto match       = menus[4].items();
    EXPECT_FALSE(Find(match, kNewMatch)->enabled);
    EXPECT_TRUE(Find(match, kStopMatch)->enabled);
    const auto watch = std::find_if(match.begin(), match.end(), [](const auto &item) { return !item.submenu.empty(); });
    ASSERT_NE(watch, match.end());
    EXPECT_EQ(watch->submenu.size(), 2u);
    EXPECT_EQ(watch->submenu[1].id, kWatchGameBase + 4);

    const auto tournament = menus[5].items();
    EXPECT_FALSE(Find(tournament, kNewTournament)->enabled) << "not while a match runs";
    EXPECT_FALSE(Find(tournament, kShowStandings)->enabled);
    EXPECT_NE(Find(menus[0].items(), kOpenPgn), nullptr);
    EXPECT_NE(Find(menus[1].items(), kPasteFen), nullptr);
    EXPECT_NE(Find(menus[6].items(), kFlip), nullptr);
    EXPECT_NE(Find(menus[7].items(), kAbout), nullptr);
}

TEST(MenuBar, TitlesSitLeftToRightAndAPressOnOneOpensItsMenu)
{
    MenuBar bar;
    bar.SetBounds({0, 0, 900, 34});
    MenuState state;
    bar.SetEntries(BuildMenus([&state] { return state; }));
    const auto file = bar.TitleBounds(0);
    const auto edit = bar.TitleBounds(1);
    EXPECT_LT(file.right(), edit.x() + 0.01f);
    EXPECT_GT(file.width(), 0.0f);
    EXPECT_EQ(bar.TitleBounds(99).width(), 0.0f);

    reckless::view::MouseEvent press;
    press.position = edit.Centre();
    bar.MouseMove(press);
    bar.MouseDown(press);
    EXPECT_EQ(bar.open_index(), -1) << "in no window, no popup opens";
    bar.MouseExit(press);
    press.position = {899.0f, 10.0f};
    bar.MouseDown(press);
    EXPECT_EQ(bar.open_index(), -1);
}

} // namespace uci_host::app
