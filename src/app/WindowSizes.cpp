#include "WindowSizes.h"

#include "Json.h"
#include "JsonFile.h"
#include "PresetFiles.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace uci_host::app
{

namespace
{

namespace json = reckless::core::json;

constexpr int kFormat = 1; ///< The file's format.

/**
 * @param percent A size, in percent.
 * @return Its zoom: exact for every size offered.
 */
float ZoomOf(int percent)
{
    return static_cast<float>(percent) / 100.0f;
}

} // namespace

WindowSizes::WindowSizes(reckless::app::AppRoot &root, reckless::gfx::PointI design, std::filesystem::path file)
    : root_(root), design_(design), file_(std::move(file))
{
}

void WindowSizes::Restore()
{
    const int                   percent = file_.empty() ? 100 : Load(file_).value_or(100);
    const reckless::gfx::PointI size    = root_.SetFixedZoom(design_, ZoomOf(percent));
    root_.SetBounds({size.x, size.y});
}

void WindowSizes::SetWindow(reckless::app::AppWindow *window)
{
    window_ = window;
    room_   = {0, 0};
    RefreshRoom();
}

bool WindowSizes::Choose(int percent)
{
    if (!IsOffered(percent) || window_ == nullptr || !Fits(percent))
    {
        return false;
    }
    const reckless::gfx::PointI size = root_.SetFixedZoom(design_, ZoomOf(percent));
    if (root_.width() != size.x || root_.height() != size.y)
    {
        window_->SetContentSizeOnScreen(size.x, size.y); // the root follows the window's resize
    }
    RefreshRoom();
    if (!file_.empty())
    {
        Store(file_, percent);
    }
    return true;
}

void WindowSizes::Frame(double seconds)
{
    elapsed_ += seconds;
    if (elapsed_ >= kRefreshSeconds)
    {
        RefreshRoom();
    }
}

WindowSizeMenu WindowSizes::Menu() const
{
    WindowSizeMenu menu;
    menu.has_window = window_ != nullptr;
    menu.in_force   = InForce();
    for (std::size_t index = 0; index < kWindowSizes.size(); ++index)
    {
        menu.fits[index] = menu.has_window && Fits(kWindowSizes[index]);
    }
    if (!menu.has_window)
    {
        return menu;
    }
    // From the size in force, or - dragged to another - from the scale the window is nearest the design at, so a step
    // from a dragged window goes to the next size past it either way.
    const double     now = menu.in_force != 0
                               ? static_cast<double>(menu.in_force)
                               : 100.0 * std::min(static_cast<double>(root_.width()) / static_cast<double>(design_.x),
                                                  static_cast<double>(root_.height()) / static_cast<double>(design_.y));
    constexpr double kTolerance = 0.5; // half a percent: a window a pixel off a size is at it
    for (std::size_t index = 0; index < kWindowSizes.size(); ++index)
    {
        const int percent = kWindowSizes[index];
        if (menu.larger == 0 && percent > now + kTolerance && menu.fits[index])
        {
            menu.larger = percent;
        }
        if (percent < now - kTolerance && menu.fits[index])
        {
            menu.smaller = percent;
        }
    }
    return menu;
}

int WindowSizes::InForce() const
{
    const std::optional<float> zoom = root_.fixed_zoom();
    if (!zoom)
    {
        return 0;
    }
    const int percent = static_cast<int>(std::lround(*zoom * 100.0f));
    return IsOffered(percent) ? percent : 0;
}

void WindowSizes::Save() const
{
    if (!file_.empty())
    {
        const int percent = InForce();
        Store(file_, percent != 0 ? std::optional<int>(percent) : std::nullopt);
    }
}

bool WindowSizes::IsOffered(int percent)
{
    return std::find(kWindowSizes.begin(), kWindowSizes.end(), percent) != kWindowSizes.end();
}

std::optional<int> WindowSizes::Load(const std::filesystem::path &file)
{
    const std::optional<json::Value> document = reckless::core::ReadJsonFile(file);
    if (!document)
    {
        return std::nullopt;
    }
    const json::Value *size = document->Find("size");
    if (size == nullptr || !size->IsInt() || !IsOffered(static_cast<int>(size->AsInt())))
    {
        return std::nullopt;
    }
    return static_cast<int>(size->AsInt());
}

bool WindowSizes::Store(const std::filesystem::path &file, std::optional<int> percent)
{
    json::Value document = json::Value::MakeObject();
    document.Set("format", kFormat);
    if (percent)
    {
        document.Set("size", *percent);
    }
    return reckless::core::WriteJsonFile(file, document);
}

std::filesystem::path WindowSizes::DefaultFile()
{
    const std::filesystem::path folder = reckless::core::UserSettingsFolder(reckless::core::kSuiteFolder);
    return folder.empty() ? folder : folder / "uci-host" / "window.json";
}

reckless::gfx::PointI WindowSizes::SizeAt(int percent) const
{
    return {static_cast<int>(std::lround(static_cast<double>(design_.x) * percent / 100.0)),
            static_cast<int>(std::lround(static_cast<double>(design_.y) * percent / 100.0))};
}

bool WindowSizes::Fits(int percent) const
{
    if (room_.x <= 0 || room_.y <= 0)
    {
        return true;
    }
    const reckless::gfx::PointI size = SizeAt(percent);
    return size.x <= room_.x && size.y <= room_.y;
}

void WindowSizes::RefreshRoom()
{
    elapsed_ = 0.0;
    if (window_ != nullptr)
    {
        room_ = window_->LargestContent();
    }
}

} // namespace uci_host::app
