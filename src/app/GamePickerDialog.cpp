#include "GamePickerDialog.h"

#include "Theme.h"

#include "ListStyles.h"

namespace amphitheater::app
{

std::size_t GamesModel::RowCount() const
{
    return games_.size();
}

std::string GamesModel::CellText(std::size_t row, std::size_t column) const
{
    const chess::PgnGame &game = games_[row];
    switch (column)
    {
    case 0:
        return std::to_string(row + 1);
    case 1:
        return game.Tag("White");
    case 2:
        return game.Tag("Black");
    case 3:
        return chess::ResultText(game.result);
    case 4:
        return game.Tag("Event");
    case 5:
        return game.Tag("Date");
    default:
        return std::to_string((game.moves.size() + 1) / 2);
    }
}

namespace
{

/** @return The list's look: the panel's rows in the small face, the selection lit. */
reckless::view::ListView::Style GamesStyle()
{
    auto style              = reckless::panel::ListViewStyle(theme::Lists(), fonts::Small().WithHeight(17.0f));
    style.header_background = theme::kPanel;
    style.stripe            = theme::kRow;
    style.grid              = theme::kRule;
    style.selection         = theme::kSelect;
    return style;
}

} // namespace

GamePickerDialog::GamePickerDialog(std::vector<chess::PgnGame> games, const std::string &file_name, std::size_t skipped)
    : DialogFrame("Open a game from " + file_name, 980, 640), games_(std::move(games)), model_(games_),
      list_(GamesStyle()), note_({}, true)
{
    list_.SetModel(&model_);
    theme::AccentScrollBar(list_);
    list_.SetColumns({{"#", 60, 0.0f, reckless::gfx::Justify::kCentredRight},
                      {"White", 0, 1.2f},
                      {"Black", 0, 1.2f},
                      {"Result", 90, 0.0f},
                      {"Event", 0, 1.4f},
                      {"Date", 120, 0.0f},
                      {"Moves", 80, 0.0f, reckless::gfx::Justify::kCentredRight}});
    list_.SetSelectionMode(reckless::view::ListView::SelectionMode::kSingle);
    list_.on_double_click = [this](std::size_t row, std::size_t) { Open(row); };
    list_.on_activate     = [this](std::size_t row) { Open(row); };
    list_.RowsChanged();
    if (!games_.empty())
    {
        list_.Select(0);
    }
    note_.SetText(std::to_string(games_.size()) + (games_.size() == 1 ? " game" : " games") +
                  (skipped > 0 ? "; " + std::to_string(skipped) + " could not be read" : std::string()) +
                  ". Double-click a game, or select it and press Open.");
    AddChild(list_);
    AddChild(note_);
    AddButton("Open", [this] {
        if (const auto row = list_.selection().First())
        {
            Open(*row);
        }
    });
    AddButton("Cancel", [this] {
        if (on_close)
        {
            on_close();
        }
    });
    on_accept = [this] {
        if (const auto row = list_.selection().First())
        {
            Open(*row);
        }
    };
    on_cancel = [this] {
        if (on_close)
        {
            on_close();
        }
    };
}

void GamePickerDialog::Open(std::size_t index)
{
    if (index < games_.size() && on_open)
    {
        on_open(games_[index]);
    }
}

void GamePickerDialog::LayOutContent(reckless::gfx::RectI area)
{
    list_.SetBounds({area.x(), area.y() + 4, area.width(), area.height() - 40});
    note_.SetBounds({area.x(), area.bottom() - 30, area.width(), 26});
}

} // namespace amphitheater::app
