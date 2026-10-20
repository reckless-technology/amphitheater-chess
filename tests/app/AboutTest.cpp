// Help > About: chosen from the menu bar it opens over the window with the app's name and version, closes on Escape,
// Return and its button, opens the website through the platform, and lists the engines the install carries - every one
// whose program is there, whatever the processor, with its version and licence - or none for a build tree's host.

#include "app/AboutDialog.h"
#include "app/Commands.h"
#include "app/Faces.h"
#include "app/MainController.h"
#include "app/MainView.h"
#include "app/PieceSet.h"
#include "version/Version.h"

#include "JsonFile.h"
#include "ScratchPath.h"
#include "SkiaCanvas.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace amphitheater::app
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

/**
 * Help > About chosen through the controller's own menu bar, as a click on it chooses it.
 * @param controller The controller.
 * @return Whether the bar held the item and chose it.
 */
bool ChooseAbout(MainController &controller)
{
    reckless::app::MenuBarController *menus = controller.own_menus();
    if (menus == nullptr)
    {
        return false;
    }
    const reckless::app::MenuEntry &help = menus->bar().menus.back();
    for (const reckless::app::MenuEntry &entry : help.submenu)
    {
        if (entry.id == kAbout)
        {
            return menus->Choose(menus->CommandOf(entry));
        }
    }
    return false;
}

/**
 * A manifest as tools/bundled_engines.py writes it, with a file for each build but those named missing.
 * @param folder The engines' folder.
 */
void WriteManifest(const std::filesystem::path &folder)
{
    struct Spec
    {
        const char *id;
        const char *name;
        const char *version;
        const char *licence;
        const char *requires_level;
        bool        is_there;
    };

    const Spec  specs[] = {{"pawnstar", "Pawnstar", "1.2.0", "GPL-3.0", "x86-64-v3-fast-pext", true},
                           {"zenith", "Zenith", "2.1.0", "GPL-3.0-or-later", "", true},
                           {"gone", "Gone", "0.1.0", "GPL-3.0", "", false}};
    json::Value engines = json::Value::MakeArray();
    for (const Spec &spec : specs)
    {
        json::Value engine = json::Value::MakeObject();
        engine.Set("id", spec.id);
        engine.Set("name", spec.name);
        engine.Set("version", spec.version);
        engine.Set("licence", spec.licence);
        engine.Set("options", json::Value::MakeArray());
        engine.Set("declared", json::Value::MakeArray());
        json::Value builds = json::Value::MakeArray();
        json::Value build  = json::Value::MakeObject();
        build.Set("executable", std::string(spec.id) + "/" + spec.id);
        build.Set("requires", spec.requires_level);
        builds.Append(std::move(build));
        engine.Set("builds", std::move(builds));
        engines.Append(std::move(engine));
        if (spec.is_there)
        {
            std::filesystem::create_directories(folder / spec.id);
            std::ofstream(folder / spec.id / spec.id, std::ios::binary) << "an engine";
        }
    }
    json::Value manifest = json::Value::MakeObject();
    manifest.Set("format", 1);
    manifest.Set("engines", std::move(engines));
    ASSERT_TRUE(reckless::core::WriteJsonFile(folder / "bundled.json", manifest));
}

} // namespace

