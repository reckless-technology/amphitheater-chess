#include "CompetitionDialog.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace amphitheater::app
{

std::vector<std::string> EngineArguments(const EngineEntry &entry)
{
    std::vector<std::string> arguments{"cmd=" + entry.config.executable, "name=" + entry.DisplayName()};
    if (!entry.config.working_directory.empty())
    {
        arguments.push_back("dir=" + entry.config.working_directory);
    }
    for (const std::string &argument : entry.config.arguments)
    {
        arguments.push_back("arg=" + argument);
    }
    for (const auto &[name, value] : entry.config.options)
    {
        arguments.push_back("option." + name + "=" + value);
    }
    if (entry.depth)
    {
        arguments.push_back("depth=" + std::to_string(*entry.depth));
    }
    if (!entry.book_path.empty())
    {
        arguments.push_back("book=" + entry.book_path);
        if (entry.book_depth > 0)
        {
            arguments.push_back("bookdepth=" + std::to_string(entry.book_depth));
        }
    }
    if (entry.config.does_ponder)
    {
        arguments.push_back("ponder");
    }
    return arguments;
}

CompetitionDialog::CompetitionDialog(bool is_tournament, const std::vector<EngineEntry> &engines)
    : DialogFrame(is_tournament ? "New tournament" : "New match", 980, 680), is_tournament_(is_tournament),
      engines_(engines), engines_label_(is_tournament ? "Engines: a round robin of those ticked" : "Engines"),
      note_({}, true)
{
    std::vector<std::string> names;
    for (const EngineEntry &engine : engines_)
    {
        names.push_back(engine.DisplayName());
    }
    AddChild(engines_label_);
    if (is_tournament_)
    {
        for (const std::string &name : names)
        {
            auto box = std::make_unique<CheckBox>(name);
            box->SetChecked(true);
            AddChild(*box);
            engine_boxes_.push_back(std::move(box));
        }
    }
    else
    {
        first_.SetChoices(names, 0);
        second_.SetChoices(names, names.size() > 1 ? 1 : 0);
        AddChild(first_);
        AddChild(second_);
    }
    AddLine("Time control", "60+0.6",
            "[moves/]time[+increment], seconds or m:ss; sessions parted by commas; inf for none");
    AddLine("Games", "2", "Games each pair of engines plays per round, colours alternating");
    AddLine("Rounds", is_tournament_ ? "1" : "10", "Cycles of every engine meeting every other");
    AddLine("Concurrency", "1", "Games played at once");
    AddLine("Openings", "", "An EPD, PGN or Polyglot file of openings; empty for the starting position");
    AddLine("Opening plies", "8", "How many plies of a PGN game or a book walk each opening keeps");
    AddLine("Max moves", "", "A draw after this many moves by each side; empty for no limit");
    AddLine("PGN file", "", "Every finished game appended to this file; empty for none");
    AddLine("Arguments", "", "More of amphitheater-cli's options, as its command line takes them");
    for (const char *text : {"Repeat each opening, colours swapped", "Double round robin: every cycle twice",
                             "Resign adjudication: 3 moves at -6.00 or worse",
                             "Draw adjudication: from move 40, 8 moves "
                             "within 0.10"})
    {
        if (!is_tournament_ && std::string(text).starts_with("Double"))
        {
            continue;
        }
        auto box = std::make_unique<CheckBox>(text);
        AddChild(*box);
        boxes_.push_back(std::move(box));
    }
    Box("Repeat")->SetChecked(true);
    choose_openings_ = MakePlate("Choose...", [this] {
        reckless::view::FileRequest request;
        request.title    = "Choose an opening file";
        request.patterns = {"*.epd", "*.pgn", "*.bin"};
        ChooseFile(request, [this](const std::string &path) {
            if (!path.empty())
            {
                Field("Openings")->SetText(path);
            }
        });
    });
    choose_pgn_      = MakePlate("Choose...", [this] {
        reckless::view::FileRequest request;
        request.title     = "Append the games to a PGN file";
        request.patterns  = {"*.pgn"};
        request.save      = true;
        request.file_name = is_tournament_ ? "tournament.pgn" : "match.pgn";
        ChooseFile(request, [this](const std::string &path) {
            if (!path.empty())
            {
                Field("PGN file")->SetText(path);
            }
        });
    });
    AddChild(*choose_openings_);
    AddChild(*choose_pgn_);
    AddChild(note_);
    note_.SetText(engines_.size() < 2 ? "Two engines are needed: add them under Engines > Manage engines."
                                      : "The options are amphitheater-cli's: Arguments takes any other it has.");
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

void CompetitionDialog::AddLine(const std::string &caption, const std::string &value, const std::string &tip)
{
    Line line;
    line.name    = caption;
    line.caption = std::make_unique<Caption>(caption);
    line.field   = std::make_unique<reckless::view::TextField>(FieldStyle());
    line.field->SetText(value);
    line.field->tooltip = tip;
    AddChild(*line.caption);
    AddChild(*line.field);
    lines_.push_back(std::move(line));
}

reckless::view::TextField *CompetitionDialog::Field(const std::string &caption)
{
    for (Line &line : lines_)
    {
        if (line.name == caption)
        {
            return line.field.get();
        }
    }
    return nullptr;
}

CheckBox *CompetitionDialog::Box(const std::string &start)
{
    // The boxes in the order they were added: a match has no double round robin.
    const std::vector<std::string> starts = is_tournament_
                                                ? std::vector<std::string>{"Repeat", "Double", "Resign", "Draw"}
                                                : std::vector<std::string>{"Repeat", "Resign", "Draw"};
    for (std::size_t index = 0; index < starts.size() && index < boxes_.size(); ++index)
    {
        if (starts[index] == start)
        {
            return boxes_[index].get();
        }
    }
    return nullptr;
}

CheckBox *CompetitionDialog::EngineBox(std::size_t index)
{
    return index < engine_boxes_.size() ? engine_boxes_[index].get() : nullptr;
}

void CompetitionDialog::LayOutContent(reckless::gfx::RectI area)
{
    const int left_width = 300;
    engines_label_.SetBounds({area.x(), area.y() + 4, left_width, 26});
    int y = area.y() + 36;
    if (is_tournament_)
    {
        for (const auto &box : engine_boxes_)
        {
            box->SetBounds({area.x(), y, left_width, 30});
            y += 34;
        }
    }
    else
    {
        first_.SetBounds({area.x(), y, left_width, 34});
        second_.SetBounds({area.x(), y + 44, left_width, 34});
    }

    const int x     = area.x() + left_width + 30;
    const int w     = area.right() - x;
    const int label = 150;
    y               = area.y() + 4;
    for (Line &line : lines_)
    {
        line.caption->SetBounds({x, y, label, 32});
        const bool has_chooser = line.name == "Openings" || line.name == "PGN file";
        line.field->SetBounds({x + label, y, w - label - (has_chooser ? 120 : 0), 32});
        if (line.name == "Openings")
        {
            choose_openings_->SetBounds({area.right() - 110, y, 110, 32});
        }
        if (line.name == "PGN file")
        {
            choose_pgn_->SetBounds({area.right() - 110, y, 110, 32});
        }
        y += 38;
    }
    y += 4;
    for (const auto &box : boxes_)
    {
        box->SetBounds({x, y, w, 28});
        y += 32;
    }
    note_.SetBounds({area.x(), area.bottom() - 26, area.width(), 26});
}

std::vector<std::string> CompetitionDialog::Arguments() const
{
    std::vector<std::string> arguments;
    const auto               add_engine = [&](const EngineEntry &engine) {
        arguments.push_back("-engine");
        const auto keys = EngineArguments(engine);
        arguments.insert(arguments.end(), keys.begin(), keys.end());
    };
    if (is_tournament_)
    {
        for (std::size_t index = 0; index < engines_.size() && index < engine_boxes_.size(); ++index)
        {
            if (engine_boxes_[index]->checked())
            {
                add_engine(engines_[index]);
            }
        }
    }
    else if (engines_.size() >= 1)
    {
        add_engine(engines_[static_cast<std::size_t>(std::max(0, first_.current()))]);
        add_engine(engines_[static_cast<std::size_t>(std::max(0, second_.current()))]);
    }
    const auto value = [this](const std::string &caption) {
        for (const Line &line : lines_)
        {
            if (line.name == caption)
            {
                return line.field->text();
            }
        }
        return std::string();
    };
    const auto add_option = [&](const char *option, const std::string &text) {
        if (!text.empty())
        {
            arguments.push_back(option);
            arguments.push_back(text);
        }
    };
    add_option("-tc", value("Time control"));
    add_option("-games", value("Games"));
    add_option("-rounds", value("Rounds"));
    add_option("-concurrency", value("Concurrency"));
    add_option("-maxmoves", value("Max moves"));
    add_option("-pgnout", value("PGN file"));
    if (const std::string openings = value("Openings"); !openings.empty())
    {
        std::string extension = std::filesystem::path(openings).extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const char *format = extension == ".pgn" ? "pgn" : extension == ".bin" ? "polyglot" : "epd";
        arguments.insert(arguments.end(), {"-openings", "file=" + openings, std::string("format=") + format});
        if (const std::string plies = value("Opening plies"); !plies.empty())
        {
            arguments.push_back("plies=" + plies);
        }
    }
    const auto ticked = [this](const std::string &start) {
        CheckBox *box = const_cast<CompetitionDialog *>(this)->Box(start);
        return box != nullptr && box->checked();
    };
    if (ticked("Repeat"))
    {
        arguments.push_back("-repeat");
    }
    if (ticked("Double"))
    {
        arguments.push_back("-double");
    }
    if (ticked("Resign"))
    {
        arguments.insert(arguments.end(), {"-resign", "movecount=3", "score=600"});
    }
    if (ticked("Draw"))
    {
        arguments.insert(arguments.end(), {"-draw", "movenumber=40", "movecount=8", "score=10"});
    }
    arguments.insert(arguments.end(), {"-event", is_tournament_ ? "Engine tournament" : "Engine match"});
    const auto extra = SplitArguments(value("Arguments"));
    arguments.insert(arguments.end(), extra.begin(), extra.end());
    return arguments;
}

std::optional<cli::TournamentOptions> CompetitionDialog::Options(std::string *error) const
{
    auto options = cli::ParseTournamentArguments(Arguments(), !is_tournament_);
    if (!options)
    {
        if (error != nullptr)
        {
            *error = options.ErrorMessage();
        }
        return std::nullopt;
    }
    return std::move(*options);
}

void CompetitionDialog::Start()
{
    std::string error;
    auto        options = Options(&error);
    if (!options)
    {
        note_.SetText(error);
        note_.SetError(true);
        return;
    }
    if (on_start)
    {
        on_start(*options);
    }
}

} // namespace amphitheater::app
