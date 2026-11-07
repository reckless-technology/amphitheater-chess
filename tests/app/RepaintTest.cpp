// What the window asks to have painted, and when. A window with nothing changing asks for nothing, so it costs no
// paints; and whatever does change on the screen is asked for, which RepaintWatch holds by drawing the whole window
// after each step and finding every pixel that changed inside what was asked for since the step before.

#include "app/AnalysisPanel.h"
#include "app/BoardView.h"
#include "app/Commands.h"
#include "app/Controls.h"
#include "app/Faces.h"
#include "app/MainController.h"
#include "app/MainView.h"
#include "app/Panels.h"
#include "app/PieceSet.h"
#include "app/TestSuiteDialog.h"

#include "ScratchPath.h"
#include "SkiaCanvas.h"
#include "View.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace amphitheater::app
{
namespace
{

using reckless::gfx::Color;
using reckless::gfx::PointF;
using reckless::gfx::RectI;
using reckless::view::Key;
using reckless::view::Modifiers;
using reckless::view::MouseRouter;
using reckless::view::View;

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
 * @param view The root to draw.
 * @return The whole of it drawn afresh, unclipped, so two pictures of the same state are the same to the pixel. (A
 * window's own paint is clipped to what was asked for, and Skia's antialiasing of a path a clip cuts through differs by
 * as much as 54 levels at its edge, which says nothing about whether anything changed.)
 */
SkBitmap PictureOf(View &view)
{
    reckless::gfx::Image image  = reckless::gfx::MakeImage(view.width(), view.height());
    const auto           canvas = reckless::gfx::MakeImageCanvas(image, 1.0f);
    reckless::view::PaintTree(view, *canvas);
    auto    &skia = dynamic_cast<reckless::gfx::SkiaCanvas &>(*canvas).skia();
    SkBitmap bitmap;
    bitmap.allocPixels(skia.imageInfo());
    skia.readPixels(bitmap, 0, 0);
    return bitmap;
}

/** The host of a root with no window: it counts what is asked to be painted, and sees what changed without asking. */
class RepaintWatch final : public reckless::view::Host
{
  public:
    /** @param root The root to watch, at its size for the whole watch; outlives this. */
    explicit RepaintWatch(View &root) : root_(root)
    {
        root_.SetHost(this);
        shown_ = PictureOf(root_);
    }

    ~RepaintWatch() override
    {
        root_.SetHost(nullptr);
    }

    void Invalidate(RectI root_area) override
    {
        ++asked_;
        dirty_ = dirty_.Union(root_area);
        areas_.push_back(root_area);
    }

    /** @return How many times a repaint has been asked for since the last Forget. */
    int asked() const
    {
        return asked_;
    }

    /**
     * @param area An area of the root.
     * @return Whether a repaint has touched it since the last Forget.
     */
    bool Touched(RectI area) const
    {
        for (const RectI &asked_area : areas_)
        {
            if (asked_area.Intersects(area))
            {
                return true;
            }
        }
        return false;
    }

    /** The counts started again; what is waiting to be painted stays waiting. */
    void Forget()
    {
        asked_ = 0;
        areas_.clear();
    }

    /**
     * What a window paints is the smallest rectangle round what was asked for (its dirty area), so that is all that may
     * have changed since the last picture.
     * @return Whether every pixel that changed lies in it: failure, saying where, for something changed and never asked
     * for.
     */
    ::testing::AssertionResult AskedForAllThatChanged()
    {
        const SkBitmap now   = PictureOf(root_);
        long           stale = 0;
        RectI          where;
        for (int y = 0; y < now.height(); ++y)
        {
            for (int x = 0; x < now.width(); ++x)
            {
                if (now.getColor(x, y) != shown_.getColor(x, y) && !dirty_.Contains(reckless::gfx::PointI{x, y}))
                {
                    ++stale;
                    where = where.Union({x, y, 1, 1});
                }
            }
        }
        shown_ = now;
        dirty_ = {};
        if (stale > 0)
        {
            return ::testing::AssertionFailure() << stale << " pixels changed unasked, in (" << where.x() << ", "
                                                 << where.y() << ") " << where.width() << " x " << where.height();
        }
        return ::testing::AssertionSuccess();
    }

  private:
    View              &root_;     ///< The root watched.
    SkBitmap           shown_;    ///< The picture at the last check.
    RectI              dirty_;    ///< What was asked for since the last check.
    std::vector<RectI> areas_;    ///< What was asked for since the last Forget.
    int                asked_{0}; ///< How many asks since the last Forget.
};

/** A root that fills itself as the window does, so a view under test is painted over a clean background. */
class Backing final : public View
{
  public:
    void Paint(reckless::gfx::Canvas &c) override
    {
        c.FillAll(Color::FromRgb(40, 40, 48));
    }
};

/** A view that changes what it draws, asking for the paint or not. */
class Swatch final : public View
{
  public:
    /**
     * @param colour Its colour.
     * @param does_ask Whether to ask for the paint.
     */
    void Set(Color colour, bool does_ask)
    {
        colour_ = colour;
        if (does_ask)
        {
            Repaint();
        }
    }

    void Paint(reckless::gfx::Canvas &c) override
    {
        c.SetColor(colour_);
        c.FillRect(LocalBounds().ToFloat());
    }

  private:
    Color colour_{Color::FromRgb(10, 20, 30)}; ///< What it draws.
};

/** The fake engine as a configured engine. */
EngineEntry Fake(std::vector<std::string> arguments, std::string name)
{
    EngineEntry entry;
    entry.config.executable = AMPHITHEATER_FAKE_ENGINE;
    entry.config.arguments  = std::move(arguments);
    entry.config.name       = std::move(name);
    return entry;
}

/** A window at its design size under a watch, and the way a person's pointer and keys reach it. */
struct Rig
{
    MainView                    view{Pieces()};
    MainController              controller{view, ControllerFiles::None()};
    std::optional<RepaintWatch> watch;
    std::optional<MouseRouter>  router;

    Rig()
    {
        view.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});
        watch.emplace(view);
        router.emplace(view, &*watch);
        controller.Frame(1.0 / 60.0); // the first frame's own settling
        EXPECT_TRUE(watch->AskedForAllThatChanged());
        watch->Forget();
    }

    /** Frames until a condition holds, or thirty seconds, the screen held to what the window holds at each. */
    bool FramesUntil(const std::function<bool()> &condition)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (std::chrono::steady_clock::now() < deadline)
        {
            controller.Frame(1.0 / 60.0);
            EXPECT_TRUE(watch->AskedForAllThatChanged());
            if (condition())
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    /**
     * @param view_in A view in the window.
     * @return Its area, in the root's coordinates.
     */
    static RectI AreaOf(const View &view_in)
    {
        const reckless::gfx::PointI origin = view_in.ToRoot(reckless::gfx::PointI{0, 0});
        return {origin.x, origin.y, view_in.width(), view_in.height()};
    }

    /** @return The middle of a view, in the root's coordinates. */
    static PointF MiddleOf(const View &view_in)
    {
        return AreaOf(view_in).ToFloat().Centre();
    }
};

/** @return A block with one line, which each change below departs from. */
EngineBlock SomeBlock()
{
    EngineBlock block;
    block.name        = "Fake";
    block.state       = "thinking 1.0 s";
    block.is_thinking = true;
    block.stats       = "depth 10/12";
    AnalysisLine line;
    line.depth = "10/12";
    line.score = "+0.10";
    line.sign  = 1;
    line.pv    = "1.e4 e5";
    block.lines.push_back(line);
    return block;
}

} // namespace

