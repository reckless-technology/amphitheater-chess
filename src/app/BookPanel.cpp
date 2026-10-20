#include "BookPanel.h"

#include "Theme.h"

#include "ListStyles.h"

#include "chess/Notation.h"

#include <algorithm>
#include <cstdio>

namespace amphitheater::app
{

namespace
{

constexpr int kMoveWidth   = 84; ///< The move's column.
constexpr int kWeightWidth = 76; ///< The weight's column, at the right.

/** @return The list's look: the moves list's rows, the header on the same colour. */
reckless::view::ListView::Style BookStyle()
{
    auto style              = reckless::panel::ListViewStyle(theme::Lists(), fonts::Body().WithHeight(18.0f));
    style.header_background = theme::kPanel;
    style.stripe            = theme::kRow;
    style.grid              = theme::kRule;
    style.row_height        = 26;
    style.header_height     = 30;
    style.padding           = 10;
    return style;
}

} // namespace

std::vector<BookMove> BookMovesOf(const chess::PolyglotBook &book, const chess::Position &position)
{
    std::vector<BookMove> moves;
    long long             total = 0;
    for (const auto &[move, weight] : book.Moves(position))
    {
        moves.push_back({move, chess::MoveToSan(position, move), weight, 0.0});
        total += weight;
    }
    for (BookMove &move : moves)
    {
        move.share = total > 0 ? static_cast<double>(move.weight) / static_cast<double>(total) : 0.0;
    }
    std::stable_sort(moves.begin(), moves.end(),
                     [](const BookMove &a, const BookMove &b) { return a.weight > b.weight; });
    return moves;
}

std::size_t BookModel::RowCount() const
{
    return moves_.size();
}

std::string BookModel::CellText(std::size_t row, std::size_t column) const
{
    if (row >= moves_.size())
    {
        return {};
    }
    const BookMove &move = moves_[row];
    switch (column)
    {
    case 0:
        return move.san;
    case 1: {
        char text[16];
        std::snprintf(text, sizeof text, "%.1f %%", 100.0 * move.share);
        return text;
    }
    case 2:
        return std::to_string(move.weight);
    default:
        return {};
    }
}

bool BookModel::PaintRow(reckless::gfx::Canvas &c, std::size_t row, reckless::gfx::RectI bounds, bool selected)
{
    static_cast<void>(selected);
    if (row >= moves_.size())
    {
        return false;
    }
    using reckless::gfx::Justify;
    using reckless::gfx::RectF;
    const BookMove &move   = moves_[row];
    const float     x      = static_cast<float>(bounds.x());
    const float     y      = static_cast<float>(bounds.y());
    const float     width  = static_cast<float>(bounds.width());
    const float     height = static_cast<float>(bounds.height());
    const float     pad    = 10.0f;

    c.SetFont(fonts::Body());
    c.SetColor(theme::kInk);
    c.DrawText(move.san, RectF{x + pad, y, kMoveWidth - pad, height}, Justify::kCentredLeft, true);

    // The share: its bar across the middle column, the heaviest move's full, and its figure at the bar's right end.
    const float share_left  = x + kMoveWidth + pad;
    const float share_width = std::max(0.0f, width - kMoveWidth - kWeightWidth - 2.0f * pad);
    const float figure      = 62.0f;
    const float bar_room    = std::max(0.0f, share_width - figure - 8.0f);
    const float bar_height  = 8.0f;
    c.SetColor(theme::kRule);
    c.FillRoundedRect(RectF{share_left, y + (height - bar_height) / 2.0f, bar_room, bar_height}, 3.0f);
    c.SetColor(theme::kSeriesBlue);
    c.FillRoundedRect(RectF{share_left, y + (height - bar_height) / 2.0f,
                            std::max(bar_height, bar_room * static_cast<float>(move.share)), bar_height},
                      3.0f);
    c.SetFont(fonts::Small());
    c.SetColor(theme::kMuted);
    c.DrawText(CellText(row, 1), RectF{share_left + bar_room, y, figure + 8.0f, height}, Justify::kCentredRight, false);
    c.DrawText(CellText(row, 2), RectF{x + width - kWeightWidth, y, kWeightWidth - pad, height}, Justify::kCentredRight,
               false);
    return true;
}

BookPanel::BookPanel() : list_(BookStyle())
{
    list_.SetModel(&model_);
    theme::AccentScrollBar(list_);
    list_.SetColumns({{"Move", kMoveWidth, 0.0f, reckless::gfx::Justify::kCentredLeft},
                      {"Share", 0, 1.0f, reckless::gfx::Justify::kCentredLeft},
                      {"Weight", kWeightWidth, 0.0f, reckless::gfx::Justify::kCentredRight}});
    list_.SetSelectionMode(reckless::view::ListView::SelectionMode::kNone);
    list_.on_click = [this](std::size_t row, std::size_t) {
        if (row < model_.moves().size() && on_move)
        {
            on_move(model_.moves()[row].move);
        }
    };
}

int BookPanel::FittedHeight(int room) const
{
    const auto &style = list_.style();
    const int   rows  = std::max(1, (room - style.header_height) / style.row_height);
    return style.header_height + rows * style.row_height;
}

void BookPanel::SetMoves(std::vector<BookMove> moves)
{
    model_.SetMoves(std::move(moves));
    list_.RowsChanged();
    list_.SetScroll(0);
}

} // namespace amphitheater::app
