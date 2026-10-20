#pragma once
/// @file TestSuiteDialog.h Engines > Test suite: an EPD test suite's positions in a list - each one's name, the move
/// it asks for (bm) or forbids (am), the engine's move and whether it solved the position - run against a configured
/// engine for a time or a depth on each, the list following the run and the score under it. A position pressed twice
/// is shown on the board.

#include "Controls.h"
#include "EngineCatalogue.h"
#include "match/TestSuite.h"

#include "ListView.h"
#include "TextField.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace amphitheater::app
{

/** The suite's positions as the list shows them, with each one's outcome once it is run. */
class TestSuiteModel final : public reckless::view::ListModel
{
  public:
    /** @param positions The suite; every outcome cleared. */
    void SetPositions(std::vector<match::TestPosition> positions);

    /** @param outcome A position's outcome. */
    void SetOutcome(const match::TestOutcome &outcome);

    /** Every outcome cleared, for another run. */
    void ClearOutcomes();

    /** @return The suite. */
    const std::vector<match::TestPosition> &positions() const
    {
        return positions_;
    }

    /** @return Each position's outcome, by its index; none for one not run yet. */
    const std::vector<std::optional<match::TestOutcome>> &outcomes() const
    {
        return outcomes_;
    }

    /** @return How many positions have been run. */
    int RunCount() const;

    /** @return How many were solved. */
    int SolvedCount() const;

    /**
     * @param position A position.
     * @return What it asks for: "bm Qg6" or "am Qxd8+".
     */
    static std::string Expected(const match::TestPosition &position);

    std::size_t                         RowCount() const override;
    std::string                         CellText(std::size_t row, std::size_t column) const override;
    std::optional<reckless::gfx::Color> CellColour(std::size_t row, std::size_t column) const override;

  private:
    std::vector<match::TestPosition>               positions_; ///< The suite.
    std::vector<std::optional<match::TestOutcome>> outcomes_;  ///< Each position's outcome.
};

/** The test suite's dialog. */
class TestSuiteDialog final : public DialogFrame
{
  public:
    /**
     * @param engines The configured engines, offered to run the suite with.
     * @param name The suite's file name, for the title.
     * @param positions The suite.
     * @param skipped How many of its lines could not be read.
     */
    TestSuiteDialog(const std::vector<EngineEntry> &engines, const std::string &name,
                    std::vector<match::TestPosition> positions, std::size_t skipped);

    std::function<void()>                                on_close; ///< Closed.
    std::function<void(const chess::Position &position)> on_show;  ///< A position pressed twice.

    /** Run, or stop the run under way: Run's button. */
    void RunOrStop();

    /**
     * Run the suite against the chosen engine, or say why not.
     * @return Whether it started.
     */
    bool Run();

    /** @return Whether a run is under way. */
    bool IsRunning() const
    {
        return runner_ != nullptr;
    }

    /** The run's outcomes taken in, and its end. */
    void Frame() override;

    /** @return The score line under the list: "Solved 12 of 20 (60.0 %)". */
    const std::string &summary() const
    {
        return summary_.text();
    }

    /** @return The list's model. */
    const TestSuiteModel &model() const
    {
        return model_;
    }

    /** @return The list. */
    reckless::view::ListView &list()
    {
        return list_;
    }

    /** @return The engine's list. */
    DropDown &engine()
    {
        return engine_;
    }

    /** @return The seconds a position's line. */
    reckless::view::TextField &seconds()
    {
        return seconds_;
    }

    /** @return The depth's line: empty for a time. */
    reckless::view::TextField &depth()
    {
        return depth_;
    }

  protected:
    void LayOutContent(reckless::gfx::RectI area) override;

  private:
    /** The score line set from the outcomes so far. */
    void Summarise();

    std::vector<EngineEntry>                engines_;       ///< The engines offered.
    std::size_t                             skipped_;       ///< The suite's lines that could not be read.
    TestSuiteModel                          model_;         ///< The positions and their outcomes.
    Caption                                 engine_label_;  ///< "Engine".
    DropDown                                engine_;        ///< The engine.
    Caption                                 seconds_label_; ///< "Seconds".
    reckless::view::TextField               seconds_;       ///< The seconds each position is searched.
    Caption                                 depth_label_;   ///< "Or depth".
    reckless::view::TextField               depth_;         ///< A depth instead, or empty.
    std::unique_ptr<reckless::view::Button> run_;           ///< Run, or Stop while a run is under way.
    reckless::view::ListView                list_;          ///< The positions.
    Caption                                 summary_;       ///< The score, or what is wrong.
    std::unique_ptr<match::TestSuiteRunner> runner_;        ///< The run under way.
    std::string                             engine_name_;   ///< The engine running, for the score line.
};

} // namespace amphitheater::app
