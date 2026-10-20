// amphitheater_render_main: the main window drawn to a PNG with no window and no display, through Skia, as the app
// draws it - the review images in docs/design/ are made with it.
//
//   amphitheater_render_main <out.png> [scale] [width height] [flipped] [empty] [newgame] [engines] [tournament]
//                       [standings] [about] [logfolded] [setup] [book] [makebook]
//
// The window shows the mockup's state (Showcase.h) unless "empty" asks for a fresh window's; "flipped" turns the board;
// "newgame" opens Game > New game over it, offering two engines; "engines" opens Engines > Manage engines on two
// engines, the first with options of every type; "tournament" opens Tournament > New tournament on three; "standings"
// shows Tournament > Standings and crosstable for a double round robin of four after ten games; "about" shows Help >
// About for an install carrying the five engines the installers pin; "logfolded" folds the engine log away to its
// header; "setup" opens Edit > Set up position on the position shown; "book" opens a sample opening book over the
// sample game, its moves under the card; "makebook" opens File > Make book from games over a library. The scale is
// the device pixels per design pixel, 1 by default; the size is the window's, in design pixels.

#include "AboutDialog.h"
#include "Commands.h"
#include "Faces.h"
#include "MainController.h"
#include "MainView.h"
#include "MakeBookDialog.h"
#include "Menus.h"
#include "PieceSet.h"
#include "Showcase.h"
#include "StandingsDialog.h"
#include "version/Version.h"

#include "SkiaCanvas.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace
{

/** The window's host with no window: text measured as Skia draws it, so a line of text lays out as the window does. */
class RenderHost final : public reckless::view::Host
{
  public:
    /**
     * Nothing to repaint: the whole window is drawn once.
     * @param root_area The area.
     */
    void Invalidate(reckless::gfx::RectI root_area) override
    {
        static_cast<void>(root_area);
    }

    /**
     * Text measured as Skia draws it.
     * @param font The font.
     * @param text The text.
     * @return Its width.
     */
    float TextWidth(const reckless::gfx::Font &font, std::string_view text) const override
    {
        return reckless::gfx::MeasureText(font, text);
    }
};

} // namespace

