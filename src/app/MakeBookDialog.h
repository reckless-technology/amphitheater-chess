#pragma once
/// @file MakeBookDialog.h File > Make book from games: an opening book made from a library of games
/// (match/BookMaker.h) - a PGN file, a zip archive of them, or a folder of either - from each game's first moves of
/// each side, saved as a Polyglot .bin and opened. The making runs on a thread of its own, its progress under the
/// fields, and stops at Stop or as the dialog closes.

#include "Controls.h"
#include "match/BookMaker.h"

#include "TextField.h"
#include "View.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace amphitheater::app
{

/** The book maker's dialog. */
class MakeBookDialog final : public DialogFrame
{
  public:
    /** A file chosen through the platform's chooser: the request, and what to do with the path (empty if none). */
    using ChooseFile =
        std::function<void(const reckless::view::FileRequest &request, std::function<void(const std::string &)>)>;

    /** @param choose_file How the dialog asks the platform for a file. */
    explicit MakeBookDialog(ChooseFile choose_file);

    /** Stops a making under way and waits for its thread. */
    ~MakeBookDialog() override;

    std::function<void()>                        on_close; ///< Closed.
    std::function<void(const std::string &book)> on_made;  ///< A book made and saved: its file, to open.

    /**
     * Make the book from what the fields say, or say why not.
     * @return Whether the making started.
     */
    bool Make();

    /** Make, or stop the making under way: the affirmative button. */
    void MakeOrStop();

    /** @return Whether a making is under way. */
    bool IsMaking() const
    {
        return thread_.joinable();
    }

    /** The making's progress shown, and its end taken in. */
    void Frame() override;

    /** @return The games' line: a PGN file, a zip archive or a folder. */
    reckless::view::TextField &games()
    {
        return games_;
    }

    /** @return The moves of each side's line. */
    reckless::view::TextField &moves()
    {
        return moves_;
    }

    /** @return The fewest games' line. */
    reckless::view::TextField &least()
    {
        return least_;
    }

    /** @return The book's file's line. */
    reckless::view::TextField &out()
    {
        return out_;
    }

    /** @return The line under the fields: what to do, the progress, what was made, or what is wrong. */
    const std::string &status() const
    {
        return status_.text();
    }

  protected:
    void LayOutContent(reckless::gfx::RectI area) override;

  private:
    /** The thread's end: its result taken in, the button back to Make. */
    void Finish();

    /** @param text The status line, as an error's or not. */
    void Say(std::string text, bool is_error = false);

    ChooseFile                              choose_file_;    ///< The platform's chooser.
    Caption                                 games_label_;    ///< "Games".
    reckless::view::TextField               games_;          ///< The library.
    std::unique_ptr<reckless::view::Button> choose_games_;   ///< A PGN file or an archive chosen.
    std::unique_ptr<reckless::view::Button> choose_folder_;  ///< A folder, chosen by a file in it.
    Caption                                 moves_label_;    ///< "Moves of each side".
    reckless::view::TextField               moves_;          ///< The moves of each side.
    Caption                                 least_label_;    ///< "In at least ... games".
    reckless::view::TextField               least_;          ///< The fewest games a move is kept for.
    Caption                                 games_suffix_;   ///< "games", after it.
    Caption                                 out_label_;      ///< "Save as".
    reckless::view::TextField               out_;            ///< The book's file.
    std::unique_ptr<reckless::view::Button> choose_out_;     ///< The book's file chosen.
    Caption                                 status_;         ///< What to do, the progress, the outcome.
    reckless::view::Button                 *make_ = nullptr; ///< Make, or Stop while making.

    std::thread                                    thread_;         ///< The making under way.
    std::atomic<bool>                              stop_{false};    ///< Set to stop it.
    std::atomic<bool>                              is_done_{false}; ///< Set as it ends.
    std::mutex                                     mutex_;          ///< Guards the two below.
    match::BookMakerProgress                       progress_;       ///< Its progress so far.
    std::optional<Result<match::BookMakerSummary>> result_;         ///< What it made, once it ends.
    std::string                                    making_;         ///< The book's file being made.
};

} // namespace amphitheater::app
