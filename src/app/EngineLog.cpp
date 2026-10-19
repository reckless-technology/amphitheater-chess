#include "EngineLog.h"

#include "Theme.h"

#include "ListStyles.h"

#include <algorithm>

namespace uci_host::app
{

void EngineLogModel::Add(std::string_view engine, LogDirection direction, std::string_view line)
{
    std::string text;
    text.reserve(engine.size() + line.size() + 4);
    text += direction == LogDirection::kToEngine ? "> " : direction == LogDirection::kFromEngine ? "< " : "! ";
    text += engine;
    text += "  ";
    text += line;
    lines_.push_back({direction, std::move(text)});
    while (lines_.size() > kMostLines)
    {
        lines_.pop_front();
    }
}

std::size_t EngineLogModel::RowCount() const
{
    return lines_.size();
}

std::string EngineLogModel::CellText(std::size_t row, std::size_t column) const
{
    static_cast<void>(column);
    return row < lines_.size() ? lines_[row].text : std::string();
}

std::optional<reckless::gfx::Color> EngineLogModel::CellColour(std::size_t row, std::size_t column) const
{
    static_cast<void>(column);
    if (row >= lines_.size())
    {
        return std::nullopt;
    }
    switch (lines_[row].direction)
    {
    case LogDirection::kToEngine:
        return theme::kAmber;
    case LogDirection::kError:
        return theme::kMinus;
    case LogDirection::kFromEngine:
        break;
    }
    return theme::kLogText;
}

namespace
{

/** @return The log's look: the monospaced face on the darkest colour, no stripes, no header. */
reckless::view::ListView::Style LogStyle()
{
    auto style          = reckless::panel::ListViewStyle(theme::Lists(), fonts::Mono());
    style.background    = theme::kBoardEdge;
    style.stripe        = reckless::gfx::Color();
    style.header_height = 0;
    style.row_height    = 25;
    style.padding       = 12;
    return style;
}

} // namespace

EngineLog::EngineLog() : list_(LogStyle())
{
    list_.SetModel(&model_);
    list_.SetSelectionMode(reckless::view::ListView::SelectionMode::kMultiple);
    list_.stick_to_bottom = true;
}

void EngineLog::Add(std::string_view engine, LogDirection direction, std::string_view line)
{
    model_.Add(engine, direction, line);
    list_.RowsChanged();
}

void EngineLog::Clear()
{
    model_.Clear();
    list_.RowsChanged();
}

int EngineLog::FittedHeight(int room) const
{
    const int row = list_.style().row_height;
    return std::max(1, room / row) * row;
}

} // namespace uci_host::app
