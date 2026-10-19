#include "Menus.h"

#include "Commands.h"

namespace uci_host::app
{

namespace
{

using reckless::view::MenuItem;

/**
 * An item.
 * @param text Its text.
 * @param command What it does.
 * @param is_enabled Whether it can be chosen now.
 * @param is_ticked Whether it shows a tick.
 * @return The item.
 */
MenuItem Item(std::string text, int command, bool is_enabled = true, bool is_ticked = false)
{
    MenuItem item;
    item.text    = std::move(text);
    item.id      = command;
    item.enabled = is_enabled;
    item.ticked  = is_ticked;
    return item;
}

/** @return A rule between groups. */
MenuItem Separator()
{
    MenuItem item;
    item.separator = true;
    return item;
}

/**
 * The games a person may follow on the board, as a submenu.
 * @param state What is going on.
 * @return The item, disabled with no game to follow.
 */
MenuItem WatchItems(const MenuState &state)
{
    MenuItem item = Item("Watch a game", 0, !state.watchable_games.empty());
    for (const auto &[number, title] : state.watchable_games)
    {
        item.submenu.push_back(Item(title, kWatchGameBase + number));
    }
    return item;
}

} // namespace

std::vector<MenuBarEntry> BuildMenus(std::function<MenuState()> state)
{
    std::vector<MenuBarEntry> menus;
    menus.push_back({"File", [] {
                         return std::vector<MenuItem>{Item("Open PGN...", kOpenPgn), Item("Save PGN...", kSavePgn),
                                                      Separator(), Item("Quit", kQuit)};
                     }});
    menus.push_back({"Edit", [] {
                         return std::vector<MenuItem>{Item("Copy FEN", kCopyFen), Item("Copy PGN", kCopyPgn),
                                                      Item("Paste FEN or PGN", kPasteFen)};
                     }});
    menus.push_back({"Game", [state] {
                         const MenuState s = state();
                         return std::vector<MenuItem>{Item("New game...", kNewGame),
                                                      Item("Take back", kTakeBack, s.can_take_back),
                                                      Item("Stop the game", kStopGame, s.is_game_running),
                                                      Separator(),
                                                      Item("First position", kFirst, s.can_step_back),
                                                      Item("Back a move", kBack, s.can_step_back),
                                                      Item("Forward a move", kForward, s.can_step_forward),
                                                      Item("Last position", kLast, s.can_step_forward)};
                     }});
    menus.push_back({"Engines", [state] {
                         const MenuState s = state();
                         return std::vector<MenuItem>{Item("Manage engines...", kEngineManager), Separator(),
                                                      Item("Analyse", kAnalyse, s.has_engines, s.is_analysing)};
                     }});
    menus.push_back({"Match", [state] {
                         const MenuState s = state();
                         return std::vector<MenuItem>{
                             Item("New match...", kNewMatch, !s.is_match_running && !s.is_tournament_running),
                             Item("Stop the match", kStopMatch, s.is_match_running), WatchItems(s)};
                     }});
    menus.push_back({"Tournament", [state] {
                         const MenuState s = state();
                         return std::vector<MenuItem>{
                             Item("New tournament...", kNewTournament, !s.is_match_running && !s.is_tournament_running),
                             Item("Stop the tournament", kStopTournament, s.is_tournament_running),
                             Item("Standings and crosstable", kShowStandings, s.has_standings), WatchItems(s)};
                     }});
    menus.push_back(
        {"View",
         [] { return std::vector<MenuItem>{Item("Flip the board", kFlip), Item("Clear the engine log", kClearLog)}; }});
    menus.push_back({"Help", [] { return std::vector<MenuItem>{Item("About Reckless UCI Host", kAbout)}; }});
    return menus;
}

} // namespace uci_host::app
