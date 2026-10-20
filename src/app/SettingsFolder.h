#pragma once
/// @file SettingsFolder.h The app's folder in the user's settings, `reckless-technology/amphitheater/` - the engine
/// list, the window's size - and the files brought into it from `reckless-technology/uci-host/`, where earlier builds
/// kept them: each one the app's folder lacks is copied once as the app starts, and the earlier folder is left as it
/// is.

#include <filesystem>
#include <string>
#include <vector>

namespace amphitheater::app
{

/** @return `reckless-technology/amphitheater` in the user's settings folder; empty where there is none. */
std::filesystem::path SettingsFolder();

/**
 * @return `reckless-technology/uci-host` in the user's settings folder, where earlier builds kept their settings; empty
 *         where there is none.
 */
std::filesystem::path EarlierSettingsFolder();

/**
 * Copy into the app's folder every file of the earlier folder that it lacks. A file already there is never overwritten,
 * and nothing in the earlier folder is changed or removed.
 * @param earlier The earlier folder; nothing is copied where it is empty or missing.
 * @param folder The app's folder, made where missing.
 * @return The names of the files copied.
 */
std::vector<std::string> BringOverSettings(const std::filesystem::path &earlier, const std::filesystem::path &folder);

} // namespace amphitheater::app
