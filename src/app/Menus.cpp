#include "Menus.h"

#include "Commands.h"

namespace uci_host::app
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
                                 Item("Paste FEN or PGN", kPasteFen, Shortcut::Primary('v'))}),
        MenuEntry::Menu("Game",
                        {Item("New game...", kNewGame, Shortcut::Primary('n')),
                         Item("Take back", kTakeBack, Shortcut::Primary('z'), state.can_take_back),
                         Item("Stop the game", kStopGame, {}, state.is_game_running), MenuEntry::Separator(),
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
        MenuEntry::Menu(
            "View", {Item("Flip the board", kFlip, Shortcut::Plain(U'f')), Item("Clear the engine log", kClearLog)}),
        MenuEntry::Menu("Help", {Item("About Reckless UCI Host", kAbout)}),
    }};
    for (MenuEntry &menu : bar.menus)
    {
        menu.enabled = !state.is_dialog_open;
    }
    return bar;
}

} // namespace uci_host::app
