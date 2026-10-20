#include "TestSuiteDialog.h"

#include "Theme.h"

#include "ListStyles.h"

#include "chess/Notation.h"

#include <cmath>
#include <cstdio>

namespace amphitheater::app
{

namespace
{

/** @return The list's look: the panel's rows in the small face. */
reckless::view::ListView::Style TableStyle()
{
    auto style              = reckless::panel::ListViewStyle(theme::Lists(), fonts::Small().WithHeight(17.0f));
    style.header_background = theme::kPanel;
    style.stripe            = theme::kRow;
    style.grid              = theme::kRule;
    return style;
}

} // namespace

void TestSuiteModel::SetPositions(std::vector<match::TestPosition> positions)
{
    positions_ = std::move(positions);
    ClearOutcomes();
}

void TestSuiteModel::SetOutcome(const match::TestOutcome &outcome)
{
    if (outcome.index >= 0 && static_cast<std::size_t>(outcome.index) < outcomes_.size())
    {
        outcomes_[static_cast<std::size_t>(outcome.index)] = outcome;
    }
}

void TestSuiteModel::ClearOutcomes()
{
    outcomes_.assign(positions_.size(), std::nullopt);
}

int TestSuiteModel::RunCount() const
{
    int count = 0;
    for (const auto &outcome : outcomes_)
    {
        count += outcome ? 1 : 0;
    }
    return count;
}

int TestSuiteModel::SolvedCount() const
{
    int count = 0;
    for (const auto &outcome : outcomes_)
    {
        count += outcome && outcome->is_solved ? 1 : 0;
    }
    return count;
}

std::string TestSuiteModel::Expected(const match::TestPosition &position)
{
    const bool  is_best = !position.best_moves.empty();
    std::string text    = is_best ? "bm" : "am";
    for (const chess::Move move : is_best ? position.best_moves : position.avoid_moves)
    {
        text += " " + chess::MoveToSan(position.position, move);
    }
    return text;
}

std::size_t TestSuiteModel::RowCount() const
{
    return positions_.size();
}

std::string TestSuiteModel::CellText(std::size_t row, std::size_t column) const
{
    if (row >= positions_.size())
    {
        return {};
    }
    const auto &outcome = outcomes_[row];
    switch (column)
    {
    case 0:
        return std::to_string(row + 1);
    case 1:
        return positions_[row].id;
    case 2:
        return Expected(positions_[row]);
    case 3:
        return outcome ? (outcome->move.empty() ? "-" : outcome->move) : std::string();
    case 4:
        return outcome ? (outcome->is_solved ? "solved" : "failed") : std::string();
    default:
        return {};
    }
}

std::optional<reckless::gfx::Color> TestSuiteModel::CellColour(std::size_t row, std::size_t column) const
{
    if (column == 4 && row < outcomes_.size() && outcomes_[row])
    {
        return outcomes_[row]->is_solved ? theme::kPlus : theme::kMinus;
    }
    if (column == 0)
    {
        return theme::kMuted;
    }
    return std::nullopt;
}

TestSuiteDialog::TestSuiteDialog(const std::vector<EngineEntry> &engines, const std::string &name,
                                 std::vector<match::TestPosition> positions, std::size_t skipped)
    : DialogFrame("Test suite: " + name, 860, 680), engines_(engines), skipped_(skipped), engine_label_("Engine"),
      seconds_label_("Seconds"), seconds_(FieldStyle()), depth_label_("Or depth"), depth_(FieldStyle()),
      list_(TableStyle()), summary_({}, true)
{
    using reckless::gfx::Justify;
    model_.SetPositions(std::move(positions));
    std::vector<std::string> names;
    for (const EngineEntry &engine : engines_)
    {
        names.push_back(engine.DisplayName());
    }
    engine_.SetChoices(names, 0);
    seconds_.SetText("1");
    run_ = MakePlate("Run", [this] { RunOrStop(); });
    list_.SetModel(&model_);
    theme::AccentScrollBar(list_);
    list_.SetColumns({{"#", 50, 0.0f, Justify::kCentredRight},
                      {"Position", 0, 1.0f},
                      {"Expected", 200, 0.0f},
                      {"Engine", 110, 0.0f},
                      {"Result", 90, 0.0f}});
    list_.SetSelectionMode(reckless::view::ListView::SelectionMode::kSingle);
    const auto show = [this](std::size_t row) {
        if (row < model_.positions().size() && on_show)
        {
            on_show(model_.positions()[row].position);
        }
    };
    list_.on_double_click = [show](std::size_t row, std::size_t) { show(row); };
    list_.on_activate     = show;
    for (reckless::view::View *view :
         std::initializer_list<reckless::view::View *>{&engine_label_, &engine_, &seconds_label_, &seconds_,
                                                       &depth_label_, &depth_, run_.get(), &list_, &summary_})
    {
        AddChild(*view);
    }
    AddButton("Close", [this] {
        if (on_close)
        {
            on_close();
        }
    });
    on_accept = [this] { RunOrStop(); };
    on_cancel = [this] {
        if (on_close)
        {
            on_close();
        }
    };
    Summarise();
}

void TestSuiteDialog::LayOutContent(reckless::gfx::RectI area)
{
    int x = area.x();
    int y = area.y() + 4;
    engine_label_.SetBounds({x, y, 64, 34});
    engine_.SetBounds({x + 64, y, 280, 34});
    x += 64 + 280 + 18;
    seconds_label_.SetBounds({x, y, 70, 34});
    seconds_.SetBounds({x + 70, y + 1, 70, 32});
    x += 70 + 70 + 14;
    depth_label_.SetBounds({x, y, 70, 34});
    depth_.SetBounds({x + 70, y + 1, 56, 32});
    run_->SetBounds({area.right() - 100, y, 100, 34});
    y += 34 + 12;
    const int summary_height = 26;
    list_.SetBounds({area.x(), y, area.width(), std::max(80, area.bottom() - y - summary_height - 8)});
    summary_.SetBounds({area.x(), area.bottom() - summary_height, area.width(), summary_height});
}

void TestSuiteDialog::RunOrStop()
{
    if (runner_)
    {
        runner_->Stop();
        return;
    }
    Run();
}

bool TestSuiteDialog::Run()
{
    const int index = engine_.current();
    if (index < 0 || index >= static_cast<int>(engines_.size()))
    {
        summary_.SetText("No engine is configured: add one under Engines > Manage engines.");
        summary_.SetError(true);
        return false;
    }
    match::TestSuiteSettings settings;
    settings.engine = engines_[static_cast<std::size_t>(index)].config;
    if (const std::string depth = depth_.text(); !depth.empty())
    {
        const auto value = ParseDecimal(depth);
        if (!value || *value < 1.0 || *value > 99.0 || std::floor(*value) != *value)
        {
            summary_.SetText("The depth is a whole number from 1 to 99, not '" + depth + "'.");
            summary_.SetError(true);
            return false;
        }
        settings.depth = static_cast<int>(*value);
    }
    else
    {
        const auto seconds = ParseDecimal(seconds_.text());
        if (!seconds || *seconds < 0.05 || *seconds > 3600.0)
        {
            summary_.SetText("The seconds are a number from 0.05 to 3600, not '" + seconds_.text() + "'.");
            summary_.SetError(true);
            return false;
        }
        settings.move_time = chess::Milliseconds(static_cast<long long>(std::lround(*seconds * 1000.0)));
    }
    engine_name_ = engines_[static_cast<std::size_t>(index)].DisplayName();
    model_.ClearOutcomes();
    list_.RowsChanged();
    runner_ = std::make_unique<match::TestSuiteRunner>(settings, model_.positions());
    runner_->Start();
    run_->SetText("Stop");
    Summarise();
    return true;
}

void TestSuiteDialog::Frame()
{
    if (!runner_)
    {
        return;
    }
    bool has_news = false;
    while (const auto outcome = runner_->Next())
    {
        model_.SetOutcome(*outcome);
        has_news = true;
    }
    if (runner_->IsFinished())
    {
        while (const auto outcome = runner_->Next())
        {
            model_.SetOutcome(*outcome);
        }
        const std::string failure = runner_->Failure();
        runner_.reset();
        run_->SetText("Run");
        Summarise();
        if (!failure.empty())
        {
            summary_.SetText(summary_.text() + "  The run stopped: " + failure);
            summary_.SetError(true);
        }
        list_.Repaint();
        return;
    }
    if (has_news)
    {
        Summarise();
        list_.Repaint();
    }
}

void TestSuiteDialog::Summarise()
{
    const int total  = static_cast<int>(model_.positions().size());
    const int run    = model_.RunCount();
    const int solved = model_.SolvedCount();
    char      text[256];
    if (run == 0)
    {
        std::snprintf(text, sizeof text, "%d positions%s. Press Run, or a position twice to show it on the board.",
                      total, skipped_ == 0 ? "" : (", " + std::to_string(skipped_) + " lines skipped").c_str());
    }
    else
    {
        std::snprintf(text, sizeof text, "%s: solved %d of %d (%.1f %%)%s", engine_name_.c_str(), solved, run,
                      100.0 * solved / run,
                      runner_ ? (", " + std::to_string(run) + " of " + std::to_string(total) + " run").c_str() : "");
    }
    summary_.SetText(text);
    summary_.SetError(false);
}

} // namespace amphitheater::app
