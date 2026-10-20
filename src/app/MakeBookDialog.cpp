#include "MakeBookDialog.h"

#include <cmath>
#include <filesystem>

namespace amphitheater::app
{

namespace
{

/**
 * A whole number typed into a line.
 * @param text The line's text.
 * @param least The least it may be.
 * @param most The most it may be.
 * @return The number; none for anything else.
 */
std::optional<int> WholeNumber(const std::string &text, int least, int most)
{
    const auto value = ParseDecimal(text);
    if (!value || std::floor(*value) != *value || *value < least || *value > most)
    {
        return std::nullopt;
    }
    return static_cast<int>(*value);
}

/**
 * @param path A UTF-8 path.
 * @return Its folder, in UTF-8.
 */
std::string FolderOf(const std::string &path)
{
    const std::u8string folder =
        std::filesystem::path(std::u8string(path.begin(), path.end())).parent_path().u8string();
    return {folder.begin(), folder.end()};
}

} // namespace

MakeBookDialog::MakeBookDialog(ChooseFile choose_file)
    : DialogFrame("Make a book from games", 820, 340), choose_file_(std::move(choose_file)), games_label_("Games"),
      games_(FieldStyle()), moves_label_("Moves of each side"), moves_(FieldStyle()),
      least_label_("Played in at least"), least_(FieldStyle()), games_suffix_("games"), out_label_("Save as"),
      out_(FieldStyle()), status_({}, true)
{
    moves_.SetText("7");
    least_.SetText("1");
    choose_games_  = MakePlate("PGN or zip...", [this] {
        reckless::view::FileRequest request;
        request.title    = "A PGN file of games, or a zip archive of them";
        request.patterns = {"*.pgn", "*.zip"};
        choose_file_(request, [this](const std::string &path) {
            if (!path.empty())
            {
                games_.SetText(path);
            }
        });
    });
    choose_folder_ = MakePlate("Folder...", [this] {
        reckless::view::FileRequest request;
        request.title    = "Any PGN file in the folder: every PGN file and zip archive in it and under it is read";
        request.patterns = {"*.pgn", "*.zip"};
        choose_file_(request, [this](const std::string &path) {
            if (!path.empty())
            {
                games_.SetText(FolderOf(path));
            }
        });
    });
    choose_out_    = MakePlate("Choose...", [this] {
        reckless::view::FileRequest request;
        request.title     = "Save the book";
        request.patterns  = {"*.bin"};
        request.save      = true;
        request.file_name = "book.bin";
        choose_file_(request, [this](const std::string &path) {
            if (!path.empty())
            {
                out_.SetText(path);
            }
        });
    });
    for (reckless::view::View *view : std::initializer_list<reckless::view::View *>{
             &games_label_, &games_, choose_games_.get(), choose_folder_.get(), &moves_label_, &moves_, &least_label_,
             &least_, &games_suffix_, &out_label_, &out_, choose_out_.get(), &status_})
    {
        AddChild(*view);
    }
    make_ = &AddButton("Make", [this] { MakeOrStop(); });
    AddButton("Close", [this] {
        if (on_close)
        {
            on_close();
        }
    });
    on_accept = [this] { MakeOrStop(); };
    on_cancel = [this] {
        if (on_close)
        {
            on_close();
        }
    };
    Say("A PGN file, a zip archive of PGN files, or a folder of either: each game's first moves of each side become "
        "the book's, each weighted by the games that play it.");
}

MakeBookDialog::~MakeBookDialog()
{
    stop_ = true;
    if (thread_.joinable())
    {
        thread_.join();
    }
}

void MakeBookDialog::LayOutContent(reckless::gfx::RectI area)
{
    const int label = 170;
    const int row   = 34;
    int       y     = area.y() + 4;
    games_label_.SetBounds({area.x(), y, label, row});
    games_.SetBounds({area.x() + label, y + 1, area.width() - label - 2 * 120 - 16, row - 2});
    choose_games_->SetBounds({area.right() - 2 * 120 - 8, y, 120, row});
    choose_folder_->SetBounds({area.right() - 120, y, 120, row});
    y += row + 14;
    moves_label_.SetBounds({area.x(), y, label, row});
    moves_.SetBounds({area.x() + label, y + 1, 60, row - 2});
    least_label_.SetBounds({area.x() + label + 90, y, 160, row});
    least_.SetBounds({area.x() + label + 250, y + 1, 70, row - 2});
    games_suffix_.SetBounds({area.x() + label + 332, y, 80, row});
    y += row + 14;
    out_label_.SetBounds({area.x(), y, label, row});
    out_.SetBounds({area.x() + label, y + 1, area.width() - label - 120 - 8, row - 2});
    choose_out_->SetBounds({area.right() - 120, y, 120, row});
    y += row + 18;
    status_.SetBounds({area.x(), y, area.width(), std::max(row, area.bottom() - y)});
}

void MakeBookDialog::Say(std::string text, bool is_error)
{
    status_.SetText(std::move(text));
    status_.SetError(is_error);
}

void MakeBookDialog::MakeOrStop()
{
    if (IsMaking())
    {
        stop_ = true;
        return;
    }
    Make();
}

bool MakeBookDialog::Make()
{
    if (IsMaking())
    {
        return false;
    }
    match::BookMakerSettings settings;
    settings.games = games_.text();
    settings.out   = out_.text();
    if (settings.games.empty())
    {
        Say("Choose the games: a PGN file, a zip archive, or a folder.", true);
        return false;
    }
    const auto moves = WholeNumber(moves_.text(), 1, 200);
    if (!moves)
    {
        Say("The moves of each side are a whole number from 1 to 200, not '" + moves_.text() + "'.", true);
        return false;
    }
    const auto least = WholeNumber(least_.text(), 1, 1000000);
    if (!least)
    {
        Say("The fewest games are a whole number from 1, not '" + least_.text() + "'.", true);
        return false;
    }
    if (settings.out.empty())
    {
        Say("Choose where to save the book.", true);
        return false;
    }
    settings.moves_per_side = *moves;
    settings.least_games    = *least;
    making_                 = settings.out;
    stop_                   = false;
    is_done_                = false;
    {
        const std::lock_guard lock(mutex_);
        progress_ = {};
        result_.reset();
    }
    thread_ = std::thread([this, settings] {
        auto result = match::MakeBook(settings, [this](const match::BookMakerProgress &progress) {
            {
                const std::lock_guard lock(mutex_);
                progress_ = progress;
            }
            return !stop_.load();
        });
        {
            const std::lock_guard lock(mutex_);
            result_ = std::move(result);
        }
        is_done_ = true;
    });
    make_->SetText("Stop");
    Say("Finding the games...");
    return true;
}

void MakeBookDialog::Frame()
{
    if (!IsMaking())
    {
        return;
    }
    if (is_done_)
    {
        Finish();
        return;
    }
    match::BookMakerProgress progress;
    {
        const std::lock_guard lock(mutex_);
        progress = progress_;
    }
    if (progress.sources_total > 0)
    {
        Say("File " + std::to_string(std::min(progress.sources_done + 1, progress.sources_total)) + " of " +
            std::to_string(progress.sources_total) + ", " + std::to_string(progress.games) +
            " games read: " + progress.source);
    }
}

void MakeBookDialog::Finish()
{
    thread_.join();
    make_->SetText("Make");
    std::optional<Result<match::BookMakerSummary>> result;
    {
        const std::lock_guard lock(mutex_);
        result = std::move(result_);
    }
    if (!result || !*result)
    {
        Say(std::string(stop_ ? "Stopped; no book was written." : "No book was made: ") +
                (stop_ || !result ? "" : result->ErrorMessage()),
            !stop_);
        return;
    }
    const match::BookMakerSummary &summary = **result;
    std::string text = "Made " + making_ + ": " + std::to_string(summary.moves) + " moves in " +
                       std::to_string(summary.positions) + " positions from " + std::to_string(summary.games) +
                       " games in " + std::to_string(summary.sources) + (summary.sources == 1 ? " file" : " files") +
                       ", and opened it.";
    if (!summary.problems.empty())
    {
        text += " " + std::to_string(summary.problems.size()) + " problems: " + summary.problems.front();
    }
    Say(std::move(text));
    if (on_made)
    {
        on_made(making_);
    }
}

} // namespace amphitheater::app
