// The app's settings folder and the files brought into it from the folder earlier builds kept them in: each file the
// app's folder lacks copied once, nothing there overwritten, the earlier folder left as it was.

#include "app/SettingsFolder.h"

#include "ScratchPath.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace amphitheater::app
{
namespace
{

/**
 * Write a file.
 * @param file The file, its folder made where missing.
 * @param text What it holds.
 */
void Write(const std::filesystem::path &file, const std::string &text)
{
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
}

/**
 * @param file A file.
 * @return What it holds; empty for none.
 */
std::string Read(const std::filesystem::path &file)
{
    std::ifstream      in(file, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

/**
 * @param name The test's own name.
 * @return An empty scratch folder of the test's own.
 */
std::filesystem::path Fresh(const std::string &name)
{
    const std::filesystem::path folder = reckless::test::ScratchPath(name);
    std::filesystem::remove_all(folder);
    return folder;
}

} // namespace

TEST(SettingsFolder, IsTheAppsOwnBesideTheEarlierOne)
{
    const std::filesystem::path folder = SettingsFolder();
    if (folder.empty())
    {
        GTEST_SKIP() << "no settings folder on this machine";
    }
    EXPECT_EQ(folder.filename(), "amphitheater");
    EXPECT_EQ(EarlierSettingsFolder().filename(), "uci-host");
    EXPECT_EQ(folder.parent_path(), EarlierSettingsFolder().parent_path());
}

TEST(SettingsFolder, BringsOverEveryFileTheAppsFolderLacks)
{
    const std::filesystem::path root    = Fresh("amphitheater-settings-copy");
    const std::filesystem::path earlier = root / "uci-host";
    const std::filesystem::path folder  = root / "amphitheater";
    Write(earlier / "engines.json", "{\"engines\": []}");
    Write(earlier / "window.json", "{\"size\": 125}");
    std::filesystem::create_directories(earlier / "a-folder");

    std::vector<std::string> copied = BringOverSettings(earlier, folder);
    std::sort(copied.begin(), copied.end());
    EXPECT_EQ(copied, (std::vector<std::string>{"engines.json", "window.json"})) << "files only, not folders";
    EXPECT_EQ(Read(folder / "engines.json"), "{\"engines\": []}");
    EXPECT_EQ(Read(folder / "window.json"), "{\"size\": 125}");
    EXPECT_FALSE(std::filesystem::exists(folder / "a-folder"));
    EXPECT_EQ(Read(earlier / "engines.json"), "{\"engines\": []}") << "the earlier folder left as it was";

    EXPECT_TRUE(BringOverSettings(earlier, folder).empty()) << "a second start copies nothing";
}

TEST(SettingsFolder, NeverOverwritesWhatTheAppsFolderHolds)
{
    const std::filesystem::path root    = Fresh("amphitheater-settings-keep");
    const std::filesystem::path earlier = root / "uci-host";
    const std::filesystem::path folder  = root / "amphitheater";
    Write(earlier / "engines.json", "old");
    Write(earlier / "window.json", "old size");
    Write(folder / "engines.json", "new");

    EXPECT_EQ(BringOverSettings(earlier, folder), std::vector<std::string>{"window.json"});
    EXPECT_EQ(Read(folder / "engines.json"), "new");
    EXPECT_EQ(Read(folder / "window.json"), "old size");
}

TEST(SettingsFolder, BringsOverNothingWithoutAnEarlierFolder)
{
    const std::filesystem::path root = Fresh("amphitheater-settings-none");
    EXPECT_TRUE(BringOverSettings(root / "uci-host", root / "amphitheater").empty());
    EXPECT_FALSE(std::filesystem::exists(root / "amphitheater")) << "nothing made for nothing";
    EXPECT_TRUE(BringOverSettings({}, root / "amphitheater").empty());
    EXPECT_TRUE(BringOverSettings(root, {}).empty());
}

} // namespace amphitheater::app