TEST(Repaint, TheWatchCatchesAChangeNoRepaintWasAskedFor)
{
    Backing root;
    root.SetBounds({200, 100});
    Swatch swatch;
    swatch.SetBounds({20, 20, 60, 60});
    root.AddChild(swatch);
    RepaintWatch watch(root);
    EXPECT_TRUE(watch.AskedForAllThatChanged());

    swatch.Set(Color::FromRgb(200, 30, 30), true);
    EXPECT_EQ(watch.asked(), 1);
    EXPECT_TRUE(watch.AskedForAllThatChanged()) << "asked for, and painted";

    watch.Forget();
    swatch.Set(Color::FromRgb(30, 200, 30), false);
    EXPECT_EQ(watch.asked(), 0);
    EXPECT_FALSE(watch.AskedForAllThatChanged()) << "changed and never asked for: the watch must be able to say so";
}

TEST(Repaint, TheAnalysisPanelAsksOnlyWhenABlockDiffers)
{
    Backing root;
    root.SetBounds({600, 300});
    AnalysisPanel panel;
    panel.SetBounds({600, 300});
    root.AddChild(panel);
    RepaintWatch watch(root);
    panel.SetBlocks({SomeBlock()});
    EXPECT_EQ(watch.asked(), 1) << "the first blocks";

    watch.Forget();
    panel.SetBlocks({SomeBlock()});
    EXPECT_EQ(watch.asked(), 0) << "the same blocks, as the controller sets them each frame";
    EXPECT_TRUE(watch.AskedForAllThatChanged());

    const std::vector<std::function<void(EngineBlock &)>> changes = {
        [](EngineBlock &block) { block.name = "Other"; },
        [](EngineBlock &block) { block.state = "thinking 1.1 s"; },
        [](EngineBlock &block) { block.is_thinking = false; },
        [](EngineBlock &block) { block.stats = "depth 11/12"; },
        [](EngineBlock &block) { block.lines.front().depth = "11/12"; },
        [](EngineBlock &block) { block.lines.front().score = "+0.11"; },
        [](EngineBlock &block) { block.lines.front().sign = -1; },
        [](EngineBlock &block) { block.lines.front().pv = "1.d4 d5"; },
        [](EngineBlock &block) { block.lines.front().is_earlier = true; },
        [](EngineBlock &block) { block.lines.push_back(block.lines.front()); },
        [](EngineBlock &block) { block.lines.clear(); },
    };
    for (std::size_t index = 0; index < changes.size(); ++index)
    {
        EngineBlock changed = SomeBlock();
        changes[index](changed);
        watch.Forget();
        panel.SetBlocks({changed});
        EXPECT_EQ(watch.asked(), 1) << "change " << index;
        EXPECT_TRUE(watch.AskedForAllThatChanged()) << "change " << index;
        panel.SetBlocks({SomeBlock()});
        EXPECT_TRUE(watch.AskedForAllThatChanged()) << "change " << index << " taken back";
    }

    watch.Forget();
    panel.SetBlocks({SomeBlock(), SomeBlock()});
    EXPECT_EQ(watch.asked(), 1) << "a second engine";
    EXPECT_EQ(panel.blocks().size(), 2u);
    watch.Forget();
    panel.SetBlocks({});
    EXPECT_EQ(watch.asked(), 1) << "the engines gone";
    EXPECT_TRUE(watch.AskedForAllThatChanged());
}

