#include "Menus.h"

#include "Commands.h"
#include "PieceSet.h"

#include <cstddef>
#include <string>
#include <vector>

namespace amphitheater::app
{

namespace
{

using reckless::app::MenuEntry;
using reckless::app::Shortcut;
using reckless::app::ShortcutKey;

/**
 * An item.
 * @param title Its text.
 * @param command What it does: its id.
 * @param shortcut Its key; none for no key.
 * @param is_enabled Whether it can be chosen now.
 * @param is_checked Whether it shows a tick.
 * @return The item.
 */
MenuEntry Item(std::string title, int command, Shortcut shortcut = {}, bool is_enabled = true, bool is_checked = false)
{
    MenuEntry entry = MenuEntry::Item(std::move(title), command, shortcut);
    entry.enabled   = is_enabled;
    entry.checked   = is_checked;
    return entry;
}

/**
 * The games a person may follow on the board, as a submenu.
 * @param state What is going on.
 * @return The entry, disabled with no game to follow.
 */
MenuEntry WatchItems(const MenuState &state)
{
    std::vector<MenuEntry> games;
    for (const auto &[number, title] : state.watchable_games)
    {
        games.push_back(Item(title, kWatchGameBase + number));
    }
    if (games.empty())
    {
        return Item("Watch a game", kNoCommand, {}, false);
    }
    MenuEntry entry = MenuEntry::Menu("Watch a game", std::move(games));
    return entry;
}

/**
 * View > Pieces: each set the binary carries, the one the board is drawn with ticked.
 * @param state What is possible now.
 * @return The submenu.
 */
MenuEntry PiecesItems(const MenuState &state)
{
    std::vector<MenuEntry> sets;
    for (std::size_t index = 0; index < kPieceSets.size(); ++index)
    {
        sets.push_back(Item(kPieceSets[index].label, kPiecesBase + static_cast<int>(index), {}, true,
                            state.piece_set == kPieceSets[index].id));
    }
    return MenuEntry::Menu("Pieces", std::move(sets));
}

/**
 * The View menu: the board and the log, then the window's sizes - each ticked while it is the size drawn and dim where
 * the window's screen does not take it - and a step up and down through them.
 * @param state What is possible now.
 * @return The menu's entries.
 */
std::vector<MenuEntry> ViewItems(const MenuState &state)
{
    const WindowSizeMenu  &sizes = state.window_sizes;
    std::vector<MenuEntry> items{Item("Flip the board", kFlip, Shortcut::Plain(U'f')),
                                 Item("Engine log", kShowLog, Shortcut::Primary('l'), true, state.is_log_open),
                                 Item("Clear the engine log", kClearLog), PiecesItems(state), MenuEntry::Separator()};
    for (std::size_t index = 0; index < kWindowSizes.size(); ++index)
    {
        const int percent = kWindowSizes[index];
        items.push_back(Item(std::to_string(percent) + "%", kWindowSize50 + static_cast<int>(index),
                             percent == 100 ? Shortcut::Primary('0') : Shortcut{}, sizes.fits[index],
                             sizes.in_force == percent));
    }
    items.push_back(MenuEntry::Separator());
    MenuEntry larger = Item("Larger", kWindowLarger, Shortcut::Primary('='), sizes.larger != 0);
    // The plus key - Shift with = - and the keypad's plus choose it too.
    larger.other_shortcuts = {Shortcut::Primary('=', true), Shortcut::Primary('+')};
    MenuEntry smaller      = Item("Smaller", kWindowSmaller, Shortcut::Primary('-'), sizes.smaller != 0);
    items.push_back(std::move(larger));
    items.push_back(std::move(smaller));
    return items;
}

} // namespace

reckless::app::MenuBar BuildMenuBar(const MenuState &state)
{
    const bool             is_competing = state.is_match_running || state.is_tournament_running;
    reckless::app::MenuBar bar{{
        MenuEntry::Menu("File", {Item("Open PGN...", kOpenPgn, Shortcut::Primary('o')),
                                 Item("Save PGN...", kSavePgn, Shortcut::Primary('s')), MenuEntry::Separator(),
                                 Item("Quit", kQuit, Shortcut::Primary('q'))}),
        MenuEntry::Menu("Edit", {Item("Copy FEN", kCopyFen, Shortcut::Primary('c')),
                                 Item("Copy PGN", kCopyPgn, Shortcut::Primary('c', true)),
                                 Item("Paste FEN or PGN", kPasteFen, Shortcut::Primary('v')), MenuEntry::Separator(),
                                 Item("Set up position...", kSetUpPosition, Shortcut::Primary('p'))}),
        MenuEntry::Menu("Game",
                        {Item("New game...", kNewGame, Shortcut::Primary('n')),
                         Item("Take back", kTakeBack, Shortcut::Primary('z'), state.can_take_back),
                         Item("Stop the game", kStopGame, {}, state.is_game_running), MenuEntry::Separator(),
                         Item("Promote variation", kPromoteVariation, {}, state.is_in_variation),
                         Item("Delete variation", kDeleteVariation, {}, state.is_in_variation), MenuEntry::Separator(),
                         Item("Analyse the game...", kAnalyseGame, {}, state.can_analyse_game),
                         Item("Stop the game analysis", kStopGameAnalysis, {}, state.is_analysing_game),
                         MenuEntry::Separator(),
                         Item("First position", kFirst, Shortcut::Plain(ShortcutKey::kHome), state.can_step_back),
                         Item("Back a move", kBack, Shortcut::Plain(ShortcutKey::kLeft), state.can_step_back),
                         Item("Forward a move", kForward, Shortcut::Plain(ShortcutKey::kRight), state.can_step_forward),
                         Item("Last position", kLast, Shortcut::Plain(ShortcutKey::kEnd), state.can_step_forward)}),
        MenuEntry::Menu("Engines",
                        {Item("Manage engines...", kEngineManager, Shortcut::Primary('e')), MenuEntry::Separator(),
                         Item("Analyse", kAnalyse, {}, state.has_engines, state.is_analysing)}),
        MenuEntry::Menu("Match", {Item("New match...", kNewMatch, {}, !is_competing),
                                  Item("Stop the match", kStopMatch, {}, state.is_match_running), WatchItems(state)}),
        MenuEntry::Menu("Tournament",
                        {Item("New tournament...", kNewTournament, {}, !is_competing),
                         Item("Stop the tournament", kStopTournament, {}, state.is_tournament_running),
                         Item("Standings and crosstable", kShowStandings, {}, state.has_standings), WatchItems(state)}),
        MenuEntry::Menu("View", ViewItems(state)),
        MenuEntry::Menu("Help", {Item("About Reckless Amphitheater", kAbout)}),
    }};
    for (MenuEntry &menu : bar.menus)
    {
        menu.enabled = !state.is_dialog_open;
    }
    bar.about_id = kAbout; // the Mac app menu's About opens Help > About, not the system's standard panel
    return bar;
}

} // namespace amphitheater::app
