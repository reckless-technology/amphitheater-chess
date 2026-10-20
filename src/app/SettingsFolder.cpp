#include "SettingsFolder.h"

#include "PresetFiles.h"

#include <system_error>

namespace amphitheater::app
{

namespace
{

/**
 * @param name A folder's name.
 * @return That folder under reckless-technology in the user's settings folder; empty where there is none.
 */
std::filesystem::path SuiteFolder(const char *name)
{
    const std::filesystem::path folder = reckless::core::UserSettingsFolder(reckless::core::kSuiteFolder);
    return folder.empty() ? folder : folder / name;
}

} // namespace

std::filesystem::path SettingsFolder()
{
    return SuiteFolder("amphitheater");
}

std::filesystem::path EarlierSettingsFolder()
{
    return SuiteFolder("uci-host");
}

std::vector<std::string> BringOverSettings(const std::filesystem::path &earlier, const std::filesystem::path &folder)
{
    std::vector<std::string> copied;
    std::error_code          error;
    if (earlier.empty() || folder.empty() || !std::filesystem::is_directory(earlier, error))
    {
        return copied;
    }
    for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(earlier, error))
    {
        if (!entry.is_regular_file(error))
        {
            continue;
        }
        const std::filesystem::path target = folder / entry.path().filename();
        if (std::filesystem::exists(target, error))
        {
            continue;
        }
        std::filesystem::create_directories(folder, error);
        if (std::filesystem::copy_file(entry.path(), target, error))
        {
            copied.push_back(entry.path().filename().string());
        }
    }
    return copied;
}

} // namespace amphitheater::app
