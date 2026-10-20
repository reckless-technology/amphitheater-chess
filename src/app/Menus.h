#pragma once
/// @file Menus.h The menu bar's menus, their items and their keyboard shortcuts, built from what the window can do
/// now - the toolkit's one description of a bar, which macOS shows in the screen's menu bar, Windows on the window and
/// Linux drawn across its top.

#include "MenuBar.h"
#include "WindowSizes.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace amphitheater::app
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
    bool is_dialog_open        = false; ///< A dialog over the window, which every menu waits for.
    /** The games of a match or a tournament in progress that the board may follow: the game's number and its title. */
    std::vector<std::pair<int, std::string>> watchable_games;
    /** The update check's revision, so its menu says what the check says now. */
    std::uint64_t updates_revision = 0;
    /** The window's sizes: the one ticked, those its screen takes, the next up and down. */
    WindowSizeMenu window_sizes;

    /**
     * Whether two states show the same menus.
     * @param a One state.
     * @param b The other.
     * @return Whether every field is the same.
     */
    friend bool operator==(const MenuState &a, const MenuState &b) = default;
};

/**
 * The menu bar: File, Edit, Game, Engines, Match, Tournament, View, Help, each item's id its command (Commands.h) and
 * its shortcut the primary modifier (Command on a Mac, Control elsewhere) with a letter, or a plain key for stepping
 * through the game and turning the board; View's window sizes on the primary modifier with 0 (100 %), = or + (the next
 * size up) and - (the next size down). Every menu is disabled while a dialog is open, so neither a click nor a
 * shortcut acts behind it.
 * @param state What is possible now: the items enabled and ticked by it.
 * @return The bar.
 */
reckless::app::MenuBar BuildMenuBar(const MenuState &state);

} // namespace amphitheater::app
