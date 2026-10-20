#include "NewGameDialog.h"

namespace amphitheater::app
{

const std::vector<TimeControlPreset> &TimeControlPresets()
{
    static const std::vector<TimeControlPreset> presets = {
        {"No clock", ""},          {"1 min", "60"},
        {"3 min + 2 s", "180+2"},  {"5 min + 3 s", "300+3"},
        {"10 min + 5 s", "600+5"}, {"15 min + 10 s", "900+10"},
        {"30 min", "1800"},        {"40 moves in 90 min, then 30 min + 30 s", "40/5400,1800+30"},
        {"Custom", nullptr},
    };
    return presets;
}

NewGameDialog::NewGameDialog(const std::vector<EngineEntry> &engines, bool can_start_from_shown)
    : DialogFrame("New game", 560, 420), engines_(engines), white_label_("White"), black_label_("Black"),
      time_label_("Time control"), custom_(FieldStyle()), start_label_("Start from"), note_({}, true)
{
    std::vector<std::string> players{"You, at the board"};
    for (const EngineEntry &engine : engines_)
    {
        players.push_back(engine.DisplayName());
    }
    white_.SetChoices(players, 0);
    black_.SetChoices(players, engines_.empty() ? 0 : 1);
    std::vector<std::string> presets;
    for (const TimeControlPreset &preset : TimeControlPresets())
    {
        presets.push_back(preset.label);
    }
    time_.SetChoices(presets, 3);
    custom_.SetText("300+3");
    custom_.SetVisible(false);
    time_.on_change = [this](int index) {
        custom_.SetVisible(TimeControlPresets()[static_cast<std::size_t>(index)].text == nullptr);
        Resized(); // the rows under it move up or down
    };
    start_.SetChoices({"The starting position", "The position shown"}, 0);
    start_.SetEnabled(can_start_from_shown);
    note_.SetText(engines_.empty() ? "No engine is configured: add one under Engines > Manage engines."
                                   : "Times are minutes and seconds; a custom one reads 300+3 or 40/5400,1800+30.");
    for (reckless::view::View *view :
         std::initializer_list<reckless::view::View *>{&white_label_, &white_, &black_label_, &black_, &time_label_,
                                                       &time_, &custom_, &start_label_, &start_, &note_})
    {
        AddChild(*view);
    }
    AddButton("Start", [this] { Start(); });
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

void NewGameDialog::LayOutContent(reckless::gfx::RectI area)
{
    const int label_width = 130;
    const int row         = 46;
    int       y           = area.y() + 8;
    const int x           = area.x() + label_width;
    const int w           = area.width() - label_width;
    for (auto [label, control] : {std::pair<Caption *, reckless::view::View *>{&white_label_, &white_},
                                  {&black_label_, &black_},
                                  {&time_label_, &time_}})
    {
        label->SetBounds({area.x(), y, label_width, 34});
        control->SetBounds({x, y, w, 34});
        y += row;
    }
    // The custom time control's line takes a row only while it shows, so a preset leaves no gap.
    if (custom_.visible())
    {
        custom_.SetBounds({x, y, w, 32});
        y += row;
    }
    start_label_.SetBounds({area.x(), y, label_width, 34});
    start_.SetBounds({x, y, w, 34});
    y += row;
    note_.SetBounds({area.x(), y, area.width(), std::max(22, area.bottom() - y)});
}

std::optional<PlaySettings> NewGameDialog::Settings(std::string *error) const
{
    const auto refuse = [error](std::string why) -> std::optional<PlaySettings> {
        if (error != nullptr)
        {
            *error = std::move(why);
        }
        return std::nullopt;
    };
    PlaySettings settings;
    const auto   side = [this](int index) {
        SideSpec spec;
        if (index > 0 && index <= static_cast<int>(engines_.size()))
        {
            spec.is_human = false;
            spec.engine   = engines_[static_cast<std::size_t>(index - 1)];
        }
        return spec;
    };
    settings.white                  = side(white_.current());
    settings.black                  = side(black_.current());
    const TimeControlPreset &preset = TimeControlPresets()[static_cast<std::size_t>(std::max(0, time_.current()))];
    const std::string        text   = preset.text != nullptr ? preset.text : custom_.text();
    if (!text.empty())
    {
        auto control = chess::TimeControl::Parse(text);
        if (!control)
        {
            return refuse("The time control '" + text + "' cannot be read: " + control.ErrorMessage());
        }
        settings.time_control = *control;
    }
    settings.event = settings.white.is_human || settings.black.is_human ? "Game at the board" : "Engine game";
    return settings;
}

bool NewGameDialog::StartsFromShown() const
{
    return start_.IsEnabled() && start_.current() == 1;
}

void NewGameDialog::Start()
{
    std::string error;
    const auto  settings = Settings(&error);
    if (!settings)
    {
        note_.SetText(error);
        note_.SetError(true);
        return;
    }
    if (on_start)
    {
        on_start(*settings, StartsFromShown());
    }
}

} // namespace amphitheater::app
