// Reckless UCI Host: the window on the machine's own platform, through the toolkit's app platform (AppPlatform.h) and
// its child window on Skia (Window.h). `--smoke [frames]` is a launch's check: the window opened, a few frames drawn,
// closed - exit 0 when every frame was drawn, 3 when none was in twenty seconds, 1 where no window could be shown.

#include "version/Version.h"

#include "AppPlatform.h"
#include "Embedded.h"
#include "SkiaCanvas.h"
#include "SvgImage.h"
#include "View.h"
#include "Window.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

constexpr int kWidth  = 1240; ///< The window's design width, in design pixels.
constexpr int kHeight = 900;  ///< The window's design height, in design pixels.

/** The window's content while the main view is built: its colour, the name and the version, and a king. */
class Placeholder final : public reckless::view::View
{
  public:
    /** @param version The version shown under the name. */
    explicit Placeholder(std::string version) : version_(std::move(version))
    {
        SetOpaque(true);
        const std::span<const std::uint8_t> svg = reckless::embedded::Find("pieces/wK.svg");
        king_ =
            reckless::gfx::SvgImage::Parse(std::string_view(reinterpret_cast<const char *>(svg.data()), svg.size()));
    }

    /**
     * The window's colour, the name in the serif, the version in the monospaced face, and the white king.
     * @param c The canvas.
     */
    void Paint(reckless::gfx::Canvas &c) override
    {
        using namespace reckless::gfx;
        c.FillAll(Color(0xff29282cu));
        const RectF area = LocalBounds().ToFloat();
        if (king_)
        {
            king_->DrawIn(c, RectF{area.CentreX() - 80.0f, area.CentreY() - 200.0f, 160.0f, 160.0f});
        }
        c.SetColor(Color(0xffe8dec8u));
        c.SetFont(Font(28.0f).Bold());
        c.DrawText("Reckless UCI Host", RectF{0.0f, area.CentreY(), area.width(), 40.0f}, Justify::kCentred);
        c.SetColor(Color(0xff9c9483u));
        c.SetFont(Font(15.0f).WithFamily(FontFamily::kMonospace));
        c.DrawText(version_, RectF{0.0f, area.CentreY() + 44.0f, area.width(), 24.0f}, Justify::kCentred);
    }

  private:
    std::string                            version_; ///< The version shown.
    std::optional<reckless::gfx::SvgImage> king_;    ///< The white king, as the board will draw it.
};

/** What the command line asks of the window. */
struct Options
{
    bool is_smoke     = false; ///< --smoke: a launch's check.
    int  smoke_frames = 30;    ///< The frames a smoke run must draw.
};

/**
 * The options of a command line.
 * @param arguments The arguments after the program's name.
 * @return The options.
 */
Options ParseOptions(const std::vector<std::string> &arguments)
{
    Options options;
    for (std::size_t index = 0; index < arguments.size(); ++index)
    {
        if (arguments[index] == "--smoke")
        {
            options.is_smoke = true;
            if (index + 1 < arguments.size())
            {
                char      *end    = nullptr;
                const long frames = std::strtol(arguments[index + 1].c_str(), &end, 10);
                if (end != nullptr && *end == '\0' && frames > 0)
                {
                    options.smoke_frames = static_cast<int>(frames);
                    ++index;
                }
            }
        }
        else
        {
            std::fprintf(stderr, "Reckless UCI Host: ignoring the argument '%s'\n", arguments[index].c_str());
        }
    }
    return options;
}

} // namespace

/**
 * Reckless UCI Host's window.
 * @param argc The argument count.
 * @param argv The arguments.
 * @return The exit code.
 */
int main(int argc, char **argv)
{
    const Options     options = ParseOptions(std::vector<std::string>(argv + 1, argv + argc));
    const std::string version = uci_host::VersionText();
    reckless::gfx::SetSerifFaces(reckless::embedded::Find("NotoSerif-Regular.ttf"),
                                 reckless::embedded::Find("NotoSerif-Bold.ttf"));
    reckless::gfx::SetMonospaceFaces(reckless::embedded::Find("MonaspaceXenon-Regular.otf"),
                                     reckless::embedded::Find("MonaspaceXenon-Bold.otf"));
    if (options.is_smoke)
    {
        std::printf("Reckless UCI Host %s\n", version.c_str());
        std::fflush(stdout);
    }

    const std::unique_ptr<reckless::app::AppPlatform> platform =
        reckless::app::AppPlatform::Create("Reckless UCI Host", reckless::embedded::Find("RecklessAppIcon256.png"));
    Placeholder root(version);
    root.SetBounds({kWidth, kHeight});

    reckless::app::WindowOptions window_options;
    window_options.title   = "Reckless UCI Host";
    window_options.width   = kWidth;
    window_options.height  = kHeight;
    window_options.closing = [&platform] { platform->CallSoon([&platform] { platform->Quit(); }); };
    const std::unique_ptr<reckless::app::AppWindow> window = platform->OpenWindow(window_options);
    std::unique_ptr<reckless::window::ChildWindow>  child;
    if (window)
    {
        child = reckless::window::CreateChildWindow(window->Handle(), root, kWidth, kHeight, window->Scale(),
                                                    platform->ChildLoop());
    }
    if (!child)
    {
        std::fprintf(stderr, "Reckless UCI Host: no window to draw in - no display, or none Skia can draw on\n");
        return 1;
    }

    int  drawn       = 0;
    bool is_quitting = false;
    child->SetFrameCallback([&] {
        if (options.is_smoke && !is_quitting && ++drawn >= options.smoke_frames)
        {
            is_quitting = true;
            platform->CallSoon([&platform] { platform->Quit(); });
        }
    });
    platform->SetQuitHandler([&platform] { platform->Quit(); });
    const auto started = std::chrono::steady_clock::now();
    platform->StartTimer(1000 / 30, [&] {
        if (options.is_smoke && drawn == 0 && std::chrono::steady_clock::now() - started > std::chrono::seconds(20))
        {
            platform->Quit();
        }
    });
    const int code = platform->Run();
    child->SetFrameCallback(nullptr);
    platform->SetQuitHandler(nullptr);
    child.reset();
    if (options.is_smoke)
    {
        std::printf("Reckless UCI Host: the window drew %d of %d frames\n", drawn, options.smoke_frames);
        return code != 0 ? code : (drawn >= options.smoke_frames ? 0 : 3);
    }
    return code;
}
