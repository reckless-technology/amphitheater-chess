// The engines the install carries and the person's engine list: a fresh list seeded with every one, an upgrade moving
// them to the new install's paths and versions with the person's edits kept, a removed one staying removed, an engine
// the install no longer carries left alone, the person's own engines untouched, each machine given the build its
// processor runs, the manifest found beside the app, and the controller seeding and saving the list as it starts.

#include "app/BundledEngines.h"
#include "app/EngineManagerDialog.h"
#include "app/Faces.h"
#include "app/MainController.h"
#include "app/MainView.h"
#include "app/PieceSet.h"

#include "JsonFile.h"
#include "ScratchPath.h"

#include <gtest/gtest.h>

#include <fstream>

namespace uci_host::app
{
namespace
{

namespace json = reckless::core::json;

/** The pieces, read once for every test. */
const PieceSet &Pieces()
{
    static const PieceSet pieces = [] {
        InstallFaces();
        return PieceSet::FromEmbedded();
    }();
    return pieces;
}

/** A file written with some bytes, its folder made first. */
void Touch(const std::filesystem::path &file)
{
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << "an engine";
}

/** A text as UTF-8, as the engine list keeps a program. */
std::string Text(const std::filesystem::path &path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

/**
 * A manifest as tools/bundled_engines.py writes it, for engines at a version, every build's file written.
 * @param folder The engines' folder.
 * @param version The version of every engine.
 * @param missing The ids of the engines whose files the install lacks.
 */
void WriteManifest(const std::filesystem::path &folder, const std::string &version,
                   const std::vector<std::string> &missing = {})
{
    struct Spec
    {
        const char              *id;
        const char              *name;
        std::vector<std::string> builds;
        std::vector<std::string> requires_level;
    };

    const std::vector<Spec> specs = {
        {"pawnstar", "Pawnstar", {"pawnstar/pawnstar"}, {"x86-64-v3"}},
        {"zenith",
         "Zenith",
         {"zenith/x86-64-v3-pext/zenith", "zenith/x86-64-v2/zenith"},
         {"x86-64-v3-fast-pext", "x86-64-v2"}},
        {"stoopid-chess", "Stoopid Chess", {"stoopid-chess/stoopid-chess"}, {""}},
    };
    json::Value engines = json::Value::MakeArray();
    for (const Spec &spec : specs)
    {
        json::Value engine = json::Value::MakeObject();
        engine.Set("id", spec.id);
        engine.Set("name", spec.name);
        engine.Set("version", version);
        engine.Set("engine_name", std::string(spec.name) + " " + version);
        engine.Set("author", "Jonny Reckless");
        json::Value options = json::Value::MakeArray();
        for (const auto &[name, value] :
             std::vector<std::pair<std::string, std::string>>{{"Threads", "1"}, {"Hash", "64"}})
        {
            json::Value option = json::Value::MakeObject();
            option.Set("name", name);
            option.Set("value", value);
            options.Append(std::move(option));
        }
        engine.Set("options", std::move(options));
        json::Value declared = json::Value::MakeArray();
        json::Value hash     = json::Value::MakeObject();
        hash.Set("name", "Hash");
        hash.Set("type", "spin");
        hash.Set("default", "16");
        hash.Set("min", 1);
        hash.Set("max", 1024 * (version == "1.0.0" ? 1 : 2));
        declared.Append(std::move(hash));
        engine.Set("declared", std::move(declared));
        json::Value builds = json::Value::MakeArray();
        for (std::size_t i = 0; i < spec.builds.size(); ++i)
        {
            json::Value build = json::Value::MakeObject();
            build.Set("executable", spec.builds[i]);
            build.Set("requires", spec.requires_level[i]);
            builds.Append(std::move(build));
            if (std::find(missing.begin(), missing.end(), spec.id) == missing.end())
            {
                Touch(folder / spec.builds[i]);
            }
        }
        engine.Set("builds", std::move(builds));
        engines.Append(std::move(engine));
    }
    json::Value manifest = json::Value::MakeObject();
    manifest.Set("format", 1);
    manifest.Set("engines", std::move(engines));
    ASSERT_TRUE(reckless::core::WriteJsonFile(folder / "bundled.json", manifest));
}

/** A machine that runs both x86-64 levels. */
constexpr CpuLevels kModernCpu{true, true, true};

/** A person's own engine. */
EngineEntry OwnEngine()
{
    EngineEntry own;
    own.config.name       = "Mine";
    own.config.executable = "/engines/mine";
    own.config.options    = {{"Threads", "8"}};
    return own;
}

/** @return An entry as the list's file keeps it. */
json::Value EntryJson(const EngineEntry &entry)
{
    EngineCatalogue one;
    one.engines.push_back(entry);
    return one.ToJson();
}

/** @return The entry of a bundled id, or null. */
const EngineEntry *Bundled(const EngineCatalogue &catalogue, const std::string &id)
{
    for (const EngineEntry &entry : catalogue.engines)
    {
        if (entry.bundled_id == id)
        {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

TEST(BundledEngines, AFreshListIsSeededWithEveryEngineMarkedBundledAtItsDefaults)
{
    const auto folder = reckless::test::ScratchPath("uci-host-bundled-fresh") / "engines";
    WriteManifest(folder, "1.0.0");
    const std::vector<BundledEngine> bundled = ReadBundledEngines(folder, kModernCpu);
    ASSERT_EQ(bundled.size(), 3u);

    EngineCatalogue catalogue;
    EXPECT_TRUE(SeedBundledEngines(catalogue, bundled));
    ASSERT_EQ(catalogue.engines.size(), 3u);
    const EngineEntry &pawnstar = catalogue.engines[0];
    EXPECT_TRUE(pawnstar.IsBundled());
    EXPECT_EQ(pawnstar.bundled_id, "pawnstar");
    EXPECT_EQ(pawnstar.bundled_version, "1.0.0");
    EXPECT_EQ(pawnstar.DisplayName(), "Pawnstar");
    EXPECT_EQ(pawnstar.engine_name, "Pawnstar 1.0.0");
    EXPECT_EQ(pawnstar.author, "Jonny Reckless");
    EXPECT_EQ(pawnstar.OptionValue("Threads"), "1");
    EXPECT_EQ(pawnstar.OptionValue("Hash"), "64");
    EXPECT_EQ(pawnstar.declared.size(), 1u);
    EXPECT_EQ(pawnstar.config.executable, Text((folder / "pawnstar" / "pawnstar").lexically_normal()));
    EXPECT_FALSE(SeedBundledEngines(catalogue, bundled)) << "a second start changes nothing";
    EXPECT_EQ(catalogue.engines.size(), 3u);

    // The marks survive the list's file.
    const auto back = EngineCatalogue::FromJson(catalogue.ToJson());
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->ToJson(), catalogue.ToJson());
    EXPECT_EQ(back->engines[1].bundled_id, "zenith");
    EXPECT_TRUE(catalogue.ToJson()["engines"].AsArray()->at(0).BoolOr("bundled", false));
}

TEST(BundledEngines, AnUpgradeMovesTheBundledEnginesAndKeepsThePersonsEditsAndEngines)
{
    const auto root = reckless::test::ScratchPath("uci-host-bundled-upgrade");
    WriteManifest(root / "old" / "engines", "1.0.0");
    EngineCatalogue catalogue;
    catalogue.engines.push_back(OwnEngine());
    ASSERT_TRUE(SeedBundledEngines(catalogue, ReadBundledEngines(root / "old" / "engines", kModernCpu)));
    // The person's edits to a bundled engine: its name, an option changed and one cleared, pondering, a book, a depth.
    EngineEntry &edited = catalogue.engines[1];
    ASSERT_EQ(edited.bundled_id, "pawnstar");
    edited.config.name = "My Pawnstar";
    edited.SetOption("Hash", "256");
    edited.ClearOption("Threads");
    edited.config.does_ponder = true;
    edited.book_path          = "/books/a.bin";
    edited.depth              = 12;
    edited.is_analysing       = false;
    const EngineEntry own     = catalogue.engines[0];

    WriteManifest(root / "new" / "engines", "1.1.0");
    EXPECT_TRUE(SeedBundledEngines(catalogue, ReadBundledEngines(root / "new" / "engines", kModernCpu)));
    ASSERT_EQ(catalogue.engines.size(), 4u) << "no engine added twice";
    EXPECT_EQ(EntryJson(catalogue.engines[0]), EntryJson(own)) << "the person's own engine is untouched";
    const EngineEntry &moved = catalogue.engines[1];
    EXPECT_EQ(moved.config.executable, Text((root / "new" / "engines" / "pawnstar" / "pawnstar").lexically_normal()));
    EXPECT_EQ(moved.bundled_version, "1.1.0");
    EXPECT_EQ(moved.engine_name, "Pawnstar 1.1.0");
    EXPECT_EQ(moved.declared.at(0).maximum, 2048) << "the new release's declared options";
    EXPECT_EQ(moved.config.name, "My Pawnstar");
    EXPECT_EQ(moved.OptionValue("Hash"), "256");
    EXPECT_FALSE(moved.HasOption("Threads")) << "an option the person cleared stays cleared";
    EXPECT_TRUE(moved.config.does_ponder);
    EXPECT_EQ(moved.book_path, "/books/a.bin");
    EXPECT_EQ(moved.depth, 12);
    EXPECT_FALSE(moved.is_analysing);
    EXPECT_EQ(Bundled(catalogue, "zenith")->bundled_version, "1.1.0");
}

TEST(BundledEngines, AnEngineThePersonRemovedStaysRemoved)
{
    const auto folder = reckless::test::ScratchPath("uci-host-bundled-removed") / "engines";
    WriteManifest(folder, "1.0.0");
    const auto      bundled = ReadBundledEngines(folder, kModernCpu);
    EngineCatalogue catalogue;
    SeedBundledEngines(catalogue, bundled);
    catalogue.Remove(1);
    EXPECT_EQ(catalogue.removed_bundled, std::vector<std::string>{"zenith"});
    EXPECT_FALSE(SeedBundledEngines(catalogue, bundled));
    EXPECT_EQ(Bundled(catalogue, "zenith"), nullptr);
    EXPECT_EQ(EngineCatalogue::FromJson(catalogue.ToJson())->removed_bundled, std::vector<std::string>{"zenith"});
    catalogue.Remove(99); // nothing there: nothing removed
    EXPECT_EQ(catalogue.engines.size(), 2u);
}

TEST(BundledEngines, AnEngineTheInstallLacksIsLeftOutAndItsEntryLeftAlone)
{
    const auto root = reckless::test::ScratchPath("uci-host-bundled-missing");
    WriteManifest(root / "a" / "engines", "1.0.0");
    EngineCatalogue catalogue;
    SeedBundledEngines(catalogue, ReadBundledEngines(root / "a" / "engines", kModernCpu));
    const std::string before = Bundled(catalogue, "stoopid-chess")->config.executable;

    WriteManifest(root / "b" / "engines", "1.1.0", {"stoopid-chess"});
    const auto bundled = ReadBundledEngines(root / "b" / "engines", kModernCpu);
    ASSERT_EQ(bundled.size(), 2u) << "an engine whose program is not there is not offered";
    SeedBundledEngines(catalogue, bundled);
    EXPECT_EQ(catalogue.engines.size(), 3u);
    EXPECT_EQ(Bundled(catalogue, "stoopid-chess")->config.executable, before);
    EXPECT_EQ(Bundled(catalogue, "stoopid-chess")->bundled_version, "1.0.0");
    EXPECT_EQ(Bundled(catalogue, "pawnstar")->bundled_version, "1.1.0");
}

TEST(BundledEngines, APersonsOwnEntryForTheBundledProgramIsTakenAsItRatherThanListedTwice)
{
    const auto folder = reckless::test::ScratchPath("uci-host-bundled-adopt") / "engines";
    WriteManifest(folder, "1.0.0");
    EngineCatalogue catalogue;
    EngineEntry     added;
    added.config.executable = Text(folder / "pawnstar" / "pawnstar");
    added.SetOption("Threads", "4");
    catalogue.engines.push_back(added);
    SeedBundledEngines(catalogue, ReadBundledEngines(folder, kModernCpu));
    ASSERT_EQ(catalogue.engines.size(), 3u);
    EXPECT_EQ(catalogue.engines[0].bundled_id, "pawnstar");
    EXPECT_EQ(catalogue.engines[0].OptionValue("Threads"), "4") << "its options are the person's";
}

TEST(BundledEngines, EachMachineRunsTheBuildItsProcessorTakes)
{
    const auto folder = reckless::test::ScratchPath("uci-host-bundled-cpu") / "engines";
    WriteManifest(folder, "1.0.0");
    const auto zenith = [&folder](const CpuLevels &cpu) -> std::string {
        for (const BundledEngine &engine : ReadBundledEngines(folder, cpu))
        {
            if (engine.id == "zenith")
            {
                return engine.executable.parent_path().filename().string();
            }
        }
        return "none";
    };
    EXPECT_EQ(zenith(kModernCpu), "x86-64-v3-pext");
    EXPECT_EQ(zenith(CpuLevels{true, true, false}), "x86-64-v2") << "no fast PEXT (Zen 1 and 2): the portable build";
    EXPECT_EQ(zenith(CpuLevels{false, false, false}), "none") << "no build of it runs";
    const auto older = ReadBundledEngines(folder, CpuLevels{true, false, false});
    ASSERT_EQ(older.size(), 2u) << "an x86-64-v2 machine is offered no Pawnstar, whose build is AVX2 throughout";
    EXPECT_EQ(older[0].id, "zenith");
    std::filesystem::remove(folder / "zenith" / "x86-64-v3-pext" / "zenith");
    EXPECT_EQ(zenith(kModernCpu), "x86-64-v2") << "a build that is not there is passed over";

    EXPECT_TRUE(MeetsRequirement(CpuLevels{}, ""));
    EXPECT_FALSE(MeetsRequirement(kModernCpu, "x86-64-v9"));
    const CpuLevels here = DetectCpuLevels();
    EXPECT_TRUE(!here.is_x86_64_v3_fast_pext || here.is_x86_64_v3) << "fast PEXT implies v3";
    EXPECT_TRUE(!here.is_x86_64_v3 || here.is_x86_64_v2) << "v3 implies v2";
    EXPECT_TRUE(MeetsRequirement(CpuLevels{true, true, false}, "x86-64-v3"));
    EXPECT_FALSE(MeetsRequirement(CpuLevels{true, false, false}, "x86-64-v3"));
#if defined(_M_X64) || defined(__x86_64__)
    EXPECT_TRUE(here.is_x86_64_v2) << "every runner is x86-64-v2 at least";
#endif
}

TEST(BundledEngines, AManifestThatCannotBeReadOffersNothing)
{
    const auto  folder = reckless::test::ScratchPath("uci-host-bundled-bad");
    std::string error;
    EXPECT_TRUE(ReadBundledEngines(folder, kModernCpu, &error).empty());
    EXPECT_NE(error.find("bundled.json"), std::string::npos);
    ASSERT_TRUE(reckless::core::WriteJsonFile(folder / "bundled.json", json::Value(3)));
    EXPECT_TRUE(ReadBundledEngines(folder, kModernCpu, &error).empty());
    EXPECT_NE(error.find("not a manifest"), std::string::npos);
}

TEST(BundledEngines, TheManifestIsFoundWhereTheInstallPutsItAndNowhereElse)
{
    const auto root = reckless::test::ScratchPath("uci-host-bundled-folder");
    EXPECT_TRUE(BundledEnginesFolder({}).empty());
    EXPECT_TRUE(BundledEnginesFolder(root / "bare" / "RecklessUciHost").empty()) << "a build tree's app";
    WriteManifest(root / "beside" / "engines", "1.0.0");
    EXPECT_EQ(BundledEnginesFolder(root / "beside" / "RecklessUciHost"), root / "beside" / "engines");
#if defined(__APPLE__)
    const auto contents = root / "RecklessUciHost.app" / "Contents";
    WriteManifest(contents / "Resources" / "engines", "1.0.0");
    EXPECT_EQ(BundledEnginesFolder(contents / "MacOS" / "RecklessUciHost"), contents / "Resources" / "engines");
#endif
    EXPECT_FALSE(RunningExecutable().empty());
    EXPECT_TRUE(std::filesystem::exists(RunningExecutable()));
}

TEST(BundledEngines, TheControllerSeedsAndSavesTheListAsItStartsAndNeverWritesOverOneItCannotRead)
{
    const auto root = reckless::test::ScratchPath("uci-host-bundled-controller");
    WriteManifest(root / "engines", "1.0.0");
    MainView        view(Pieces());
    ControllerFiles files;
    files.engines         = root / "settings" / "engines.json";
    files.bundled_engines = root / "engines";
    // As many as this machine's processor runs: all three on a modern x86-64, Stoopid Chess alone elsewhere, since the
    // test's manifest gives Pawnstar and Zenith x86-64 builds only.
    const std::size_t runnable = ReadBundledEngines(files.bundled_engines, DetectCpuLevels()).size();
    ASSERT_GE(runnable, 1u);
    {
        MainController controller(view, files);
        EXPECT_EQ(controller.catalogue().engines.size(), runnable);
    }
    const auto saved = EngineCatalogue::Load(files.engines);
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(saved->engines.size(), runnable);

    // A list the person broke: read as nothing, and left as it is.
    {
        std::ofstream(files.engines, std::ios::binary | std::ios::trunc) << "{ not json";
    }
    {
        MainController controller(view, files);
        EXPECT_TRUE(controller.catalogue().engines.empty());
    }
    std::ifstream file(files.engines, std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(file), {}), "{ not json");

    // No bundle: the list as it is.
    ControllerFiles none;
    none.engines = root / "other" / "engines.json";
    {
        MainController controller(view, none);
        EXPECT_TRUE(controller.catalogue().engines.empty());
    }
    EXPECT_FALSE(std::filesystem::exists(none.engines));
}

TEST(BundledEngines, TheManagerShowsABundledEngineAsSuchAndRemovesItForGood)
{
    const auto folder = reckless::test::ScratchPath("uci-host-bundled-manager") / "engines";
    WriteManifest(folder, "1.0.0");
    EngineCatalogue catalogue;
    catalogue.engines.push_back(OwnEngine());
    SeedBundledEngines(catalogue, ReadBundledEngines(folder, kModernCpu));
    EngineManagerDialog dialog{catalogue};
    dialog.SetBounds({0, 0, 1380, 1120});
    dialog.Select(1);
    EXPECT_NE(dialog.about().find("bundled with the host 1.0.0"), std::string::npos) << dialog.about();
    dialog.Select(0);
    EXPECT_EQ(dialog.about().find("bundled"), std::string::npos) << "the person's own is not";
    dialog.Select(1);
    dialog.RemoveSelected();
    EXPECT_EQ(dialog.catalogue().engines.size(), 3u);
    EXPECT_EQ(dialog.catalogue().removed_bundled, std::vector<std::string>{"pawnstar"});
}

} // namespace uci_host::app