TEST(Repaint, TheBoardAsksOnlyWhenAnArrowDiffers)
{
    Backing root;
    root.SetBounds({600, 600});
    BoardView board(Pieces());
    board.SetBounds({0, 0, 600, 600});
    root.AddChild(board);
    RepaintWatch     watch(root);
    const BoardArrow arrow{chess::Square("e2"), chess::Square("e4"), Color::FromRgb(200, 120, 30)};
    board.SetArrows({arrow});
    EXPECT_EQ(watch.asked(), 1) << "the first arrow";

    watch.Forget();
    board.SetArrows({arrow});
    EXPECT_EQ(watch.asked(), 0) << "the same arrow, as the controller sets it each frame";
    EXPECT_TRUE(watch.AskedForAllThatChanged());

    const std::vector<BoardArrow> others = {
        {chess::Square("d2"), chess::Square("e4"), arrow.colour},
        {chess::Square("e2"), chess::Square("e3"), arrow.colour},
        {chess::Square("e2"), chess::Square("e4"), arrow.colour.WithAlpha(0.5f)},
    };
    for (std::size_t index = 0; index < others.size(); ++index)
    {
        watch.Forget();
        board.SetArrows({others[index]});
        EXPECT_EQ(watch.asked(), 1) << "other arrow " << index;
        EXPECT_TRUE(watch.AskedForAllThatChanged()) << "other arrow " << index;
    }
    watch.Forget();
    board.SetArrows({});
    EXPECT_EQ(watch.asked(), 1) << "the arrows gone";
    EXPECT_TRUE(watch.AskedForAllThatChanged());
    watch.Forget();
    board.SetArrows({});
    EXPECT_EQ(watch.asked(), 0) << "still none";
}

