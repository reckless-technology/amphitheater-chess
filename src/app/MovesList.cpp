#include "MovesList.h"

#include "Theme.h"

#include "ListStyles.h"

#include <algorithm>

namespace amphitheater::app
{

void MovesModel::SetMoves(std::vector<std::string> sans, int first_number, bool is_black_first,
                          std::vector<VariationRow> variations)
{
    sans_           = std::move(sans);
    first_number_   = first_number;
    is_black_first_ = is_black_first;
    variations_     = std::move(variations);
    Lay();
}

void MovesModel::Lay()
{
    // A row for each pair of moves, and under it a row for each variation that leaves the line at either of them.
    rows_.clear();
    const int   slots     = static_cast<int>(sans_.size()) + (is_black_first_ ? 1 : 0);
    const int   move_rows = (slots + 1) / 2;
    std::size_t variation = 0;
    for (int move_row = 0; move_row < move_rows; ++move_row)
    {
        rows_.push_back({move_row, -1});
        while (variation < variations_.size() &&
               (variations_[variation].move_index + (is_black_first_ ? 1 : 0)) / 2 <= move_row)
        {
            rows_.push_back({move_row, static_cast<int>(variation)});
            ++variation;
        }
    }
}

std::pair<std::size_t, std::size_t> MovesModel::CellOf(int move_index) const
{
    const int slot     = move_index + (is_black_first_ ? 1 : 0);
    const int move_row = slot / 2;
    for (std::size_t row = 0; row < rows_.size(); ++row)
    {
        if (rows_[row].variation < 0 && rows_[row].move_row == move_row)
        {
            return {row, static_cast<std::size_t>(1 + slot % 2)};
        }
    }
    return {static_cast<std::size_t>(move_row), static_cast<std::size_t>(1 + slot % 2)};
}

std::optional<int> MovesModel::MoveAt(std::size_t row, std::size_t column) const
{
    if (column < 1 || column > 2 || row >= rows_.size() || rows_[row].variation >= 0)
    {
        return std::nullopt;
    }
    const int move_index = rows_[row].move_row * 2 + static_cast<int>(column) - 1 - (is_black_first_ ? 1 : 0);
    if (move_index < 0 || move_index >= static_cast<int>(sans_.size()))
    {
        return std::nullopt;
    }
    return move_index;
}

std::optional<int> MovesModel::VariationAt(std::size_t row) const
{
    if (row >= rows_.size() || rows_[row].variation < 0)
    {
        return std::nullopt;
    }
    return variations_[static_cast<std::size_t>(rows_[row].variation)].id;
}

std::size_t MovesModel::RowCount() const
{
    return rows_.size();
}

std::string MovesModel::CellText(std::size_t row, std::size_t column) const
{
    if (row >= rows_.size())
    {
        return {};
    }
    if (rows_[row].variation >= 0)
    {
        return column == 1 ? variations_[static_cast<std::size_t>(rows_[row].variation)].text : std::string();
    }
    if (column == 0)
    {
        return std::to_string(first_number_ + rows_[row].move_row) + ".";
    }
    if (rows_[row].move_row == 0 && column == 1 && is_black_first_)
    {
        return "...";
    }
    const auto move = MoveAt(row, column);
    return move ? sans_[static_cast<std::size_t>(*move)] : std::string();
}

std::optional<reckless::gfx::Color> MovesModel::CellColour(std::size_t row, std::size_t column) const
{
    if (column == 0 ||
        (row < rows_.size() && rows_[row].move_row == 0 && rows_[row].variation < 0 && column == 1 && is_black_first_))
    {
        return theme::kMuted;
    }
    return std::nullopt;
}

std::optional<reckless::gfx::Color> MovesModel::CellBackground(std::size_t row, std::size_t column) const
{
    if (current_ply_ <= 0)
    {
        return std::nullopt;
    }
    const auto [current_row, current_column] = CellOf(current_ply_ - 1);
    if (row == current_row && column == current_column)
    {
        return theme::kSelect;
    }
    return std::nullopt;
}

bool MovesModel::PaintRow(reckless::gfx::Canvas &c, std::size_t row, reckless::gfx::RectI bounds, bool selected)
{
    static_cast<void>(selected);
    if (row >= rows_.size() || rows_[row].variation < 0)
    {
        return false;
    }
    // The variation across the moves' columns, under the number's, in the muted ink and the small face.
    const VariationRow &variation = variations_[static_cast<std::size_t>(rows_[row].variation)];
    const float         indent    = 56.0f;
    c.SetFont(fonts::Small());
    c.SetColor(theme::kMuted);
    c.DrawText(variation.text,
               reckless::gfx::RectF{static_cast<float>(bounds.x()) + indent, static_cast<float>(bounds.y()),
                                    static_cast<float>(bounds.width()) - indent - 6.0f,
                                    static_cast<float>(bounds.height())},
               reckless::gfx::Justify::kCentredLeft, true);
    return true;
}

namespace
{

/** @return The list's look: the panel's rows, the header on the same colour, the moves in the body font. */
reckless::view::ListView::Style MovesStyle()
{
    auto style              = reckless::panel::ListViewStyle(theme::Lists(), fonts::Body().WithHeight(18.0f));
    style.header_background = theme::kPanel;
    style.stripe            = theme::kRow;
    style.grid              = theme::kRule;
    style.row_height        = 30;
    style.header_height     = 34;
    style.padding           = 10;
    return style;
}

} // namespace

MovesList::MovesList() : list_(MovesStyle())
{
    list_.SetModel(&model_);
    theme::AccentScrollBar(list_);
    list_.SetColumns({{"", 46, 0.0f, reckless::gfx::Justify::kCentredRight},
                      {"White", 0, 1.0f, reckless::gfx::Justify::kCentredLeft},
                      {"Black", 0, 1.0f, reckless::gfx::Justify::kCentredLeft}});
    list_.SetSelectionMode(reckless::view::ListView::SelectionMode::kNone);
    list_.on_click = [this](std::size_t row, std::size_t column) {
        if (const auto variation = model_.VariationAt(row))
        {
            if (on_variation)
            {
                on_variation(*variation);
            }
            return;
        }
        if (const auto move = model_.MoveAt(row, column); move && on_select)
        {
            on_select(*move + 1);
        }
    };
}

void MovesList::SetMoves(std::vector<std::string> sans, int first_number, bool is_black_first, int current_ply,
                         std::vector<VariationRow> variations)
{
    model_.SetMoves(std::move(sans), first_number, is_black_first, std::move(variations));
    model_.SetCurrentPly(current_ply);
    list_.RowsChanged();
    SetCurrentPly(current_ply);
}

int MovesList::FittedHeight(int room) const
{
    const auto &style = list_.style();
    const int   rows  = std::max(1, (room - style.header_height) / style.row_height);
    return style.header_height + rows * style.row_height;
}

void MovesList::SetCurrentPly(int ply)
{
    model_.SetCurrentPly(ply);
    if (ply > 0)
    {
        list_.ScrollToRow(model_.CellOf(ply - 1).first);
    }
    list_.Repaint();
}

} // namespace amphitheater::app
