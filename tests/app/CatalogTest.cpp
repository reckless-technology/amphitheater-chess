// The engine list as it is kept: every engine's program, arguments, options, book, limits and declared options
// through JSON and a file and back, a missing file an empty list, and what is not a list refused.

#include "app/EngineCatalog.h"

#include "ScratchPath.h"

#include <gtest/gtest.h>

namespace amphitheater::app
{
namespace
{

/** An option declared as an engine declares it. */
uci::EngineOption Option(const char *line)
{
    return *uci::ParseOptionLine(line);
}

} // namespace

TEST(EngineCatalog, TheListRoundTripsThroughJsonAndAFile)
{
    EngineCatalog catalog;
    EngineEntry   entry;
    entry.config.name        = "A";
    entry.config.executable  = "/engines/a";
    entry.config.arguments   = {"--uci", "with space"};
    entry.config.options     = {{"Hash", "64"}, {"Clear Hash", ""}};
    entry.config.does_ponder = true;
    entry.author             = "Someone";
    entry.book_path          = "/books/a.bin";
    entry.book_depth         = 10;
    entry.depth              = 20;
    entry.is_analyzing       = false;
    entry.declared           = {Option("option name Hash type spin default 16 min 1 max 1024"),
                                Option("option name Style type combo default B var A var B")};
    catalog.engines.push_back(entry);
    EngineEntry bare;
    bare.config.executable = "/engines/b";
    catalog.engines.push_back(bare);

    const auto back = EngineCatalog::FromJson(catalog.ToJson());
    ASSERT_TRUE(back.has_value());
    ASSERT_EQ(back->engines.size(), 2u);
    EXPECT_EQ(back->ToJson(), catalog.ToJson());
    EXPECT_EQ(back->engines[0].declared[1].choices, (std::vector<std::string>{"A", "B"}));
    EXPECT_EQ(back->engines[1].DisplayName(), "b");

    const auto scratch = reckless::test::ScratchPath("amphitheater-engines");
    const auto file    = scratch / "amphitheater" / "engines.json";
    ASSERT_TRUE(catalog.Save(file));
    std::string error;
    const auto  loaded = EngineCatalog::Load(file, &error);
    ASSERT_TRUE(loaded.has_value()) << error;
    EXPECT_EQ(loaded->ToJson(), catalog.ToJson());
    EXPECT_TRUE(EngineCatalog::Load(scratch / "missing.json")->engines.empty());

    std::string why;
    EXPECT_FALSE(EngineCatalog::FromJson(reckless::core::json::Value(3), &why).has_value());
    EXPECT_FALSE(why.empty());
    EXPECT_FALSE(EngineCatalog::FromJson(reckless::core::json::Value::MakeObject(), &why).has_value());
}

TEST(EngineCatalog, AnEnginesSummaryAndOptionsReadAsTheStripShowsThem)
{
    EngineEntry entry;
    entry.config.executable = "/engines/pawnstar";
    entry.declared          = {Option("option name Threads type spin default 1 min 1 max 64"),
                               Option("option name Hash type spin default 16 min 1 max 1024")};
    EXPECT_EQ(entry.DisplayName(), "pawnstar");
    EXPECT_EQ(entry.OptionValue("hash"), "16") << "the declared default, the name compared without case";
    entry.SetOption("Hash", "64");
    entry.SetOption("HASH", "128");
    EXPECT_EQ(entry.config.options.size(), 1u);
    EXPECT_EQ(entry.OptionValue("Hash"), "128");
    entry.engine_name = "Pawnstar 2.3.1";
    EXPECT_EQ(entry.DisplayName(), "Pawnstar 2.3.1");
    entry.config.does_ponder = true;
    EXPECT_EQ(entry.Summary(), "Threads 1, Hash 128 MB, ponder");
    EXPECT_EQ(entry.OptionValue("Missing"), "");
}

} // namespace amphitheater::app