TEST(Repaint, AnIdleWindowAsksForNothing)
{
    Rig rig;
    for (int frame = 0; frame < 300; ++frame)
    {
        rig.controller.Frame(1.0 / 60.0);
    }
    EXPECT_EQ(rig.watch->asked(), 0) << "a window with nothing on it";

    chess::Game game;
    game.PlaySan("e4");
    game.PlaySan("e5");
    rig.controller.ShowGame(game, {});
    rig.controller.Command(kUseBuiltInBook); // arrows on the board, set every frame
    rig.controller.ShowPly(0);
    rig.controller.Frame(1.0 / 60.0);
    EXPECT_TRUE(rig.watch->AskedForAllThatChanged());
    EXPECT_FALSE(rig.view.board().arrows().empty()) << "the book's arrows";
    rig.watch->Forget();
    for (int frame = 0; frame < 300; ++frame)
    {
        rig.controller.Frame(1.0 / 60.0);
    }
    EXPECT_EQ(rig.watch->asked(), 0) << "a game shown, a book's arrows on it";

    rig.controller.Command(kAbout);
    EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << "a dialog opened";
    rig.controller.Frame(1.0 / 60.0); // the bar set again, with its menus waiting
    EXPECT_TRUE(rig.watch->AskedForAllThatChanged());
    rig.watch->Forget();
    for (int frame = 0; frame < 60; ++frame)
    {
        rig.controller.Frame(1.0 / 60.0);
    }
    EXPECT_EQ(rig.watch->asked(), 0) << "a dialog open";
}

TEST(Repaint, AStepThroughAGameAFlipAndThePiecesAreAllShown)
{
    Rig         rig;
    chess::Game game;
    game.PlaySan("e4");
    game.PlaySan("e5");
    rig.controller.ShowGame(game, {});
    EXPECT_TRUE(rig.watch->AskedForAllThatChanged());
    for (const int command :
         std::vector<int>{kFirst, kForward, kForward, kBack, kLast, kFlip, kFlip, kPiecesBase + 1, kPiecesBase})
    {
        rig.watch->Forget();
        rig.controller.Command(command);
        rig.controller.Frame(1.0 / 60.0);
        EXPECT_GT(rig.watch->asked(), 0) << command;
        EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << command;
    }
}

TEST(Repaint, AnEnginesOutputIsShownAndTheBoardIsLeftAloneOnceItsArrowIsThere)
{
    Rig rig;
    rig.controller.catalogue().engines = {Fake({"--score", "35"}, "Fake")};
    chess::Game game;
    game.PlaySan("e4");
    game.PlaySan("e5");
    rig.controller.ShowGame(game, {});
    rig.controller.Command(kAnalyse);
    const auto &blocks = rig.view.analysis().blocks();
    ASSERT_TRUE(rig.FramesUntil([&] { return !blocks.empty() && !blocks.front().lines.empty(); }));
    ASSERT_TRUE(rig.FramesUntil([&] { return !rig.view.board().arrows().empty(); }));

    rig.watch->Forget();
    const std::string state = blocks.front().state;
    ASSERT_TRUE(rig.FramesUntil([&] { return blocks.front().state != state; })) << "its running time moves on";
    EXPECT_TRUE(rig.watch->Touched(Rig::AreaOf(rig.view.analysis())));
    EXPECT_FALSE(rig.watch->Touched(Rig::AreaOf(rig.view.board()))) << "nothing on the board changed";

    rig.watch->Forget();
    rig.controller.ShowPly(1);
    rig.controller.Frame(1.0 / 60.0);
    EXPECT_TRUE(rig.watch->Touched(Rig::AreaOf(rig.view.board()))) << "another position";
    EXPECT_TRUE(rig.watch->AskedForAllThatChanged());

    rig.watch->Forget();
    rig.controller.Command(kAnalyse);
    ASSERT_TRUE(rig.FramesUntil([&] { return blocks.empty(); }));
    EXPECT_TRUE(rig.watch->Touched(Rig::AreaOf(rig.view.analysis()))) << "the engines gone";
}

