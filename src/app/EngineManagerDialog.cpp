#include "EngineManagerDialog.h"

#include "Theme.h"
#include "uci/Engine.h"

#include "ListStyles.h"

#include <algorithm>
#include <charconv>
#include <filesystem>

namespace amphitheater::app
{

namespace
{

/**
 * A whole number from a line, or none for an empty line or anything else.
 * @param text The line.
 * @return The number.
 */
std::optional<int> WholeNumber(const std::string &text)
{
    int        value = 0;
    const auto end   = text.data() + text.size();
    const auto read  = std::from_chars(text.data(), end, value);
    if (text.empty() || read.ec != std::errc{} || read.ptr != end)
    {
        return std::nullopt;
    }
    return value;
}

/** The option rows' heights, in design pixels. */
constexpr int kRowHeight = 40;

} // namespace

Handshake RunHandshake(uci::EngineConfig config)
{
    Handshake   result;
    uci::Engine engine(std::move(config));
    const auto  status = engine.Start();
    if (!status)
    {
        result.error = status.ErrorMessage();
        return result;
    }
    result.is_ok   = true;
    result.name    = engine.Name();
    result.author  = engine.Author();
    result.options = engine.Options();
    engine.Quit();
    return result;
}

OptionRow::OptionRow(const uci::EngineOption &option, const std::string &value, bool is_set)
    : option_(option), name_(option.name), note_({}, true)
{
    AddChild(name_);
    switch (option_.type)
    {
    case uci::OptionType::kCheck:
        check_ = std::make_unique<CheckBox>(std::string());
        check_->SetChecked(value == "true");
        check_->on_change = [this](bool is_checked) { Tell(is_checked ? "true" : "false"); };
        AddChild(*check_);
        break;
    case uci::OptionType::kCombo: {
        combo_      = std::make_unique<DropDown>();
        int current = 0;
        for (std::size_t index = 0; index < option_.choices.size(); ++index)
        {
            if (option_.choices[index] == value)
            {
                current = static_cast<int>(index);
            }
        }
        combo_->SetChoices(option_.choices, current);
        combo_->on_change = [this](int index) { Tell(option_.choices[static_cast<std::size_t>(index)]); };
        AddChild(*combo_);
        break;
    }
    case uci::OptionType::kButton:
        button_ = MakePlate(
            "At every start",
            [this] {
                if (on_change)
                {
                    on_change(option_.name,
                              button_->on() ? std::optional<std::string>(std::string()) : std::optional<std::string>());
                }
            },
            true);
        button_->SetOn(is_set);
        button_->tooltip = "Press this button every time the engine starts.";
        AddChild(*button_);
        break;
    case uci::OptionType::kSpin:
    case uci::OptionType::kString:
        field_ = std::make_unique<reckless::view::TextField>(FieldStyle());
        field_->SetText(value);
        field_->on_commit = [this](const std::string &text) { Tell(text); };
        AddChild(*field_);
        if (option_.type == uci::OptionType::kSpin && option_.minimum && option_.maximum)
        {
            note_.SetText(std::to_string(*option_.minimum) + " to " + std::to_string(*option_.maximum));
        }
        break;
    }
    AddChild(note_);
}

void OptionRow::Tell(const std::string &value)
{
    if (const auto refusal = option_.Validate(value))
    {
        note_.SetText(*refusal);
        note_.SetError(true);
        return;
    }
    note_.SetError(false);
    if (option_.type == uci::OptionType::kSpin && option_.minimum && option_.maximum)
    {
        note_.SetText(std::to_string(*option_.minimum) + " to " + std::to_string(*option_.maximum));
    }
    if (on_change)
    {
        on_change(option_.name,
                  value == option_.default_value ? std::optional<std::string>() : std::optional<std::string>(value));
    }
}

void OptionRow::Resized()
{
    const int w      = width();
    const int h      = height();
    const int column = std::min(220, w / 3);
    name_.SetBounds({0, 0, column - 8, h});
    const int x = column;
    if (check_)
    {
        check_->SetBounds({x, (h - 28) / 2, 40, 28});
    }
    if (combo_)
    {
        combo_->SetBounds({x, (h - 32) / 2, std::min(280, w - x), 32});
    }
    if (button_)
    {
        button_->SetBounds({x, (h - 30) / 2, 170, 30});
    }
    if (field_)
    {
        const int field_width = option_.type == uci::OptionType::kSpin ? 140 : std::min(360, w - x);
        field_->SetBounds({x, (h - 30) / 2, field_width, 30});
        note_.SetBounds({x + field_width + 12, 0, std::max(0, w - x - field_width - 12), h});
    }
    else
    {
        note_.SetBounds({w, 0, 0, h});
    }
}

void ScrollPane::SetRows(std::vector<std::pair<reckless::view::View *, int>> rows)
{
    for (auto &[view, height] : rows_)
    {
        RemoveChild(*view);
    }
    rows_   = std::move(rows);
    scroll_ = 0;
    for (auto &[view, height] : rows_)
    {
        AddChild(*view);
    }
    Resized();
    Repaint();
}

int ScrollPane::ContentHeight() const
{
    int total = 0;
    for (const auto &[view, height] : rows_)
    {
        total += height;
    }
    return total;
}

void ScrollPane::SetScroll(int offset)
{
    const int held = std::clamp(offset, 0, std::max(0, ContentHeight() - height()));
    if (held != scroll_)
    {
        scroll_ = held;
        Resized();
        Repaint();
    }
}

void ScrollPane::Resized()
{
    int y = -scroll_;
    for (auto &[view, row_height] : rows_)
    {
        view->SetBounds({0, y, width() - 10, row_height});
        view->SetVisible(y + row_height > 0 && y < height());
        y += row_height;
    }
}

void ScrollPane::PaintOverChildren(reckless::gfx::Canvas &c)
{
    const int content = ContentHeight();
    if (content <= height() || height() <= 0)
    {
        return;
    }
    const float h     = static_cast<float>(height());
    const float thumb = std::max(24.0f, h * h / static_cast<float>(content));
    const float y     = (h - thumb) * static_cast<float>(scroll_) / static_cast<float>(content - height());
    c.SetColor(theme::kRule);
    c.FillRoundedRect(reckless::gfx::RectF{static_cast<float>(width()) - 5.0f, y, 4.0f, thumb}, 2.0f);
}

bool ScrollPane::MouseWheel(const reckless::view::MouseEvent &e)
{
    const int before = scroll_;
    SetScroll(scroll_ - static_cast<int>(e.wheel_y));
    return scroll_ != before || ContentHeight() > height();
}

std::size_t EngineManagerDialog::EnginesModel::RowCount() const
{
    return catalogue_.engines.size();
}

std::string EngineManagerDialog::EnginesModel::CellText(std::size_t row, std::size_t column) const
{
    const EngineEntry &entry = catalogue_.engines[row];
    if (column == 0)
    {
        return entry.DisplayName();
    }
    return entry.IsBundled() ? (entry.author.empty() ? "bundled" : entry.author + " (bundled)") : entry.author;
}

namespace
{

/** @return The engine list's look: the panel's rows in the small face. */
reckless::view::ListView::Style EnginesStyle()
{
    auto style              = reckless::panel::ListViewStyle(theme::Lists(), fonts::Small().WithHeight(17.0f));
    style.header_background = theme::kPanel;
    style.stripe            = theme::kRow;
    style.grid              = theme::kRule;
    style.selection         = theme::kSelect;
    return style;
}

} // namespace

EngineManagerDialog::EngineManagerDialog(EngineCatalogue catalogue)
    : DialogFrame("Engines", 980, 660), catalogue_(std::move(catalogue)), model_(catalogue_), list_(EnginesStyle()),
      program_({}, true), author_({}, true), name_label_("Name"), name_(FieldStyle()), args_label_("Arguments"),
      arguments_(FieldStyle()), ponder_("Ponder: think on the opponent's time"),
      analyse_("Analyse: one of the engines Analyse runs"), depth_label_("Depth limit"), depth_(FieldStyle()),
      book_label_("Own book"), book_({}, true), book_depth_label_("Book depth"), book_depth_(FieldStyle()),
      options_label_("Options"), note_({}, true)
{
    list_.SetModel(&model_);
    theme::AccentScrollBar(list_);
    list_.SetColumns({{"Engine", 0, 1.0f}, {"Author", 0, 1.0f}});
    list_.SetSelectionMode(reckless::view::ListView::SelectionMode::kSingle);
    list_.on_selection_changed = [this] {
        if (const auto row = list_.selection().First(); row && static_cast<int>(*row) != selected_)
        {
            TakeLines();
            Select(static_cast<int>(*row));
        }
    };
    add_             = MakePlate("Add...", [this] {
        reckless::view::FileRequest request;
        request.title = "Choose an engine's program";
        ChooseFile(request, [this](const std::string &path) {
            if (!path.empty())
            {
                AddEngine(path);
            }
        });
    });
    remove_          = MakePlate("Remove", [this] { RemoveSelected(); });
    detect_          = MakePlate("Detect", [this] { Detect(); });
    choose_book_     = MakePlate("Choose...", [this] {
        reckless::view::FileRequest request;
        request.title    = "Choose a Polyglot book";
        request.patterns = {"*.bin"};
        ChooseFile(request, [this](const std::string &path) {
            if (!path.empty())
            {
                SetBook(path);
            }
        });
    });
    clear_book_      = MakePlate("Clear", [this] { SetBook({}); });
    add_->tooltip    = "Add an engine: choose its program, and its handshake fills in its name and options.";
    detect_->tooltip = "Start the engine again and read its name, author and options afresh.";
    depth_.SetText({});
    ponder_.on_change = [this](bool is_on) {
        if (EngineEntry *entry = Selected())
        {
            entry->config.does_ponder = is_on;
        }
    };
    analyse_.on_change = [this](bool is_on) {
        if (EngineEntry *entry = Selected())
        {
            entry->is_analysing = is_on;
        }
    };
    for (reckless::view::View *view : std::initializer_list<reckless::view::View *>{&list_,
                                                                                    add_.get(),
                                                                                    remove_.get(),
                                                                                    detect_.get(),
                                                                                    &program_,
                                                                                    &author_,
                                                                                    &name_label_,
                                                                                    &name_,
                                                                                    &args_label_,
                                                                                    &arguments_,
                                                                                    &ponder_,
                                                                                    &analyse_,
                                                                                    &depth_label_,
                                                                                    &depth_,
                                                                                    &book_label_,
                                                                                    &book_,
                                                                                    choose_book_.get(),
                                                                                    clear_book_.get(),
                                                                                    &book_depth_label_,
                                                                                    &book_depth_,
                                                                                    &options_label_,
                                                                                    &options_,
                                                                                    &note_})
    {
        AddChild(*view);
    }
    AddButton("Save", [this] { Save(); });
    AddButton("Cancel", [this] {
        if (on_close)
        {
            on_close();
        }
    });
    on_accept = [this] { Save(); };
    on_cancel = [this] {
        if (on_close)
        {
            on_close();
        }
    };
    list_.RowsChanged();
    Select(catalogue_.engines.empty() ? -1 : 0);
}

EngineEntry *EngineManagerDialog::Selected()
{
    return selected_ >= 0 && selected_ < static_cast<int>(catalogue_.engines.size())
               ? &catalogue_.engines[static_cast<std::size_t>(selected_)]
               : nullptr;
}

void EngineManagerDialog::LayOutContent(reckless::gfx::RectI area)
{
    const int list_width = 300;
    list_.SetBounds({area.x(), area.y() + 4, list_width, area.height() - 50});
    const int plates_y = area.bottom() - 40;
    add_->SetBounds({area.x(), plates_y, 90, 32});
    remove_->SetBounds({area.x() + 98, plates_y, 96, 32});
    detect_->SetBounds({area.x() + 202, plates_y, 96, 32});

    const int x     = area.x() + list_width + 24;
    const int w     = area.right() - x;
    const int label = 120;
    int       y     = area.y() + 4;
    program_.SetBounds({x, y, w, 22});
    y += 22;
    author_.SetBounds({x, y, w, 22});
    y += 30;
    name_label_.SetBounds({x, y, label, 32});
    name_.SetBounds({x + label, y, w - label, 32});
    y += 40;
    args_label_.SetBounds({x, y, label, 32});
    arguments_.SetBounds({x + label, y, w - label, 32});
    y += 40;
    ponder_.SetBounds({x, y, w / 2, 30});
    analyse_.SetBounds({x + w / 2, y, w / 2, 30});
    y += 38;
    depth_label_.SetBounds({x, y, label, 32});
    depth_.SetBounds({x + label, y, 90, 32});
    book_depth_label_.SetBounds({x + label + 110, y, 110, 32});
    book_depth_.SetBounds({x + label + 220, y, 90, 32});
    y += 40;
    book_label_.SetBounds({x, y, label, 32});
    book_.SetBounds({x + label, y, w - label - 210, 32});
    choose_book_.get()->SetBounds({area.right() - 200, y, 110, 32});
    clear_book_.get()->SetBounds({area.right() - 82, y, 82, 32});
    y += 42;
    options_label_.SetBounds({x, y, w, 24});
    y += 26;
    note_.SetBounds({x, area.bottom() - 24, w, 24});
    options_.SetBounds({x, y, w, std::max(40, area.bottom() - 30 - y)});
}

void EngineManagerDialog::Select(int index)
{
    selected_ = index >= 0 && index < static_cast<int>(catalogue_.engines.size()) ? index : -1;
    if (selected_ >= 0)
    {
        list_.Select(static_cast<std::size_t>(selected_));
    }
    ShowSelected();
}

void EngineManagerDialog::ShowSelected()
{
    std::vector<std::pair<reckless::view::View *, int>> rows;
    rows_.clear();
    EngineEntry *entry = Selected();
    for (reckless::view::View *view : std::initializer_list<reckless::view::View *>{
             &name_, &arguments_, &ponder_, &analyse_, &depth_, &book_depth_, choose_book_.get(), clear_book_.get(),
             remove_.get(), detect_.get()})
    {
        view->SetEnabled(entry != nullptr);
    }
    if (entry == nullptr)
    {
        program_.SetText(catalogue_.engines.empty() ? "No engine yet: Add... chooses one's program." : "");
        author_.SetText({});
        name_.SetText({});
        arguments_.SetText({});
        book_.SetText({});
        options_.SetRows({});
        return;
    }
    program_.SetText(entry->config.executable);
    std::string about = entry->author.empty() ? "" : "by " + entry->author;
    if (entry->IsBundled())
    {
        const std::string version = entry->bundled_version.empty() ? std::string() : " " + entry->bundled_version;
        about += std::string(about.empty() ? "" : " - ") + "bundled with the host" + version +
                 "; Remove takes it off the list for good";
    }
    author_.SetText(about);
    name_.SetText(entry->config.name.empty() ? entry->engine_name : entry->config.name);
    arguments_.SetText(JoinArguments(entry->config.arguments));
    ponder_.SetChecked(entry->config.does_ponder);
    analyse_.SetChecked(entry->is_analysing);
    depth_.SetText(entry->depth ? std::to_string(*entry->depth) : std::string());
    book_.SetText(entry->book_path.empty() ? "none" : entry->book_path);
    book_depth_.SetText(entry->book_depth > 0 ? std::to_string(entry->book_depth) : std::string());
    for (const uci::EngineOption &option : entry->declared)
    {
        if (option.name == "Ponder")
        {
            continue; // the box above sets it
        }
        auto row = std::make_unique<OptionRow>(option, entry->OptionValue(option.name), entry->HasOption(option.name));
        row->on_change = [this](const std::string &name, std::optional<std::string> value) {
            if (EngineEntry *selected = Selected())
            {
                if (value)
                {
                    selected->SetOption(name, *value);
                }
                else
                {
                    selected->ClearOption(name);
                }
            }
        };
        rows.emplace_back(row.get(), kRowHeight);
        rows_.push_back(std::move(row));
    }
    options_.SetRows(std::move(rows));
    options_label_.SetText(entry->declared.empty() ? "Options: none read yet - Detect runs the engine's handshake"
                                                   : "Options");
}

void EngineManagerDialog::TakeLines()
{
    EngineEntry *entry = Selected();
    if (entry == nullptr)
    {
        return;
    }
    const std::string name  = name_.text();
    entry->config.name      = name == entry->engine_name ? std::string() : name;
    entry->config.arguments = SplitArguments(arguments_.text());
    const auto depth        = WholeNumber(depth_.text());
    entry->depth            = depth && *depth > 0 ? depth : std::nullopt;
    const auto book_depth   = WholeNumber(book_depth_.text());
    entry->book_depth       = book_depth && *book_depth > 0 ? *book_depth : 0;
    for (const auto &row : rows_)
    {
        if (reckless::view::TextField *field = row->field())
        {
            const std::string value = field->text();
            if (!row->option().Validate(value))
            {
                if (value == row->option().default_value)
                {
                    entry->ClearOption(row->option().name);
                }
                else
                {
                    entry->SetOption(row->option().name, value);
                }
            }
        }
    }
    list_.Repaint();
}

void EngineManagerDialog::SetBook(const std::string &path)
{
    if (EngineEntry *entry = Selected())
    {
        entry->book_path = path;
        book_.SetText(path.empty() ? "none" : path);
    }
}

void EngineManagerDialog::AddEngine(const std::string &path)
{
    if (IsDetecting())
    {
        return;
    }
    uci::EngineConfig config;
    config.executable = path;
    adding_path_      = path;
    detecting_.reset();
    handshake_ = std::async(std::launch::async, RunHandshake, config);
    note_.SetError(false);
    note_.SetText("Starting " + std::filesystem::path(path).filename().string() + " to read its options...");
}

void EngineManagerDialog::Detect()
{
    EngineEntry *entry = Selected();
    if (entry == nullptr || IsDetecting())
    {
        return;
    }
    TakeLines();
    detecting_ = selected_;
    handshake_ = std::async(std::launch::async, RunHandshake, entry->config);
    note_.SetError(false);
    note_.SetText("Starting " + entry->DisplayName() + " to read its options...");
}

void EngineManagerDialog::RemoveSelected()
{
    if (selected_ < 0 || IsDetecting())
    {
        return;
    }
    catalogue_.Remove(static_cast<std::size_t>(selected_)); // a bundled one is not seeded again
    list_.RowsChanged();
    Select(std::min(selected_, static_cast<int>(catalogue_.engines.size()) - 1));
}

void EngineManagerDialog::Frame()
{
    if (!handshake_.valid() || handshake_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    {
        return;
    }
    const Handshake result = handshake_.get();
    if (!result.is_ok)
    {
        note_.SetText("The engine did not answer: " + result.error);
        note_.SetError(true);
        detecting_.reset();
        return;
    }
    EngineEntry *entry = nullptr;
    if (detecting_ && *detecting_ < static_cast<int>(catalogue_.engines.size()))
    {
        entry = &catalogue_.engines[static_cast<std::size_t>(*detecting_)];
    }
    else
    {
        EngineEntry added;
        added.config.executable = adding_path_;
        catalogue_.engines.push_back(added);
        entry      = &catalogue_.engines.back();
        detecting_ = static_cast<int>(catalogue_.engines.size()) - 1;
    }
    entry->engine_name = result.name;
    entry->author      = result.author;
    entry->declared    = result.options;
    note_.SetError(false);
    note_.SetText(result.name + (result.author.empty() ? "" : " by " + result.author) + ": " +
                  std::to_string(result.options.size()) + " options read");
    list_.RowsChanged();
    const int index = *detecting_;
    detecting_.reset();
    selected_ = -1;
    Select(index);
}

void EngineManagerDialog::Save()
{
    TakeLines();
    if (on_save)
    {
        on_save(catalogue_);
    }
}

OptionRow *EngineManagerDialog::Row(const std::string &name)
{
    for (const auto &row : rows_)
    {
        if (row->option().name == name)
        {
            return row.get();
        }
    }
    return nullptr;
}

} // namespace amphitheater::app
