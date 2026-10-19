#include "EngineLog.h"

#include "Theme.h"

#include "ListStyles.h"

#include <algorithm>

namespace uci_host::app
{

namespace
{

/**
 * Whether a byte starts a character in UTF-8: anything but a continuation byte.
 * @param byte The byte.
 * @return Whether it starts one.
 */
bool StartsCharacter(char byte)
{
    return (static_cast<unsigned char>(byte) & 0xc0u) != 0x80u;
}

/**
 * Where a run of characters ends.
 * @param text The text.
 * @param start The run's first byte.
 * @param characters How many characters it holds at most.
 * @return The byte after the run: the text's end when it holds fewer.
 */
std::size_t AfterCharacters(std::string_view text, std::size_t start, int characters)
{
    std::size_t at = start;
    for (int count = 0; at < text.size() && count < characters; ++count)
    {
        ++at;
        while (at < text.size() && !StartsCharacter(text[at]))
        {
            ++at;
        }
    }
    return at;
}

/**
 * How many characters some text holds.
 * @param text The text, UTF-8.
 * @return The characters.
 */
int CharacterCount(std::string_view text)
{
    return static_cast<int>(std::count_if(text.begin(), text.end(), StartsCharacter));
}

/** The fewest characters a row holds, however narrow the log. */
constexpr int kNarrowestRow = 8;

} // namespace

std::vector<std::pair<std::size_t, std::size_t>> WrapLine(std::string_view text, int columns, int indent)
{
    std::vector<std::pair<std::size_t, std::size_t>> rows;
    if (columns <= 0)
    {
        rows.emplace_back(0, text.size());
        return rows;
    }
    columns               = std::max(columns, kNarrowestRow);
    const int   continued = columns - std::clamp(indent, 0, columns / 2);
    std::size_t start     = 0;
    bool        is_first  = true;
    while (true)
    {
        const int         room = is_first ? columns : continued;
        const std::size_t end  = AfterCharacters(text, start, room);
        if (end >= text.size())
        {
            rows.emplace_back(start, text.size() - start);
            return rows;
        }
        // The last space that fits, or the space just past the row: the break; else the row as far as it goes.
        std::size_t space = text.find_last_of(' ', end);
        if (space == std::string_view::npos || space <= start)
        {
            rows.emplace_back(start, end - start);
            start = end;
        }
        else
        {
            rows.emplace_back(start, space - start);
            start = text.find_first_not_of(' ', space);
            if (start == std::string_view::npos)
            {
                return rows;
            }
        }
        is_first = false;
    }
}

void EngineLogModel::Add(std::string_view engine, LogDirection direction, std::string_view line)
{
    std::string text;
    text.reserve(engine.size() + line.size() + 4);
    text += direction == LogDirection::kToEngine ? "> " : direction == LogDirection::kFromEngine ? "< " : "! ";
    text += engine;
    text += "  ";
    const int indent = CharacterCount(text);
    text += line;
    lines_.push_back({direction, std::move(text), next_serial_++, indent});
    AppendRows(lines_.back());
    while (lines_.size() > kMostLines)
    {
        const std::uint64_t gone = lines_.front().serial;
        while (!rows_.empty() && rows_.front().serial == gone)
        {
            rows_.pop_front();
        }
        lines_.pop_front();
    }
}

void EngineLogModel::Clear()
{
    lines_.clear();
    rows_.clear();
}

bool EngineLogModel::SetWrapColumns(int columns)
{
    columns = std::max(0, columns);
    if (columns == wrap_columns_)
    {
        return false;
    }
    wrap_columns_ = columns;
    rows_.clear();
    for (const Entry &entry : lines_)
    {
        AppendRows(entry);
    }
    return true;
}

void EngineLogModel::AppendRows(const Entry &entry)
{
    bool is_first = true;
    for (const auto &[start, length] : WrapLine(entry.text, wrap_columns_, entry.indent))
    {
        rows_.push_back({entry.serial, start, length, !is_first});
        is_first = false;
    }
}

std::size_t EngineLogModel::RowCount() const
{
    return rows_.size();
}

std::string EngineLogModel::CellText(std::size_t row, std::size_t column) const
{
    static_cast<void>(column);
    if (row >= rows_.size())
    {
        return {};
    }
    const Row         &part  = rows_[row];
    const Entry       &entry = EntryOf(part);
    const std::string &text  = entry.text;
    if (!part.is_continuation)
    {
        return text.substr(part.start, part.length);
    }
    const int indent = std::clamp(entry.indent, 0, std::max(wrap_columns_, kNarrowestRow) / 2);
    return std::string(static_cast<std::size_t>(indent), ' ') + text.substr(part.start, part.length);
}

std::optional<reckless::gfx::Color> EngineLogModel::CellColour(std::size_t row, std::size_t column) const
{
    static_cast<void>(column);
    if (row >= rows_.size())
    {
        return std::nullopt;
    }
    switch (EntryOf(rows_[row]).direction)
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

void EngineLog::SetWrapping(bool is_wrapping)
{
    is_wrapping_ = is_wrapping;
    Fit();
}

int EngineLog::FittedHeight(int room) const
{
    const int row = list_.style().row_height;
    return std::max(1, room / row) * row;
}

void EngineLog::Fit()
{
    int columns = 0;
    if (is_wrapping_ && list_.width() > 0)
    {
        // The rows' room, the scroll bar's taken whether it shows or not, so a line wrapped to fit never makes the bar
        // appear and the lines wrap again; the face's advance measured on ten characters, as the window draws them.
        const auto &style = list_.style();
        const int   room  = list_.width() - 2 * style.padding - style.scroll_bar_width - 1;
        const auto *host  = list_.host();
        const float advance =
            host != nullptr ? host->TextWidth(style.font, "0000000000") / 10.0f : 0.6f * style.font.height;
        columns = advance > 0.0f ? std::max(1, static_cast<int>(static_cast<float>(room) / advance)) : 0;
    }
    if (model_.SetWrapColumns(columns))
    {
        list_.RowsChanged();
    }
}

} // namespace uci_host::app