TEST(Repaint, AGamesClocksAndAnEnginesMovesAreShownAndItsPersonsPointerToo)
{
    Rig          rig;
    PlaySettings settings;
    settings.white.is_human = true;
    settings.black.is_human = false;
    settings.black.engine   = Fake({"--name", "Fake", "--think", "20"}, "Fake");
    settings.time_control   = *chess::TimeControl::Parse("60+1");
    rig.controller.StartGame(settings);
    const auto person_to_move = [&] { return rig.controller.IsPlaying() && rig.view.board().interactive(); };
    ASSERT_TRUE(rig.FramesUntil(person_to_move));

    // A running clock: its text moves on, and the strip it is on is what asks.
    const PlayerStrip &strip = rig.view.bottom_strip();
    rig.watch->Forget();
    const std::string clock = strip.state().clock;
    ASSERT_TRUE(rig.FramesUntil([&] { return strip.state().clock != clock; }));
    EXPECT_TRUE(rig.watch->Touched(Rig::AreaOf(strip)));
    EXPECT_FALSE(rig.watch->Touched(Rig::AreaOf(rig.view.board()))) << "nothing on the board changed";

    // A person's pointer: the press selects, the drag follows, the drop makes the move.
    BoardView   &board  = rig.view.board();
    const PointF origin = Rig::AreaOf(board).ToFloat().TopLeft();
    const PointF from   = origin + board.SquareBounds(chess::Square("e2")).Centre();
    const PointF to     = origin + board.SquareBounds(chess::Square("e4")).Centre();
    rig.router->Move(from, Modifiers{});
    EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << "hover";
    rig.router->Down(from, 1, false, Modifiers{});
    EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << "press";
    rig.router->Drag(PointF{(from.x + to.x) / 2.0f, (from.y + to.y) / 2.0f}, Modifiers{});
    EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << "drag";
    rig.router->Drag(to, Modifiers{});
    EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << "drag on";
    rig.router->Up(to, Modifiers{});
    EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << "drop";
    EXPECT_EQ(rig.controller.game().PlyCount(), 1) << "the move made";

    // The engine's move and the turn it gives back.
    ASSERT_TRUE(rig.FramesUntil([&] { return rig.controller.game().PlyCount() == 2 && person_to_move(); }));

    // A plate under the pointer, pressed and let go of.
    View *plate = rig.view.Plate(kFlip);
    ASSERT_NE(plate, nullptr);
    rig.watch->Forget();
    rig.router->Move(Rig::MiddleOf(*plate), Modifiers{});
    EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << "over a plate";
    rig.router->Down(Rig::MiddleOf(*plate), 1, false, Modifiers{});
    EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << "a plate pressed";
    rig.router->Up(Rig::MiddleOf(*plate), Modifiers{});
    EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << "a plate let go of";

    rig.controller.Command(kStopGame);
    ASSERT_TRUE(rig.FramesUntil([&] { return !rig.controller.IsPlaying(); }));
}

TEST(Repaint, AGameBetweenEnginesIsShownMoveByMove)
{
    Rig          rig;
    PlaySettings settings;
    settings.white.is_human = false;
    settings.white.engine   = Fake({"--think", "20"}, "Alpha");
    settings.black.is_human = false;
    settings.black.engine   = Fake({"--seed", "5", "--think", "20"}, "Beta");
    settings.time_control   = *chess::TimeControl::Parse("60+1");
    rig.controller.StartGame(settings);
    ASSERT_TRUE(rig.FramesUntil([&] { return rig.controller.game().PlyCount() >= 4; }));
    EXPECT_FALSE(rig.view.analysis().blocks().empty()) << "their searches on the panel";
    rig.controller.Command(kStopGame);
    ASSERT_TRUE(rig.FramesUntil([&] { return !rig.controller.IsPlaying(); }));
}

TEST(Repaint, EveryDialogOpensTakesThePointerAndTheKeysAndCloses)
{
    for (const int command : {kNewGame, kEngineManager, kNewMatch, kNewTournament, kAbout, kMakeBook, kSetUpPosition,
                              kShowStandings, kAnalyseGame})
    {
        Rig rig;
        rig.controller.catalogue().engines = {Fake({}, "Alpha"), Fake({"--seed", "5"}, "Beta")};
        chess::Game game; // a game to analyse
        game.PlaySan("e4");
        game.PlaySan("e5");
        rig.controller.ShowGame(game, {});
        rig.controller.Command(command);
        rig.controller.Frame(1.0 / 60.0);
        DialogFrame *dialog = rig.controller.dialog();
        if (command == kShowStandings)
        {
            EXPECT_EQ(dialog, nullptr) << "the standings open only with a tournament";
            continue;
        }
        ASSERT_NE(dialog, nullptr) << command;
        EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << command << " opened";

        const reckless::gfx::RectF card = dialog->CardBounds().ToFloat();
        // Across the card, row by row: the pointer over each control, a press on each, the keys to what takes them.
        for (float y = card.y() + 24.0f; y < card.bottom(); y += 48.0f)
        {
            const PointF point{card.x() + card.width() / 2.0f, y};
            rig.router->Move(point, Modifiers{});
            EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << command << " pointer over " << y;
        }
        for (float x = card.x() + 40.0f; x < card.right(); x += 120.0f)
        {
            const PointF point{x, card.y() + 90.0f};
            rig.router->Down(point, 1, false, Modifiers{});
            EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << command << " press at " << x;
            rig.router->Up(point, Modifiers{});
            EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << command << " release at " << x;
            rig.router->Key({Key::kCharacter, U'7', {}});
            EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << command << " a key at " << x;
            if (rig.controller.dialog() != dialog)
            {
                break; // the press closed it
            }
        }
        rig.controller.Frame(1.0 / 60.0);
        EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << command << " a frame";

        rig.watch->Forget();
        if (rig.controller.dialog() == dialog && dialog->on_cancel)
        {
            dialog->on_cancel();
        }
        rig.controller.Frame(1.0 / 60.0);
        EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << command << " closed";
    }
}

