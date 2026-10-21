#pragma once
/// @file WindowSizes.h The window's sizes the View menu offers - the toolkit's (AppWindowSizes.h: 50 % to 150 % of the
/// layout's design size, the content drawn at exactly that scale, which one is in force, which the window's screen
/// takes, the next up and down) - with the one chosen kept across launches in
/// `reckless-technology/amphitheater/window.json` in the user's settings folder.

#include "AppPlatform.h"
#include "AppRoot.h"
#include "AppWindowSizes.h"
#include "Geometry.h"

#include <filesystem>
#include <optional>

namespace amphitheater::app
{

using reckless::app::kWindowSizes;   ///< The sizes the View menu offers, in percent, smallest first.
using reckless::app::WindowSizeMenu; ///< What the View menu shows of the window's size.

/**
 * The window's sizes (reckless::app::WindowSizes, whose logic they are) and the size chosen kept in a file, which a
 * launch opens at. Main thread.
 */
class WindowSizes
{
  public:
    /** How often the screen's room is asked again while the window is open: it may be moved to another screen. */
    static constexpr double kRefreshSeconds = reckless::app::WindowSizes::kRefreshSeconds;

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
    reckless::app::WindowSizes sizes_; ///< The sizes, the window held at them.
    std::filesystem::path      file_;  ///< Where the size chosen is kept.
};

} // namespace amphitheater::app
