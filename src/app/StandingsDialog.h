#pragma once
/// @file StandingsDialog.h Tournament > Standings and crosstable: the tournament's standings - rank, points, games,
/// score, wins, draws, losses and Sonneborn-Berger - and its crosstable, each in the toolkit's list, followed as games
/// finish.

#include "Controls.h"
#include "match/Statistics.h"

#include "ListView.h"

#include <functional>
#include <string>
#include <vector>

namespace uci_host::app
{

/** The standings as the list shows them. */
class StandingsModel final : public reckless::view::ListModel
{
  public:
    /** @param standings The standings, best first. */
    void Set(std::vector<match::Standing> standings)
    {
        standings_ = std::move(standings);
    }

    std::size_t RowCount() const override;
    std::string CellText(std::size_t row, std::size_t column) const override;

  private:
    std::vector<match::Standing> standings_; ///< The standings.
};

/** The crosstable as the list shows it: a row an engine in the standings' order, a column an opponent. */
class CrosstableModel final : public reckless::view::ListModel
{
  public:
    /**
     * @param names Each engine's name, by index.
     * @param standings The standings, for the order.
     * @param table The crosstable.
     */
    void Set(std::vector<std::string> names, std::vector<match::Standing> standings, match::Crosstable table);

    std::size_t RowCount() const override;
    std::string CellText(std::size_t row, std::size_t column) const override;

  private:
    std::vector<std::string>     names_;     ///< The names.
    std::vector<match::Standing> standings_; ///< The order.
    match::Crosstable            table_;     ///< The scores.
};

/** The standings' and the crosstable's dialog. */
class StandingsDialog final : public DialogFrame
{
  public:
    /** What the dialog shows: the engines' names and the finished games' results, asked for each frame. */
    using Source = std::function<std::pair<std::vector<std::string>, std::vector<match::GameResultEntry>>()>;

    /** @param source Where the names and the results come from. */
    explicit StandingsDialog(Source source);

    std::function<void()> on_close; ///< Closed.

    /** Brought to the results as they stand. */
    void Frame() override;

    /** @return The standings' list. */
    reckless::view::ListView &standings()
    {
        return standings_;
    }

    /** @return The crosstable's list. */
    reckless::view::ListView &crosstable()
    {
        return crosstable_;
    }

    /** @return The standings. */
    const StandingsModel &standings_model() const
    {
        return standings_model_;
    }

    /** @return The crosstable. */
    const CrosstableModel &crosstable_model() const
    {
        return crosstable_model_;
    }

  protected:
    void LayOutContent(reckless::gfx::RectI area) override;

  private:
    Source                   source_;                                       ///< Where the results come from.
    std::size_t              shown_results_ = static_cast<std::size_t>(-1); ///< How many results are shown.
    Caption                  standings_label_;                              ///< "Standings".
    StandingsModel           standings_model_;                              ///< The standings.
    reckless::view::ListView standings_;                                    ///< Their list.
    Caption                  crosstable_label_;                             ///< "Crosstable".
    CrosstableModel          crosstable_model_;                             ///< The crosstable.
    reckless::view::ListView crosstable_;                                   ///< Its list.
};

} // namespace uci_host::app
