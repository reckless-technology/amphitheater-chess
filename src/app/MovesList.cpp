#include "MovesList.h"

#include "Theme.h"

#include "ListStyles.h"

namespace uci_host::app
{

void MovesModel::SetMoves(std::vector<std::string> sans, int first_number, bool is_black_first)
{
    sans_           = std::move(sans);
    first_number_   = first_number;
    is_black_first_ = is_black_first;
}

std::pair<std::size_t, std::size_t> MovesModel::CellOf(int move_index) const
{
    const int slot = move_index + (is_black_first_ ? 1 : 0);
    return {static_cast<std::size_t>(slot / 2), static_cast<std::size_t>(1 + slot % 2)};
}

std::optional<int> MovesModel::MoveAt(std::size_t row, std::size_t column) const
{
    if (column < 1 || column > 2)
    {
        return std::nullopt;
    }
    const int move_index = static_cast<int>(row) * 2 + static_cast<int>(column) - 1 - (is_black_first_ ? 1 : 0);
    if (move_index < 0 || move_index >= static_cast<int>(sans_.size()))
    {
        return std::nullopt;
    }
    return move_index;
}

std::size_t MovesModel::RowCount() const
{
    const std::size_t slots = sans_.size() + (is_black_first_ ? 1 : 0);
    return (slots + 1) / 2;
}

std::string MovesModel::CellText(std::size_t row, std::size_t column) const
{
    if (column == 0)
    {
        return std::to_string(first_number_ + static_cast<int>(row)) + ".";
    }
    if (row == 0 && column == 1 && is_black_first_)
    {
        return "...";
    }
    const auto move = MoveAt(row, column);
    return move ? sans_[static_cast<std::size_t>(*move)] : std::string();
}

std::optional<reckless::gfx::Color> MovesModel::CellColour(std::size_t row, std::size_t column) const
{
    if (column == 0 || (row == 0 && column == 1 && is_black_first_))
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
    style.padding           = 12;
    return style;
}

} // namespace

MovesList::MovesList() : list_(MovesStyle())
{
    list_.SetModel(&model_);
    list_.SetColumns({{"", 64, 0.0f, reckless::gfx::Justify::kCentredRight},
                      {"White", 0, 1.0f, reckless::gfx::Justify::kCentredLeft},
                      {"Black", 0, 1.0f, reckless::gfx::Justify::kCentredLeft}});
    list_.SetSelectionMode(reckless::view::ListView::SelectionMode::kNone);
    list_.on_click = [this](std::size_t row, std::size_t column) {
        if (const auto move = model_.MoveAt(row, column); move && on_select)
        {
            on_select(*move + 1);
        }
    };
}

void MovesList::SetMoves(std::vector<std::string> sans, int first_number, bool is_black_first, int current_ply)
{
    model_.SetMoves(std::move(sans), first_number, is_black_first);
    model_.SetCurrentPly(current_ply);
    list_.RowsChanged();
    SetCurrentPly(current_ply);
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

} // namespace uci_host::app
