// The engine manager and the engine list, against the fake engine: an engine added by its program and its handshake
// run off the window's thread, every UCI option type edited as its control, the limits and the book, a failed
// handshake named, the list saved as JSON and read back, and the controller writing it where it keeps it.

#include "app/Commands.h"
#include "app/EngineCatalogue.h"
#include "app/EngineManagerDialog.h"
#include "app/Faces.h"
#include "app/MainController.h"
#include "app/MainView.h"
#include "app/PieceSet.h"

#include "ScratchPath.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace uci_host::app
{
namespace
{

using namespace std::chrono_literals;

/** The pieces, read once for every test. */
const PieceSet &Pieces()
{
    static const PieceSet pieces = [] {
        InstallFaces();
        return PieceSet::FromEmbedded();
    }();
    return pieces;
}

/**
 * Frames of the dialog until its handshake has been taken in, or ten seconds.
 * @param dialog The dialog.
 * @return Whether it was.
 */
bool AwaitHandshake(EngineManagerDialog &dialog)
{
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (dialog.IsDetecting() && std::chrono::steady_clock::now() < deadline)
    {
        dialog.Frame();
        std::this_thread::sleep_for(5ms);
    }
    dialog.Frame();
    return !dialog.IsDetecting();
}

/** An option declared as an engine declares it. */
uci::EngineOption Option(const char *line)
{
    return *uci::ParseOptionLine(line);
}

} // namespace

TEST(EngineManager, AnEngineAddedByItsProgramIsNamedAndItsOptionsReadByItsHandshake)
{
    EngineManagerDialog dialog{EngineCatalogue{}};
    dialog.SetBounds({0, 0, 1380, 1120});
    EXPECT_EQ(dialog.selected(), -1);
    dialog.AddEngine(UCI_HOST_FAKE_ENGINE);
    EXPECT_TRUE(dialog.IsDetecting());
    ASSERT_TRUE(AwaitHandshake(dialog));
    ASSERT_EQ(dialog.catalogue().engines.size(), 1u);
    EXPECT_EQ(dialog.selected(), 0);
    const EngineEntry &entry = dialog.catalogue().engines.front();
    EXPECT_FALSE(entry.engine_name.empty());
    EXPECT_FALSE(entry.declared.empty());
    EXPECT_NE(dialog.note().find("options read"), std::string::npos);
    EXPECT_NE(dialog.Row("Hash"), nullptr) << "the fake engine declares Hash";

    dialog.AddEngine("no-such-engine-anywhere");
    ASSERT_TRUE(AwaitHandshake(dialog));
    EXPECT_EQ(dialog.catalogue().engines.size(), 1u) << "a program that does not answer is not added";
    EXPECT_NE(dialog.note().find("did not answer"), std::string::npos);
}

TEST(EngineManager, EveryOptionTypeIsEditedAsItsControlAndSavedWithTheLimitsAndTheBook)
{
    EngineCatalogue catalogue;
    EngineEntry     entry;
    entry.config.executable = "engine";
    entry.engine_name       = "Engine 1.0";
    entry.declared          = {Option("option name Hash type spin default 16 min 1 max 1024"),
                               Option("option name Own Book type check default false"),
                               Option("option name Style type combo default Normal var Solid var Normal var Risky"),
                               Option("option name Clear Hash type button"),
                               Option("option name SyzygyPath type string default <empty>"),
                               Option("option name Ponder type check default false")};
    catalogue.engines.push_back(entry);
    EngineManagerDialog dialog(catalogue);
    dialog.SetBounds({0, 0, 1380, 1120});
    ASSERT_EQ(dialog.selected(), 0);
    EXPECT_EQ(dialog.Row("Ponder"), nullptr) << "pondering is the box above the options";

    OptionRow *hash = dialog.Row("Hash");
    ASSERT_NE(hash, nullptr);
    ASSERT_NE(hash->field(), nullptr);
    EXPECT_EQ(hash->field()->text(), "16");
    hash->field()->SetText("4096");
    hash->field()->on_commit("4096");
    EXPECT_TRUE(hash->note().text().find("1024") != std::string::npos) << "out of range: refused";
    hash->field()->SetText("256");
    hash->field()->on_commit("256");

    ASSERT_NE(dialog.Row("Own Book")->check(), nullptr);
    dialog.Row("Own Book")->check()->Toggle();
    ASSERT_NE(dialog.Row("Style")->combo(), nullptr);
    dialog.Row("Style")->combo()->Choose(2);
    ASSERT_NE(dialog.Row("Clear Hash")->button(), nullptr);
    dialog.Row("Clear Hash")->button()->SetOn(true);
    dialog.Row("Clear Hash")->button()->on_secondary_click = nullptr;
    dialog.Row("SyzygyPath")->field()->SetText("C:/tb");

    dialog.name_field().SetText("My Engine");
    dialog.depth_field().SetText("12");
    dialog.book_depth_field().SetText("8");
    dialog.ponder().Toggle();
    dialog.analyse().Toggle();
    dialog.SetBook("book.bin");

    EngineCatalogue saved;
    dialog.on_save = [&](const EngineCatalogue &edited) { saved = edited; };
    dialog.Save();
    ASSERT_EQ(saved.engines.size(), 1u);
    const EngineEntry &out = saved.engines.front();
    EXPECT_EQ(out.config.name, "My Engine");
    EXPECT_EQ(out.OptionValue("Hash"), "256");
    EXPECT_EQ(out.OptionValue("Own Book"), "true");
    EXPECT_EQ(out.OptionValue("Style"), "Risky");
    EXPECT_EQ(out.OptionValue("SyzygyPath"), "C:/tb");
    EXPECT_EQ(out.depth, std::optional<int>(12));
    EXPECT_EQ(out.book_depth, 8);
    EXPECT_EQ(out.book_path, "book.bin");
    EXPECT_TRUE(out.config.does_ponder);
    EXPECT_FALSE(out.is_analysing);
    EXPECT_EQ(out.Summary(), "Hash 256 MB, depth 12, ponder, book");

    dialog.Row("Own Book")->check()->Toggle();
    dialog.Save();
    EXPECT_FALSE(saved.engines.front().HasOption("Own Book")) << "back at its default: no longer configured";
}

TEST(EngineManager, TheControllerWritesTheListWhereItKeepsIt)
{
    const auto     scratch = reckless::test::ScratchPath("uci-host-manager");
    const auto     file    = scratch / "engines.json";
    MainView       view(Pieces());
    MainController controller(view, ControllerFiles{file});
    controller.Command(kEngineManager);
    auto *dialog = dynamic_cast<EngineManagerDialog *>(controller.dialog());
    ASSERT_NE(dialog, nullptr);
    dialog->AddEngine(UCI_HOST_FAKE_ENGINE);
    ASSERT_TRUE(AwaitHandshake(*dialog));
    dialog->Save();
    controller.Frame(0.0);
    EXPECT_EQ(controller.dialog(), nullptr);
    ASSERT_EQ(controller.catalogue().engines.size(), 1u);
    const auto loaded = EngineCatalogue::Load(file);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->engines.size(), 1u);
    EXPECT_TRUE(controller.Menus().has_engines);
}

} // namespace uci_host::app
