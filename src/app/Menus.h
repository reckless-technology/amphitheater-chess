#pragma once
/// @file Menus.h The menu bar's menus and their items, built from what the window can do as each menu opens.

#include "MenuBar.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace uci_host::app
{

/** What the menus need to know to show what is possible now. */
struct MenuState
{
    bool can_take_back         = false; ///< A person's move to take back.
    bool can_step_back         = false; ///< A position before the one shown.
    bool can_step_forward      = false; ///< A position after the one shown.
    bool is_game_running       = false; ///< A game being played on the board.
    bool is_analysing          = false; ///< The engines analysing.
    bool has_engines           = false; ///< An engine configured.
    bool is_match_running      = false; ///< A match being played.
    bool is_tournament_running = false; ///< A tournament being played.
    bool has_standings         = false; ///< A tournament's results to show.
    /** The games of a match or a tournament in progress that the board may follow: the game's number and its title. */
    std::vector<std::pair<int, std::string>> watchable_games;
};

/**
 * The menu bar's menus: File, Edit, Game, Engines, Match, Tournament, View, Help.
 * @param state What is possible now, asked as each menu opens.
 * @return The menus.
 */
std::vector<MenuBarEntry> BuildMenus(std::function<MenuState()> state);

} // namespace uci_host::app