TEST(About, HelpAboutOpensOverTheWindowWithTheNameAndVersionAndClosesOnEscape)
{
    MainView                 view(Pieces());
    std::vector<std::string> opened;
    ControllerServices       services;
    services.open_url = [&](const std::string &url) { opened.push_back(url); };
    MainController controller(view, ControllerFiles{{}}, services);
    view.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});

    ASSERT_TRUE(ChooseAbout(controller)) << "Help holds About and the bar chooses it";
    auto *about = dynamic_cast<AboutDialog *>(controller.dialog());
    ASSERT_NE(about, nullptr);
    EXPECT_EQ(view.overlay(), about);
    EXPECT_EQ(about->info().version, VersionText());
    const std::vector<std::string> lines = about->Lines();
    ASSERT_GE(lines.size(), 2u);
    EXPECT_EQ(lines[0], "Reckless Amphitheater");
    EXPECT_EQ(lines[1], "Version " + VersionText());
    const auto has = [&lines](const std::string &part) {
        return std::any_of(lines.begin(), lines.end(),
                           [&part](const std::string &line) { return line.find(part) != std::string::npos; });
    };
    EXPECT_TRUE(has("Copyright 2026 Jonny Reckless. Reckless Technology."));
    EXPECT_TRUE(has("EULA.txt"));
    EXPECT_TRUE(has("THIRD-PARTY-NOTICES.txt"));
    EXPECT_TRUE(has("recklesstechnology.com"));
    EXPECT_TRUE(about->EngineLines().empty()) << "a build tree's host carries no engines";
    EXPECT_FALSE(has("Bundled engines"));
    controller.Frame(0.0);
    EXPECT_FALSE(controller.menu_bar().menus.back().enabled) << "the menus wait while it is open";

    // The website: a click on the link opens it in the person's browser.
    EXPECT_TRUE(about->CardBounds().Contains(about->link().bounds()));
    reckless::view::MouseEvent click;
    click.position = {4.0f, 4.0f};
    click.down     = click.position;
    about->link().MouseUp(click);
    EXPECT_EQ(opened, std::vector<std::string>{AboutDialog::kWebsite});

    // The whole card drawn through Skia: nothing it holds is cut off the card.
    reckless::gfx::Image image = reckless::gfx::MakeImage(MainView::kDesignWidth, MainView::kDesignHeight);
    {
        const auto canvas = reckless::gfx::MakeImageCanvas(image, 1.0f);
        ASSERT_NE(canvas, nullptr);
        reckless::view::PaintTree(view, *canvas);
    }

    EXPECT_TRUE(about->KeyPressed({reckless::view::Key::kEscape, 0, {}}));
    EXPECT_EQ(controller.dialog(), nullptr);
    EXPECT_EQ(view.overlay(), nullptr);
    controller.Frame(0.0);
    EXPECT_TRUE(controller.menu_bar().menus.back().enabled);

    // Return and the Close button close it too.
    controller.Command(kAbout);
    ASSERT_NE(controller.dialog(), nullptr);
    EXPECT_TRUE(controller.dialog()->KeyPressed({reckless::view::Key::kReturn, 0, {}}));
    EXPECT_EQ(controller.dialog(), nullptr);
    controller.Command(kAbout);
    ASSERT_NE(controller.dialog(), nullptr);
    controller.dialog()->on_cancel();
    EXPECT_EQ(controller.dialog(), nullptr);
}

TEST(About, TheEnginesTheInstallCarriesAreListedWithTheirVersionsAndLicences)
{
    const std::filesystem::path folder = reckless::test::ScratchPath("amphitheater-about") / "engines";
    WriteManifest(folder);
    MainView        view(Pieces());
    ControllerFiles files{{}};
    files.bundled_engines = folder;
    MainController controller(view, files);
    view.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});

    controller.Command(kAbout);
    auto *about = dynamic_cast<AboutDialog *>(controller.dialog());
    ASSERT_NE(about, nullptr);
    EXPECT_EQ(about->EngineLines(),
              (std::vector<std::string>{"Pawnstar 1.2.0 (GPLv3)", "Zenith 2.1.0 (GPLv3 or later)"}))
        << "every engine whose program is there, whatever this processor runs; the missing one left out";
    const std::vector<std::string> lines = about->Lines();
    EXPECT_NE(std::find(lines.begin(), lines.end(), "Bundled engines"), lines.end());
    EXPECT_NE(std::find(lines.begin(), lines.end(), "Pawnstar 1.2.0 (GPLv3)"), lines.end());

    // The card grows with the list, and the link stays on it.
    AboutDialog bare(AboutInfo{VersionText(), {}});
    bare.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});
    EXPECT_GT(about->CardBounds().height(), bare.CardBounds().height());
    EXPECT_TRUE(about->CardBounds().Contains(about->link().bounds()));
}

TEST(About, ALicenceIsNamedForTheGplsVersionOrShownAsItIs)
{
    EXPECT_EQ(LicenceName("GPL-3.0"), "GPLv3");
    EXPECT_EQ(LicenceName("GPL-3.0-only"), "GPLv3");
    EXPECT_EQ(LicenceName("GPL-3.0-or-later"), "GPLv3 or later");
    EXPECT_EQ(LicenceName("MIT"), "MIT");
    EXPECT_EQ(LicenceName(""), "");
    AboutDialog about(AboutInfo{"1.2.3", {{"Toy", "0.1.0", ""}}});
    EXPECT_EQ(about.EngineLines(), std::vector<std::string>{"Toy 0.1.0"}) << "no licence named, none shown";
}

TEST(About, ItsMarkIsTheAppsIconCarriedInTheBinary)
{
    const AboutDialog about(AboutInfo{"1.2.3", {}});
    EXPECT_TRUE(about.has_mark());
}

TEST(About, TheMacAppMenusAboutItemOpensThisWindow)
{
    MainView       view(Pieces());
    MainController controller(view, ControllerFiles{{}});
    view.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});
    EXPECT_EQ(controller.menu_bar().about_id, kAbout) << "the bar names Help > About for the app menu's item";
    reckless::app::MenuBarController *menus = controller.own_menus();
    ASSERT_NE(menus, nullptr);
    const int command = menus->CommandOfId(controller.menu_bar().about_id);
    ASSERT_NE(command, 0);
    ASSERT_TRUE(menus->Choose(command)) << "as the Mac platform chooses it";
    EXPECT_NE(dynamic_cast<AboutDialog *>(controller.dialog()), nullptr);
}

} // namespace amphitheater::app
