#pragma once
/// @file LayoutSettings.h What the window keeps of its layout across launches - the engine log's height and whether
/// it shows - in `reckless-technology/amphitheater/layout.json` in the user's settings folder.

#include "MainView.h"

#include <filesystem>

namespace amphitheater::app
{

/** The window's layout a launch opens with. */
struct LayoutSettings
{
    int  log_height  = MainView::kLogDefaultHeight; ///< The engine log's height asked for, in design pixels.
    bool is_log_open = true;                        ///< Whether the engine log shows.

    /**
     * The settings kept in a file.
     * @param file The file.
     * @return Them; the defaults for no file, one that cannot be read, or a value it lacks or holds out of range.
     */
    static LayoutSettings Load(const std::filesystem::path &file);

    /**
     * Keep the settings in a file.
     * @param file The file, its folder made where missing.
     * @return Whether it was written.
     */
    bool Save(const std::filesystem::path &file) const;

    /** @return `reckless-technology/amphitheater/layout.json` in the user's settings folder; empty where there is
        none. */
    static std::filesystem::path DefaultFile();

    /**
     * Whether two layouts are the same.
     * @param a One.
     * @param b The other.
     * @return Whether every field is the same.
     */
    friend bool operator==(const LayoutSettings &a, const LayoutSettings &b) = default;
};

} // namespace amphitheater::app
