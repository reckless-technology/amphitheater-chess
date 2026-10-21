#include "WindowSizes.h"

#include "SettingsFolder.h"

#include "Json.h"
#include "JsonFile.h"

#include <utility>

namespace amphitheater::app
{

namespace
{

namespace json = reckless::core::json;

constexpr int kFormat = 1; ///< The file's format.

} // namespace

WindowSizes::WindowSizes(reckless::app::AppRoot &root, reckless::gfx::PointI design, std::filesystem::path file)
    : sizes_(root, design), file_(std::move(file))
{
}

void WindowSizes::Restore()
{
    sizes_.Restore(file_.empty() ? 100 : Load(file_).value_or(100));
}

void WindowSizes::SetWindow(reckless::app::AppWindow *window)
{
    sizes_.SetWindow(window);
}

bool WindowSizes::Choose(int percent)
{
    if (!sizes_.Choose(percent))
    {
        return false;
    }
    if (!file_.empty())
    {
        Store(file_, percent);
    }
    return true;
}

void WindowSizes::Frame(double seconds)
{
    sizes_.Frame(seconds);
}

WindowSizeMenu WindowSizes::Menu() const
{
    return sizes_.Menu();
}

int WindowSizes::InForce() const
{
    return sizes_.InForce();
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
    return reckless::app::WindowSizes::IsOffered(percent);
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
    const std::filesystem::path folder = SettingsFolder();
    return folder.empty() ? folder : folder / "window.json";
}

} // namespace amphitheater::app
