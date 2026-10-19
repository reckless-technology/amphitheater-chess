// uci_host_render_main: the main window drawn to a PNG with no window and no display, through Skia, as the app draws
// it - the review images in docs/design/ are made with it.
//
//   uci_host_render_main <out.png> [scale] [width height] [flipped] [empty] [newgame]
//
// The window shows the mockup's state (Showcase.h) unless "empty" asks for a fresh window's; "flipped" turns the
// board; "newgame" opens Game > New game over it, offering two engines. The scale is the device pixels per design
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
    uci_host::app::MainView       view(pieces);
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
