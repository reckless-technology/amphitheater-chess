// Reckless UCI Host: the window on the machine's own platform, through the toolkit's app shell (AppRun.h, AppRoot.h)
// and its child window on Skia. `--smoke [frames]` is a launch's check: the window opened, a few frames drawn, closed -
// exit 0 when every frame was drawn, 3 when too few were, 1 where no window could be shown.

#include "Faces.h"
#include "MainController.h"
#include "MainView.h"
#include "PieceSet.h"
#include "Theme.h"
#include "version/Version.h"

#include "AppPlatform.h"
#include "AppRoot.h"
#include "AppRun.h"
#include "Embedded.h"
#include "Window.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

/**
 * Reckless UCI Host's window.
 * @param argc The argument count.
 * @param argv The arguments.
 * @return The exit code (reckless::app::RunApp).
 */
int main(int argc, char **argv)
{
    using uci_host::app::MainView;
    const reckless::app::AppOptions options =
        reckless::app::ParseAppOptions(std::vector<std::string>(argv + 1, argv + argc));
    uci_host::app::InstallFaces();
    std::string                   error;
    const uci_host::app::PieceSet pieces = uci_host::app::PieceSet::FromEmbedded(&error);
    if (!pieces.IsComplete())
    {
        std::fprintf(stderr, "Reckless UCI Host: the pieces could not be read: %s\n", error.c_str());
    }

    const std::unique_ptr<reckless::app::AppPlatform> platform =
        reckless::app::AppPlatform::Create("Reckless UCI Host", reckless::embedded::Find("RecklessAppIcon256.png"));
    MainView                      view(pieces);
    uci_host::app::MainController controller(view);
    reckless::app::AppRoot        root(view, uci_host::app::theme::Shell().Tooltip(), {},
                                       {MainView::kLayoutWidth, MainView::kLayoutHeight});
    root.SetBounds({MainView::kDesignWidth, MainView::kDesignHeight});

    reckless::app::AppRunHooks hooks;
    hooks.on_frame = [&](double seconds) {
        controller.Frame(seconds);
        root.Frame();
    };
    return reckless::app::RunApp(*platform, root, {MainView::kMinimumWidth, MainView::kMinimumHeight}, options,
                                 "Reckless UCI Host", uci_host::VersionText(), &reckless::window::CreateChildWindow,
                                 hooks);
}
