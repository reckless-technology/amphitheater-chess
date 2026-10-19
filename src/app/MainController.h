#pragma once
/// @file MainController.h What the main window does: it holds the game shown and the position the board shows in it,
/// answers the menus, the plates, the keys, the board and the lists, and sets what the view shows.

#include "MainView.h"
#include "Menus.h"
#include "chess/Game.h"

#include <string>
#include <vector>

namespace uci_host::app
{

/** What the game card says of a game beside its moves. */
struct GameInfo
{
    std::string white = "White"; ///< White's name.
    std::string black = "Black"; ///< Black's name.
    std::string event;           ///< The event, as PGN names it.
    std::string opening;         ///< The opening's name.
    std::string time_control;    ///< The time control, as a person reads it.
};

/** The main window's controller. */
class MainController
{
  public:
    /** @param view The view it drives; outlives the controller. */
    explicit MainController(MainView &view);

    /** Not copyable: the view's callbacks hold it. */
    MainController(const MainController &) = delete;
    /** Not assignable: the view's callbacks hold it. @return Never. */
    MainController &operator=(const MainController &) = delete;

    /**
     * Show a game, at its last position.
     * @param game The game.
     * @param info What the card says of it.
     */
    void ShowGame(chess::Game game, GameInfo info);

    /**
     * Do a command.
     * @param command A Command.
     */
    void Command(int command);

    /**
     * Show a position of the game.
     * @param ply 0 for the start, up to the game's length.
     */
    void ShowPly(int ply);

    /**
     * One display frame.
     * @param seconds Since the last.
     */
    void Frame(double seconds);

    /** @return The game shown. */
    const chess::Game &game() const
    {
        return game_;
    }

    /** @return The position the board shows: 0 for the start, n after the n-th move. */
    int view_ply() const
    {
        return view_ply_;
    }

    /** @return Whether Black is at the bottom. */
    bool flipped() const
    {
        return is_flipped_;
    }

    /** @return What the menus show as possible now. */
    MenuState Menus() const;

  private:
    /** The view brought to the game, the position shown and the players. */
    void Refresh();

    MainView   &view_;               ///< The view.
    chess::Game game_;               ///< The game shown.
    GameInfo    info_;               ///< What the card says of it.
    int         view_ply_   = 0;     ///< The position shown.
    bool        is_flipped_ = false; ///< Black at the bottom.
};

} // namespace uci_host::app
