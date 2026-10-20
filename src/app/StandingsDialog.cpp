#include "StandingsDialog.h"

#include "CompetitionSession.h"
#include "Theme.h"

#include "ListStyles.h"

#include <cstdio>

namespace amphitheater::app
{

std::size_t StandingsModel::RowCount() const
{
    return standings_.size();
}

std::string StandingsModel::CellText(std::size_t row, std::size_t column) const
{
    const match::Standing &standing = standings_[row];
    char                   text[32];
    switch (column)
    {
    case 0:
        return std::to_string(standing.rank);
    case 1:
        return standing.name;
    case 2:
        return PointsText(standing.score.Points());
    case 3:
        return std::to_string(standing.score.Games());
    case 4:
        std::snprintf(text, sizeof text, "%.1f %%", 100.0 * standing.score.Fraction());
        return text;
    case 5:
        return std::to_string(standing.score.wins);
    case 6:
        return std::to_string(standing.score.draws);
    case 7:
        return std::to_string(standing.score.losses);
    default:
        std::snprintf(text, sizeof text, "%.2f", standing.sonneborn_berger);
        return text;
    }
}

void CrosstableModel::Set(std::vector<std::string> names, std::vector<match::Standing> standings,
                          match::Crosstable table)
{
    names_     = std::move(names);
    standings_ = std::move(standings);
    table_     = std::move(table);
}

std::size_t CrosstableModel::RowCount() const
{
    return standings_.size();
}

std::string CrosstableModel::CellText(std::size_t row, std::size_t column) const
{
    const int engine = standings_[row].engine;
    if (column == 0)
    {
        return names_[static_cast<std::size_t>(engine)];
    }
    const std::size_t opponent_row = column - 1;
    if (opponent_row >= standings_.size())
    {
        return {};
    }
    const int opponent = standings_[opponent_row].engine;
    if (opponent == engine)
    {
        return "\xc2\xb7";
    }
    if (static_cast<std::size_t>(engine) >= table_.cells.size() ||
        static_cast<std::size_t>(opponent) >= table_.cells[static_cast<std::size_t>(engine)].size())
    {
        return {};
    }
    const match::ScoreLine &score = table_.cells[static_cast<std::size_t>(engine)][static_cast<std::size_t>(opponent)];
    return score.Games() == 0 ? std::string() : PointsText(score.Points()) + "/" + std::to_string(score.Games());
}

namespace
{

/** @return A table's look: the panel's rows in the body face, no selection. */
reckless::view::ListView::Style TableStyle()
{
    auto style              = reckless::panel::ListViewStyle(theme::Lists(), fonts::Small().WithHeight(17.0f));
    style.header_background = theme::kPanel;
    style.stripe            = theme::kRow;
    style.grid              = theme::kRule;
    return style;
}

} // namespace

StandingsDialog::StandingsDialog(Source source)
    : DialogFrame("Standings and crosstable", 1000, 680), source_(std::move(source)), standings_label_("Standings"),
      standings_(TableStyle()), crosstable_label_("Crosstable"), crosstable_(TableStyle())
{
    using reckless::gfx::Justify;
    standings_.SetModel(&standings_model_);
    theme::AccentScrollBar(standings_);
    standings_.SetColumns({{"#", 44, 0.0f, Justify::kCentredRight},
                           {"Engine", 0, 1.0f},
                           {"Points", 80, 0.0f, Justify::kCentredRight},
                           {"Games", 72, 0.0f, Justify::kCentredRight},
                           {"Score", 92, 0.0f, Justify::kCentredRight},
                           {"W", 52, 0.0f, Justify::kCentredRight},
                           {"D", 52, 0.0f, Justify::kCentredRight},
                           {"L", 52, 0.0f, Justify::kCentredRight},
                           {"SB", 80, 0.0f, Justify::kCentredRight}});
    standings_.SetSelectionMode(reckless::view::ListView::SelectionMode::kNone);
    crosstable_.SetModel(&crosstable_model_);
    theme::AccentScrollBar(crosstable_);
    crosstable_.SetSelectionMode(reckless::view::ListView::SelectionMode::kNone);
    AddChild(standings_label_);
    AddChild(standings_);
    AddChild(crosstable_label_);
    AddChild(crosstable_);
    AddButton("Close", [this] {
        if (on_close)
        {
            on_close();
        }
    });
    on_accept = [this] {
        if (on_close)
        {
            on_close();
        }
    };
    on_cancel = on_accept;
    Frame();
}

void StandingsDialog::Frame()
{
    if (!source_)
    {
        return;
    }
    const auto [names, results] = source_();
    if (results.size() == shown_results_)
    {
        return;
    }
    shown_results_       = results.size();
    const auto standings = match::ComputeStandings(names, results);
    standings_model_.Set(standings);
    standings_.RowsChanged();
    std::vector<reckless::view::ListColumn> columns{{"Engine", 0, 1.6f}};
    for (const match::Standing &standing : standings)
    {
        columns.push_back({std::to_string(standing.rank), 0, 1.0f, reckless::gfx::Justify::kCentred});
    }
    crosstable_.SetColumns(std::move(columns));
    crosstable_model_.Set(names, standings, match::Crosstable::Build(static_cast<int>(names.size()), results));
    crosstable_.RowsChanged();
    standings_.Repaint();
    crosstable_.Repaint();
}

void StandingsDialog::LayOutContent(reckless::gfx::RectI area)
{
    const int half = (area.height() - 20) / 2;
    standings_label_.SetBounds({area.x(), area.y(), area.width(), 26});
    standings_.SetBounds({area.x(), area.y() + 28, area.width(), half - 28});
    crosstable_label_.SetBounds({area.x(), area.y() + half + 10, area.width(), 26});
    crosstable_.SetBounds({area.x(), area.y() + half + 38, area.width(), area.bottom() - area.y() - half - 38});
}

} // namespace amphitheater::app
