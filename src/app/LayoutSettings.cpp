#include "LayoutSettings.h"

#include "SettingsFolder.h"

#include "Json.h"
#include "JsonFile.h"

#include <optional>
#include <system_error>

namespace amphitheater::app
{

namespace
{

namespace json = reckless::core::json;

constexpr int kFormat     = 1;     ///< The file's format, written and checked.
constexpr int kMostHeight = 10000; ///< The tallest log a file may hold, in design pixels: past any screen.

} // namespace

LayoutSettings LayoutSettings::Load(const std::filesystem::path &file)
{
    LayoutSettings  settings;
    std::error_code ignored;
    if (file.empty() || !std::filesystem::exists(file, ignored))
    {
        return settings;
    }
    const std::optional<json::Value> document = reckless::core::ReadJsonFile(file);
    if (!document || !document->IsObject())
    {
        return settings;
    }
    if (const json::Value *format = document->Find("format");
        format == nullptr || !format->IsInt() || format->AsInt() != kFormat)
    {
        return settings;
    }
    if (const json::Value *pieces = document->Find("pieces");
        pieces != nullptr && pieces->IsString() && IsPieceSet(pieces->AsString()))
    {
        settings.pieces = pieces->AsString();
    }
    const json::Value *log = document->Find("engine_log");
    if (log == nullptr || !log->IsObject())
    {
        return settings;
    }
    if (const json::Value *height = log->Find("height"); height != nullptr && height->IsInt() &&
                                                         height->AsInt() >= MainView::kLogLeastHeight &&
                                                         height->AsInt() <= kMostHeight)
    {
        settings.log_height = static_cast<int>(height->AsInt());
    }
    if (const json::Value *open = log->Find("open"); open != nullptr && open->IsBool())
    {
        settings.is_log_open = open->AsBool();
    }
    return settings;
}

bool LayoutSettings::Save(const std::filesystem::path &file) const
{
    json::Value log = json::Value::MakeObject();
    log.Set("height", log_height);
    log.Set("open", is_log_open);
    json::Value document = json::Value::MakeObject();
    document.Set("format", kFormat);
    document.Set("engine_log", std::move(log));
    document.Set("pieces", pieces);
    return reckless::core::WriteJsonFile(file, document);
}

std::filesystem::path LayoutSettings::DefaultFile()
{
    const std::filesystem::path folder = SettingsFolder();
    return folder.empty() ? folder : folder / "layout.json";
}

} // namespace amphitheater::app
