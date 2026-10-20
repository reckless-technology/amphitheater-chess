// Reckless Amphitheater: the window on the machine's own platform, through the toolkit's app shell (AppRun.h,
// AppRoot.h) and its child window on Skia. `--smoke [frames]` is a launch's check: the window opened, a few frames
// drawn, closed - exit 0 when every frame was drawn, 3 when too few were, 1 where no window could be shown.

#include "BundledEngines.h"
#include "Faces.h"
#include "MainController.h"
#include "MainView.h"
#include "PieceSet.h"
#include "SettingsFolder.h"
#include "Theme.h"
#include "WindowSizes.h"
#include "version/Version.h"

#include "AppPlatform.h"
#include "AppRoot.h"
#include "AppRun.h"
#include "AppUpdates.h"
#include "Embedded.h"
#include "Window.h"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * Reckless Amphitheater's window.
 * @param argc The argument count.
 * @param argv The arguments.
 * @return The exit code (reckless::app::RunApp).
 */
int main(int argc, char **argv)
{
    using amphitheater::app::MainView;
    reckless::app::AppOptions options = reckless::app::ParseAppOptions(std::vector<std::string>(argv + 1, argv + argc));
    // A PGN file named on the command line (a file opened with the app) is shown as the window opens.
    std::vector<std::string> pgn_files;
    std::erase_if(options.unknown, [&pgn_files](const std::string &argument) {
        const bool is_pgn = argument.size() > 4 && argument.substr(argument.size() - 4) == ".pgn";
        if (is_pgn)
        {
            pgn_files.push_back(argument);
        }
        return is_pgn;
    });
    amphitheater::app::InstallFaces();
    std::string                       error;
    const amphitheater::app::PieceSet pieces = amphitheater::app::PieceSet::FromEmbedded(&error);
    if (!pieces.IsComplete())
    {
        std::fprintf(stderr, "Reckless Amphitheater: the pieces could not be read: %s\n", error.c_str());
    }

    const std::unique_ptr<reckless::app::AppPlatform> platform =
        reckless::app::AppPlatform::Create("Reckless Amphitheater", reckless::embedded::Find("icon-256.png"));
    MainView                              view(pieces);
    amphitheater::app::ControllerServices services;
    services.read_clipboard  = [&platform] { return platform->ClipboardText(); };
    services.write_clipboard = [&platform](const std::string &text) { return platform->SetClipboardText(text); };
    services.quit            = [&platform] { platform->CallSoon([&platform] { platform->Quit(); }); };
    services.open_url        = [](const std::string &url) { reckless::window::OpenInBrowser(url); };
    // The menu bar the platform's own - the screen's on a Mac, the window's on Windows - with its shortcuts working
    // whichever view has the keyboard; on Linux, which has none, the platform's bar drawn across the window's top.
    services.set_menu_bar = [&platform](const reckless::app::MenuBar &bar, std::function<void(int id)> chosen) {
        platform->SetMenuBar(bar, std::move(chosen));
    };
    if (!platform->HasNativeMenuBar())
    {
        view.SetMenuBarView(platform->MakeMenuBarView(amphitheater::app::theme::MenuBarLook()));
    }
    // The settings earlier builds kept in reckless-technology/uci-host/ brought into the app's folder before anything
    // reads them (SettingsFolder.h); a launch's check leaves both folders alone.
    if (!options.is_smoke)
    {
        amphitheater::app::BringOverSettings(amphitheater::app::EarlierSettingsFolder(),
                                             amphitheater::app::SettingsFolder());
    }
    // The window's root, and the sizes the View menu offers (WindowSizes.h): opened at the size chosen last, kept in
    // the user's settings, or at the design size; a launch's check keeps none.
    reckless::app::AppRoot         root(view, amphitheater::app::theme::Shell().Tooltip(), {},
                                        {MainView::kLayoutWidth, MainView::kLayoutHeight});
    amphitheater::app::WindowSizes sizes(root, {MainView::kDesignWidth, MainView::kDesignHeight},
                                         options.is_smoke ? std::filesystem::path()
                                                          : amphitheater::app::WindowSizes::DefaultFile());
    sizes.Restore();
    services.window_sizes       = [&sizes] { return sizes.Menu(); };
    services.choose_window_size = [&sizes](int percent) { sizes.Choose(percent); };
    // The engines the install carries beside the app (BundledEngines.h): seeded into the person's engine list. A build
    // tree's app carries none, and leaves the list as it is.
    amphitheater::app::ControllerFiles files;
    files.bundled_engines = amphitheater::app::BundledEnginesFolder(amphitheater::app::RunningExecutable());
    amphitheater::app::MainController controller(view, files, services);
    // The update check, as every product on the toolkit runs it: once a day at most while the window shows, never in a
    // launch's check, and none where RECKLESS_UPDATE_CHECK=off (every CI runner).
    if (!options.is_smoke)
    {
        const reckless::update::Product product{"RecklessAmphitheater", amphitheater::VersionText(),
                                                "https://recklesstechnology.com/updates/amphitheater.json",
                                                "amphitheater-updates.json"};
        controller.SetUpdateChecker(reckless::app::MakeUpdateChecker(product),
                                    [](const std::string &url) { reckless::window::OpenInBrowser(url); });
    }
    for (const std::string &file : pgn_files)
    {
        controller.OpenPgn(file);
    }

    // The window titled after the game it shows, retitled whenever the game or its file changes.
    reckless::app::AppWindow  *window = nullptr;
    std::string                title;
    reckless::app::AppRunHooks hooks;
    hooks.has_menu_bar = true;
    hooks.on_open      = [&](reckless::app::AppWindow &opened) {
        window = &opened;
        sizes.SetWindow(&opened);
    };
    hooks.on_frame = [&](double seconds) {
        sizes.Frame(seconds);
        controller.Frame(seconds);
        root.Frame();
        if (std::string now = controller.WindowTitle(); window != nullptr && now != title)
        {
            title = std::move(now);
            window->SetTitle(title);
        }
    };
    hooks.on_end = [&](std::optional<std::pair<int, int>> position) {
        static_cast<void>(position);
        sizes.Save(); // the size in force, or none where the window was dragged to another
        sizes.SetWindow(nullptr);
        window = nullptr;
    };
    return reckless::app::RunApp(*platform, root, {MainView::kMinimumWidth, MainView::kMinimumHeight}, options,
                                 "Reckless Amphitheater", amphitheater::VersionText(),
                                 &reckless::window::CreateChildWindow, hooks);
}
