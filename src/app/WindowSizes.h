#pragma once
/// @file WindowSizes.h The window's sizes the View menu offers - 50 % to 150 % of the layout's design size, the
/// content drawn at exactly that scale - which one is in force, which the window's screen takes, the next up and down,
/// and the one chosen kept across launches in `reckless-technology/amphitheater/window.json` in the user's settings
/// folder.

#include "AppPlatform.h"
#include "AppRoot.h"
#include "Geometry.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>

namespace amphitheater::app
{

/** The sizes the View menu offers, in percent of the layout's design size, smallest first. */
inline constexpr std::array<int, 5> kWindowSizes{50, 75, 100, 125, 150};

/** What the View menu shows of the window's size. */
struct WindowSizeMenu
{
    bool has_window = false; ///< A window to size: none in a suite or the render tool, where every size is dim.
    /** The size the window is drawn at, one of kWindowSizes; 0 for none - the person dragged its frame to another. */
    int in_force = 0;
    /** Whether each size's window, in kWindowSizes' order, stands wholly on the screen the window is on. */
    std::array<bool, kWindowSizes.size()> fits{};
    int larger  = 0; ///< The next size up that fits, from the size the window is now; 0 at the top.
    int smaller = 0; ///< The next size down, likewise; 0 at the bottom.

    /**
     * Whether two states show the same menu.
     * @param a One state.
     * @param b The other.
     * @return Whether every field is the same.
     */
    friend bool operator==(const WindowSizeMenu &a, const WindowSizeMenu &b) = default;
};

/**
 * The window's sizes: the root held at a size from the View menu (AppRoot::SetFixedZoom), the window sized to it on
 * its screen (AppWindow::SetContentSizeOnScreen), the sizes its screen takes asked of the window
 * (AppWindow::LargestContent) - a size it does not take is dim rather than shrunk, so a ticked size is always the size
 * drawn - and the size chosen kept in a file, which a launch opens at. Main thread.
 */
class WindowSizes
{
  public:
    /** How often the screen's room is asked again while the window is open: it may be moved to another screen. */
    static constexpr double kRefreshSeconds = 0.5;

    /**
     * @param root The window's root; outlives this.
     * @param design The layout's design size, which 100 % is, in design pixels.
     * @param file Where the size chosen is kept; empty to keep none.
     */
    WindowSizes(reckless::app::AppRoot &root, reckless::gfx::PointI design, std::filesystem::path file);

    /**
     * Before the window opens: the root held at the size kept, or at 100 % - the design size, as a first launch
     * opens - with none kept. The window then opens round the root, held to what its screen takes (RunApp), which
     * lets the size go where the screen is smaller.
     */
    void Restore();

    /**
     * The window to size, once it is open; null as it closes.
     * @param window The window, or null.
     */
    void SetWindow(reckless::app::AppWindow *window);

    /**
     * Choose a size: the root held at it, the window sized to it and kept on its screen, the choice kept.
     * @param percent One of kWindowSizes.
     * @return Whether it was taken: not for a size not offered, with no window, or one the screen does not take.
     */
    bool Choose(int percent);

    /**
     * One display frame: the screen's room asked again every kRefreshSeconds.
     * @param seconds Since the last.
     */
    void Frame(double seconds);

    /** @return What the View menu shows now. */
    WindowSizeMenu Menu() const;

    /** @return The size the window is drawn at, one of kWindowSizes; 0 for none. */
    int InForce() const;

    /** The size in force kept, or none where the person has dragged the window to another: what the next launch
        opens at. */
    void Save() const;

    /**
     * @param percent A size.
     * @return Whether the View menu offers it.
     */
    static bool IsOffered(int percent);

    /**
     * The size kept in a file.
     * @param file The file.
     * @return The size, one of kWindowSizes; none for no file, no size or one not offered.
     */
    static std::optional<int> Load(const std::filesystem::path &file);

    /**
     * Keep a size in a file.
     * @param file The file, its folder made where missing.
     * @param percent The size; none to keep none.
     * @return Whether it was written.
     */
    static bool Store(const std::filesystem::path &file, std::optional<int> percent);

    /** @return `reckless-technology/amphitheater/window.json` in the user's settings folder; empty where there is none.
     */
    static std::filesystem::path DefaultFile();

  private:
    /**
     * @param percent A size.
     * @return The window's content at it: the design size times the percent, in design pixels.
     */
    reckless::gfx::PointI SizeAt(int percent) const;

    /**
     * @param percent A size.
     * @return Whether its window stands wholly on the window's screen; true where the screen's room is not known.
     */
    bool Fits(int percent) const;

    /** The screen's room asked of the window. */
    void RefreshRoom();

    reckless::app::AppRoot   &root_;              ///< The window's root.
    reckless::gfx::PointI     design_;            ///< The layout's design size.
    std::filesystem::path     file_;              ///< Where the size chosen is kept.
    reckless::app::AppWindow *window_  = nullptr; ///< The window, while it is open.
    reckless::gfx::PointI     room_    = {0, 0};  ///< The largest content its screen takes; {0, 0} for not known.
    double                    elapsed_ = 0.0;     ///< Seconds since the room was last asked.
};

} // namespace amphitheater::app