/**
 * Draw the main window into a PNG.
 * @param argc The argument count.
 * @param argv The arguments.
 * @return 0 when the file was written, 1 otherwise, 2 for a usage error.
 */
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: amphitheater_render_main <out.png> [scale] [width height] [view]\n");
        return 2;
    }
    const std::string        out = argv[1];
    std::vector<std::string> numbers;
    bool                     is_flipped    = false;
    bool                     is_empty      = false;
    bool                     is_newgame    = false;
    bool                     is_engines    = false;
    bool                     is_tournament = false;
    bool                     is_standings  = false;
    bool                     is_about      = false;
    bool                     is_log_folded = false;
    bool                     is_setup      = false;
    bool                     is_book       = false;
    bool                     is_make_book  = false;
    for (int index = 2; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "flipped")
        {
            is_flipped = true;
        }
        else if (argument == "empty")
        {
            is_empty = true;
        }
        else if (argument == "newgame")
        {
            is_newgame = true;
        }
        else if (argument == "engines")
        {
            is_engines = true;
        }
        else if (argument == "tournament")
        {
            is_tournament = true;
        }
        else if (argument == "standings")
        {
            is_standings = true;
        }
        else if (argument == "about")
        {
            is_about = true;
        }
        else if (argument == "logfolded")
        {
            is_log_folded = true;
        }
        else if (argument == "setup")
        {
            is_setup = true;
        }
        else if (argument == "book")
        {
            is_book = true;
        }
        else if (argument == "makebook")
        {
            is_make_book = true;
        }
        else
        {
            numbers.push_back(argument);
        }
    }
    const float scale = numbers.size() >= 1 ? std::strtof(numbers[0].c_str(), nullptr) : 1.0f;
    const int   width = numbers.size() >= 3 ? std::atoi(numbers[1].c_str()) : amphitheater::app::MainView::kDesignWidth;
    const int height = numbers.size() >= 3 ? std::atoi(numbers[2].c_str()) : amphitheater::app::MainView::kDesignHeight;
    if (!(scale > 0.0f) || width < 1 || height < 1)
    {
        std::fprintf(stderr, "amphitheater_render_main: a scale above zero and a size of at least one pixel\n");
        return 2;
    }

    amphitheater::app::InstallFaces();
    std::string                       error;
    const amphitheater::app::PieceSet pieces =
        amphitheater::app::PieceSet::FromEmbedded(amphitheater::app::kDefaultPieceSet, &error);
    if (!pieces.IsComplete())
    {
        std::fprintf(stderr, "amphitheater_render_main: the pieces: %s\n", error.c_str());
        return 1;
    }
    RenderHost                  host;
    amphitheater::app::MainView view(pieces);
    view.SetHost(&host);
    amphitheater::app::MainController controller(view, amphitheater::app::ControllerFiles{{}});
    if (!is_empty)
    {
        amphitheater::app::ShowSample(view);
        if (is_book)
        {
            amphitheater::app::ShowSampleBook(view);
        }
    }
    view.SetFlipped(is_flipped);
    view.SetLogLayout(view.log_height(), !is_log_folded);
    view.SetBounds({width, height});
    if (is_newgame)
    {
        for (const char *name : {"Pawnstar 2.1", "Zenith 0.9"})
        {
            amphitheater::app::EngineEntry engine;
            engine.config.name       = name;
            engine.config.executable = name;
            controller.catalogue().engines.push_back(engine);
        }
        controller.Command(amphitheater::app::kNewGame);
    }
    if (is_engines)
    {
        amphitheater::app::EngineEntry pawnstar;
        pawnstar.config.executable  = "C:/Engines/pawnstar/pawnstar.exe";
        pawnstar.engine_name        = "Pawnstar 2.1";
        pawnstar.author             = "Jonny Reckless";
        pawnstar.config.does_ponder = true;
        pawnstar.book_path          = "C:/Books/openings.bin";
        pawnstar.book_depth         = 16;
        for (const char *line :
             {"option name Threads type spin default 1 min 1 max 256",
              "option name Hash type spin default 16 min 1 max 65536", "option name Clear Hash type button",
              "option name Ponder type check default false", "option name MultiPV type spin default 1 min 1 max 256",
              "option name Style type combo default Normal var Solid var Normal var Risky",
              "option name UCI_ShowWDL type check default false", "option name SyzygyPath type string default <empty>"})
        {
            pawnstar.declared.push_back(*amphitheater::uci::ParseOptionLine(line));
        }
        pawnstar.SetOption("Hash", "64");
        amphitheater::app::EngineEntry zenith;
        zenith.config.executable       = "C:/Engines/zenith/zenith.exe";
        zenith.engine_name             = "Zenith 0.9";
        zenith.author                  = "Jonny Reckless";
        controller.catalogue().engines = {pawnstar, zenith};
        controller.Command(amphitheater::app::kEngineManager);
    }
    if (is_setup)
    {
        // The sample game's position, as the card's FEN gives it, for the dialog to open on.
        controller.Paste("r2q1rk1/3nbppp/p2pbn2/4p1P1/1p2P3/1NN1BP2/PPPQ3P/2KR1B1R w - - 0 13");
        controller.Command(amphitheater::app::kSetUpPosition);
    }
    if (is_make_book)
    {
        controller.Command(amphitheater::app::kMakeBook);
        if (auto *dialog = dynamic_cast<amphitheater::app::MakeBookDialog *>(controller.dialog()))
        {
            dialog->games().SetText("C:/Users/Jonny/Chess/Classical games 2000-2026.zip");
            dialog->out().SetText("C:/Users/Jonny/Chess/classical.bin");
        }
    }
    if (is_tournament)
    {
        for (const char *name : {"Pawnstar 2.1", "Zenith 0.9", "Stoopid Chess 1.4"})
        {
            amphitheater::app::EngineEntry engine;
            engine.config.name       = name;
            engine.config.executable = name;
            controller.catalogue().engines.push_back(engine);
        }
        controller.Command(amphitheater::app::kNewTournament);
    }
    // Ten games of a double round robin of four: the leader two and a half points clear, one pairing yet to meet.
    using amphitheater::chess::GameResult;
    const std::vector<std::string> names = {"Pawnstar 2.1", "Zenith 0.9", "Stoopid Chess 1.4", "Blunderbuss 3"};
    const std::vector<amphitheater::match::GameResultEntry> results = {
        {0, 1, GameResult::kWhiteWins}, {1, 0, GameResult::kDraw},      {0, 2, GameResult::kWhiteWins},
        {2, 0, GameResult::kDraw},      {1, 2, GameResult::kBlackWins}, {2, 1, GameResult::kWhiteWins},
        {0, 3, GameResult::kWhiteWins}, {3, 0, GameResult::kBlackWins}, {1, 3, GameResult::kDraw},
        {3, 1, GameResult::kWhiteWins}};
    amphitheater::app::StandingsDialog standings([&] { return std::make_pair(names, results); });
    if (is_standings)
    {
        view.SetOverlay(&standings);
    }
    // The five engines the installers carry, at the versions packaging/engines.json pins, as an install's About shows.
    amphitheater::app::AboutInfo about_info;
    about_info.version = amphitheater::VersionText();
    about_info.engines = {{"Pawnstar", "1.2.0", "GPL-3.0"},
                          {"Zenith", "2.1.0", "GPL-3.0-or-later"},
                          {"Pawnstar-Go", "1.2.0", "GPL-3.0"},
                          {"Pawnstar-RS", "1.2.0", "GPL-3.0"},
                          {"Stoopid Chess", "3.0.0", "GPL-3.0"}};
    amphitheater::app::AboutDialog about(about_info);
    if (is_about)
    {
        view.SetOverlay(&about);
    }

    reckless::gfx::Image image = reckless::gfx::MakeImage(static_cast<int>(static_cast<float>(width) * scale + 0.5f),
                                                          static_cast<int>(static_cast<float>(height) * scale + 0.5f));
    {
        const auto canvas = reckless::gfx::MakeImageCanvas(image, scale);
        if (!canvas)
        {
            std::fprintf(stderr, "amphitheater_render_main: no canvas\n");
            return 1;
        }
        reckless::view::PaintTree(view, *canvas);
    }
    const std::vector<std::uint8_t> png = reckless::gfx::EncodePng(image);
    std::ofstream                   file(out, std::ios::binary);
    file.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!file)
    {
        std::fprintf(stderr, "amphitheater_render_main: could not write %s\n", out.c_str());
        return 1;
    }
    std::printf("%s: %d x %d at %.2fx\n", out.c_str(), width, height, static_cast<double>(scale));
    return 0;
}
