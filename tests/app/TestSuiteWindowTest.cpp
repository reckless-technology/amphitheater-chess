// Engines > Test suite: a suite's file opened in its dialog, its positions listed with what each asks for, a run
// against the fake engine filling in each one's move and result and the score under the list, a position shown on the
// board, and what the dialog refuses.

#include "app/Commands.h"
#include "app/Controls.h"
#include "app/Faces.h"
#include "app/MainController.h"
#include "app/MainView.h"
#include "app/PieceSet.h"
#include "app/TestSuiteDialog.h"
#include "app/Theme.h"

#include "ScratchPath.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace amphitheater::app
{
namespace
{

/** The pieces, read once for every test. */
const PieceSet &Pieces()
{
    static const PieceSet pieces = [] {
        InstallFaces();
        return PieceSet::FromEmbedded();
    }();
    return pieces;
}

/** White's queen takes Black's, which the fake engine always plays: it takes the most valuable piece it can. */
constexpr const char *kQueenTrade = "3q3k/8/8/8/8/8/8/3Q3K w - -";

/** A window with its controller and the fake engine configured, and a suite of three positions on file. */
struct Rig
{
    MainView              view{Pieces()};
    MainController        controller{view, ControllerFiles::None()};
    std::filesystem::path suite;

    Rig()
    {
        view.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});
        EngineEntry fake;
        fake.config.executable = AMPHITHEATER_FAKE_ENGINE;
        fake.config.name       = "Fake";
        controller.catalogue().engines.push_back(fake);
        const auto folder = reckless::test::ScratchPath("amphitheater-suite");
        std::filesystem::create_directories(folder);
        suite = folder / "suite.epd";
        std::ofstream(suite, std::ios::trunc) << kQueenTrade << " bm Qxd8+; id \"trade\";\n"
                                              << kQueenTrade << " am Qxd8+; id \"no trade\";\n"
                                              << "not a position\n";
    }

    /** @return The suite's dialog, open. */
    TestSuiteDialog *Dialog()
    {
        return dynamic_cast<TestSuiteDialog *>(controller.dialog());
    }
};

} // namespace

TEST(TestSuiteWindow, ASuitesPositionsAreListedWithWhatEachAsksFor)
{
    Rig rig;
    ASSERT_TRUE(rig.controller.OpenTestSuite(rig.suite.string()));
    TestSuiteDialog *dialog = rig.Dialog();
    ASSERT_NE(dialog, nullptr);
    const TestSuiteModel &model = dialog->model();
    ASSERT_EQ(model.RowCount(), 2u);
    EXPECT_EQ(model.CellText(0, 1), "trade");
    EXPECT_EQ(model.CellText(0, 2), "bm Qxd8+");
    EXPECT_EQ(model.CellText(1, 2), "am Qxd8+");
    EXPECT_EQ(model.CellText(0, 4), "") << "not run yet";
    EXPECT_NE(dialog->summary().find("2 positions, 1 lines skipped"), std::string::npos) << dialog->summary();

    EXPECT_FALSE(rig.controller.OpenTestSuite((rig.suite.parent_path() / "missing.epd").string()));
    EXPECT_NE(rig.controller.last_error().find("could not be read"), std::string::npos);
}

TEST(TestSuiteWindow, ARunFillsInEachMoveAndResultAndTheScore)
{
    Rig rig;
    ASSERT_TRUE(rig.controller.OpenTestSuite(rig.suite.string()));
    TestSuiteDialog &dialog = *rig.Dialog();
    dialog.seconds().SetText("0.05");
    ASSERT_TRUE(dialog.Run());
    EXPECT_TRUE(dialog.IsRunning());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (dialog.IsRunning() && std::chrono::steady_clock::now() < deadline)
    {
        rig.controller.Frame(0.016);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_FALSE(dialog.IsRunning());
    const TestSuiteModel &model = dialog.model();
    EXPECT_EQ(model.CellText(0, 3), "Qxd8+");
    EXPECT_EQ(model.CellText(0, 4), "solved");
    EXPECT_EQ(model.CellText(1, 4), "failed");
    EXPECT_EQ(model.CellColour(0, 4), theme::kPlus);
    EXPECT_EQ(model.CellColour(1, 4), theme::kMinus);
    EXPECT_EQ(dialog.summary(), "Fake: solved 1 of 2 (50.0 %)");
}

TEST(TestSuiteWindow, APositionPressedTwiceIsShownOnTheBoard)
{
    Rig rig;
    ASSERT_TRUE(rig.controller.OpenTestSuite(rig.suite.string()));
    rig.Dialog()->list().on_double_click(1, 1);
    rig.controller.Frame(0.0);
    EXPECT_EQ(rig.controller.dialog(), nullptr) << "closed to show it";
    EXPECT_EQ(rig.controller.game().StartPosition().ToFen(), "3q3k/8/8/8/8/8/8/3Q3K w - - 0 1");
}

TEST(TestSuiteWindow, TheDialogRefusesATimeOrADepthItCannotUse)
{
    Rig rig;
    ASSERT_TRUE(rig.controller.OpenTestSuite(rig.suite.string()));
    TestSuiteDialog &dialog = *rig.Dialog();
    dialog.seconds().SetText("soon");
    EXPECT_FALSE(dialog.Run());
    EXPECT_NE(dialog.summary().find("'soon'"), std::string::npos);
    dialog.depth().SetText("2.5");
    EXPECT_FALSE(dialog.Run());
    EXPECT_NE(dialog.summary().find("'2.5'"), std::string::npos);

    EXPECT_EQ(ParseDecimal("0.5"), std::optional<double>(0.5));
    EXPECT_EQ(ParseDecimal("12"), std::optional<double>(12.0));
    EXPECT_FALSE(ParseDecimal(" 1"));
    EXPECT_FALSE(ParseDecimal("1 "));
    EXPECT_FALSE(ParseDecimal(""));
    EXPECT_FALSE(ParseDecimal("inf"));
}

TEST(TestSuiteWindow, TheEnginesMenuOffersATestSuiteOnceAnEngineIsConfigured)
{
    MainView       view(Pieces());
    MainController controller(view, ControllerFiles::None());
    const auto     item = [&controller]() -> const reckless::app::MenuEntry     *{
        for (const auto &menu : controller.menu_bar().menus)
        {
            for (const auto &entry : menu.submenu)
            {
                if (entry.id == kTestSuite)
                {
                    return &entry;
                }
            }
        }
        return nullptr;
    };
    ASSERT_NE(item(), nullptr);
    EXPECT_FALSE(item()->enabled);
    EngineEntry fake;
    fake.config.executable = AMPHITHEATER_FAKE_ENGINE;
    controller.catalogue().engines.push_back(fake);
    controller.Frame(0.0);
    EXPECT_TRUE(item()->enabled);
}

} // namespace amphitheater::app
