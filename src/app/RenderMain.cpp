// uci_host_render_main: the main window drawn to a PNG with no window and no display, through Skia, as the app draws
// it - the review images in docs/design/ are made with it.
//
//   uci_host_render_main <out.png> [scale] [width height] [flipped] [empty] [newgame] [engines]
//
// The window shows the mockup's state (Showcase.h) unless "empty" asks for a fresh window's; "flipped" turns the
// board; "newgame" opens Game > New game over it, offering two engines; "engines" opens Engines > Manage engines on
// two engines, the first with options of every type. The scale is the device pixels per design
// pixel, 1 by default; the size is the window's, in design pixels.

#include "Commands.h"
#include "Faces.h"
#include "MainController.h"
#include "MainView.h"
#include "Menus.h"
#include "PieceSet.h"
#include "Showcase.h"

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
        std::fprintf(stderr, "usage: uci_host_render_main <out.png> [scale] [width height] [flipped] [empty]\n");
        return 2;
    }
    const std::string        out = argv[1];
    std::vector<std::string> numbers;
    bool                     is_flipped = false;
    bool                     is_empty   = false;
    bool                     is_newgame = false;
    bool                     is_engines = false;
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
        else
        {
            numbers.push_back(argument);
        }
    }
    const float scale  = numbers.size() >= 1 ? std::strtof(numbers[0].c_str(), nullptr) : 1.0f;
    const int   width  = numbers.size() >= 3 ? std::atoi(numbers[1].c_str()) : uci_host::app::MainView::kDesignWidth;
    const int   height = numbers.size() >= 3 ? std::atoi(numbers[2].c_str()) : uci_host::app::MainView::kDesignHeight;
    if (!(scale > 0.0f) || width < 1 || height < 1)
    {
        std::fprintf(stderr, "uci_host_render_main: a scale above zero and a size of at least one pixel\n");
        return 2;
    }

    uci_host::app::InstallFaces();
    std::string                   error;
    const uci_host::app::PieceSet pieces = uci_host::app::PieceSet::FromEmbedded(&error);
    if (!pieces.IsComplete())
    {
        std::fprintf(stderr, "uci_host_render_main: the pieces: %s\n", error.c_str());
        return 1;
    }
    RenderHost              host;
    uci_host::app::MainView view(pieces);
    view.SetHost(&host);
    uci_host::app::MainController controller(view, uci_host::app::ControllerFiles{{}});
    if (!is_empty)
    {
        uci_host::app::ShowSample(view);
    }
    view.SetFlipped(is_flipped);
    view.SetBounds({width, height});
    if (is_newgame)
    {
        for (const char *name : {"Pawnstar 2.1", "Zenith 0.9"})
        {
            uci_host::app::EngineEntry engine;
            engine.config.name       = name;
            engine.config.executable = name;
            controller.catalogue().engines.push_back(engine);
        }
        controller.Command(uci_host::app::kNewGame);
    }
    if (is_engines)
    {
        uci_host::app::EngineEntry pawnstar;
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
            pawnstar.declared.push_back(*uci_host::uci::ParseOptionLine(line));
        }
        pawnstar.SetOption("Hash", "64");
        uci_host::app::EngineEntry zenith;
        zenith.config.executable       = "C:/Engines/zenith/zenith.exe";
        zenith.engine_name             = "Zenith 0.9";
        zenith.author                  = "Jonny Reckless";
        controller.catalogue().engines = {pawnstar, zenith};
        controller.Command(uci_host::app::kEngineManager);
    }

    reckless::gfx::Image image = reckless::gfx::MakeImage(static_cast<int>(static_cast<float>(width) * scale + 0.5f),
                                                          static_cast<int>(static_cast<float>(height) * scale + 0.5f));
    {
        const auto canvas = reckless::gfx::MakeImageCanvas(image, scale);
        if (!canvas)
        {
            std::fprintf(stderr, "uci_host_render_main: no canvas\n");
            return 1;
        }
        reckless::view::PaintTree(view, *canvas);
    }
    const std::vector<std::uint8_t> png = reckless::gfx::EncodePng(image);
    std::ofstream                   file(out, std::ios::binary);
    file.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!file)
    {
        std::fprintf(stderr, "uci_host_render_main: could not write %s\n", out.c_str());
        return 1;
    }
    std::printf("%s: %d x %d at %.2fx\n", out.c_str(), width, height, static_cast<double>(scale));
    return 0;
}
