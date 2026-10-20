#include "GameAnalysisDialog.h"

#include <charconv>
#include <cmath>

namespace amphitheater::app
{

namespace
{

/**
 * @param text A line's text.
 * @return The number it holds, whole or decimal; none for anything else.
 */
std::optional<double> NumberOf(const std::string &text)
{
    double     value  = 0.0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size() || !std::isfinite(value))
    {
        return std::nullopt;
    }
    return value;
}

} // namespace

GameAnalysisDialog::GameAnalysisDialog(const std::vector<EngineEntry> &engines)
    : DialogFrame("Analyse the game", 560, 400), engines_(engines), engine_label_("Engine"),
      seconds_label_("Seconds a move"), seconds_(FieldStyle()), depth_label_("Or a depth"), depth_(FieldStyle()),
      mark_("Mark inaccuracies, mistakes and blunders (?!, ?, ??)"),
      lines_("Add the engine's line where a move was a mistake or a blunder"), note_({}, true)
{
    std::vector<std::string> names;
    int                      first_analyser = 0;
    for (std::size_t index = 0; index < engines_.size(); ++index)
    {
        names.push_back(engines_[index].DisplayName());
        if (first_analyser == 0 && engines_[index].is_analysing && index > 0 && !engines_[0].is_analysing)
        {
            first_analyser = static_cast<int>(index);
        }
    }
    engine_.SetChoices(names, first_analyser);
    seconds_.SetText("1");
    mark_.SetChecked(true);
    lines_.SetChecked(true);
    note_.SetText("Every position of the line shown is searched in turn; a depth, when given, is searched instead of "
                  "the time.");
    for (reckless::view::View *view : std::initializer_list<reckless::view::View *>{
             &engine_label_, &engine_, &seconds_label_, &seconds_, &depth_label_, &depth_, &mark_, &lines_, &note_})
    {
        AddChild(*view);
    }
    AddButton("Analyse", [this] { Start(); });
    AddButton("Cancel", [this] {
        if (on_close)
        {
            on_close();
        }
    });
    on_accept = [this] { Start(); };
    on_cancel = [this] {
        if (on_close)
        {
            on_close();
        }
    };
}

void GameAnalysisDialog::LayOutContent(reckless::gfx::RectI area)
{
    const int label_width = 150;
    const int row         = 46;
    int       y           = area.y() + 8;
    const int x           = area.x() + label_width;
    const int w           = area.width() - label_width;
    engine_label_.SetBounds({area.x(), y, label_width, 34});
    engine_.SetBounds({x, y, w, 34});
    y += row;
    seconds_label_.SetBounds({area.x(), y, label_width, 34});
    seconds_.SetBounds({x, y, 100, 32});
    y += row;
    depth_label_.SetBounds({area.x(), y, label_width, 34});
    depth_.SetBounds({x, y, 100, 32});
    y += row;
    mark_.SetBounds({area.x(), y, area.width(), 30});
    y += 36;
    lines_.SetBounds({area.x(), y, area.width(), 30});
    y += 44;
    note_.SetBounds({area.x(), y, area.width(), std::max(22, area.bottom() - y)});
}

std::optional<GameAnalysisChoice> GameAnalysisDialog::Choice(std::string *error) const
{
    const auto refuse = [error](std::string why) -> std::optional<GameAnalysisChoice> {
        if (error != nullptr)
        {
            *error = std::move(why);
        }
        return std::nullopt;
    };
    const int index = engine_.current();
    if (index < 0 || index >= static_cast<int>(engines_.size()))
    {
        return refuse("No engine is configured: add one under Engines > Manage engines.");
    }
    GameAnalysisChoice choice;
    choice.engine_name     = engines_[static_cast<std::size_t>(index)].DisplayName();
    choice.settings.engine = engines_[static_cast<std::size_t>(index)].config;
    choice.does_mark_moves = mark_.checked();
    choice.does_add_lines  = lines_.checked();
    if (const std::string depth = depth_.text(); !depth.empty())
    {
        const auto value = NumberOf(depth);
        if (!value || *value < 1.0 || *value > 99.0 || std::floor(*value) != *value)
        {
            return refuse("The depth is a whole number from 1 to 99, not '" + depth + "'.");
        }
        choice.settings.depth = static_cast<int>(*value);
        return choice;
    }
    const auto seconds = NumberOf(seconds_.text());
    if (!seconds || *seconds < 0.05 || *seconds > 3600.0)
    {
        return refuse("The seconds a move are a number from 0.05 to 3600, not '" + seconds_.text() + "'.");
    }
    choice.settings.move_time = chess::Milliseconds(static_cast<long long>(std::lround(*seconds * 1000.0)));
    return choice;
}

void GameAnalysisDialog::Start()
{
    std::string error;
    const auto  choice = Choice(&error);
    if (!choice)
    {
        note_.SetText(error);
        note_.SetError(true);
        return;
    }
    if (on_start)
    {
        on_start(*choice);
    }
}

} // namespace amphitheater::app
