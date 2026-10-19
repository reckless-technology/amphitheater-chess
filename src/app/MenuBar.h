#pragma once
/// @file MenuBar.h The window's menu bar, drawn: File, Edit, Game, Engines, Match, Tournament, View, Help, each a
/// press away from its menu in the toolkit's popup, its items asked for as it opens so they show what is possible now.

#include "Menu.h"
#include "View.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace uci_host::app
{

/** One menu of the bar: its title, and its items as they stand when it opens. */
struct MenuBarEntry
{
    std::string                                            title; ///< "File".
    std::function<std::vector<reckless::view::MenuItem>()> items; ///< Its items, asked for as it opens.
};

/** The bar of menus across the window's top. */
class MenuBar final : public reckless::view::View
{
  public:
    MenuBar();

    std::function<void(int command)> on_command; ///< An item chosen: its id.

    /** @param entries The menus, left to right. */
    void SetEntries(std::vector<MenuBarEntry> entries);

    /** @return The menus. */
    const std::vector<MenuBarEntry> &entries() const
    {
        return entries_;
    }

    /**
     * Where a menu's title sits on the bar.
     * @param index The menu, from 0.
     * @return Its rectangle, in the bar's coordinates.
     */
    reckless::gfx::RectF TitleBounds(std::size_t index) const;

    /**
     * A menu opened as a press on its title opens it (a suite's way in, too).
     * @param index The menu, from 0.
     */
    void Open(std::size_t index);

    /** @return The open menu's index, or -1. */
    int open_index() const
    {
        return open_;
    }

    /**
     * The bar, its titles, the open one and the one under the pointer lit.
     * @param c The canvas.
     */
    void Paint(reckless::gfx::Canvas &c) override;

    /**
     * Opens the menu under the press.
     * @param e The press.
     */
    void MouseDown(const reckless::view::MouseEvent &e) override;

    /**
     * Lights the title under the pointer.
     * @param e The move.
     */
    void MouseMove(const reckless::view::MouseEvent &e) override;

    /**
     * Puts the light out.
     * @param e The exit.
     */
    void MouseExit(const reckless::view::MouseEvent &e) override;

  private:
    /**
     * The menu under a point.
     * @param x The point's x.
     * @return Its index, or -1.
     */
    int EntryAt(float x) const;

    std::vector<MenuBarEntry>                    entries_;    ///< The menus.
    std::unique_ptr<reckless::view::MenuSession> session_;    ///< The open menu.
    int                                          open_  = -1; ///< The open menu's index.
    int                                          hover_ = -1; ///< The title under the pointer.
};

} // namespace uci_host::app