TEST(Repaint, ADialogsRunningWorkIsShown)
{
    Rig rig;
    rig.controller.catalogue().engines = {Fake({"--think", "20"}, "Fake")};
    const auto folder                  = reckless::test::ScratchPath("amphitheater-repaint-suite");
    std::filesystem::create_directories(folder);
    const auto suite = folder / "suite.epd";
    std::ofstream(suite, std::ios::trunc) << "3q3k/8/8/8/8/8/8/3Q3K w - - bm Qxd8+; id \"trade\";\n"
                                          << "3q3k/8/8/8/8/8/8/3Q3K w - - am Qxd8+; id \"no trade\";\n";
    ASSERT_TRUE(rig.controller.OpenTestSuite(suite.string()));
    auto *dialog = dynamic_cast<TestSuiteDialog *>(rig.controller.dialog());
    ASSERT_NE(dialog, nullptr);
    EXPECT_TRUE(rig.watch->AskedForAllThatChanged());
    ASSERT_TRUE(dialog->Run()) << dialog->summary();
    ASSERT_TRUE(rig.FramesUntil([&] { return !dialog->IsRunning(); }));
    EXPECT_EQ(dialog->model().RunCount(), 2);
}

TEST(Repaint, TheLogFoldedAndFedIsShown)
{
    Rig rig;
    rig.view.log().Add("Fake", LogDirection::kFromEngine, "info depth 1");
    EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << "a line, the log folded or not";
    rig.controller.Command(kShowLog);
    rig.controller.Frame(1.0 / 60.0);
    EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << "the log shown";
    rig.view.log().Add("Fake", LogDirection::kToEngine, "go infinite");
    rig.controller.Frame(1.0 / 60.0);
    EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << "a line in the log shown";
    rig.controller.Command(kClearLog);
    rig.controller.Frame(1.0 / 60.0);
    EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << "the log cleared";
    rig.controller.Command(kShowLog);
    rig.controller.Frame(1.0 / 60.0);
    EXPECT_TRUE(rig.watch->AskedForAllThatChanged()) << "the log folded";
}

TEST(Repaint, ASizeOrABookOpenedAsksForTheWindow)
{
    MainView       view(Pieces());
    MainController controller(view, ControllerFiles::None());
    view.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});
    {
        RepaintWatch watch(view);
        controller.Frame(1.0 / 60.0);
        EXPECT_TRUE(watch.AskedForAllThatChanged());
        controller.Command(kUseBuiltInBook);
        controller.Frame(1.0 / 60.0);
        EXPECT_TRUE(watch.AskedForAllThatChanged()) << "the book's list and its arrows";
        controller.Command(kCloseBook);
        controller.Frame(1.0 / 60.0);
        EXPECT_TRUE(watch.AskedForAllThatChanged()) << "the book closed";
        watch.Forget();
        view.SetBounds({MainView::kDesignWidth - 100, MainView::kDesignHeight - 80});
        EXPECT_GT(watch.asked(), 0) << "a resize";
        // The real window's raster is made anew and painted whole at a resize; here a watch's picture cannot change
        // size.
        view.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});
        EXPECT_TRUE(watch.AskedForAllThatChanged()) << "and back";
    }
}

} // namespace amphitheater::app
