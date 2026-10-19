#pragma once
/// @file CompetitionDialog.h Match > New match and Tournament > New tournament: the engines and the options
/// uci-host-cli takes - the time control, games and rounds, a double round robin, repeated openings, concurrency, an
/// opening file, the adjudications and a PGN file - written as the command line's own arguments and read by its own
/// reader (cli::ParseTournamentArguments), so the window and the command line agree on every option and its checks.

#include "Controls.h"
#include "EngineCatalogue.h"
#include "cli/Cli.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace uci_host::app
{

/** The arguments a configured engine is on the command line: cmd=, name=, arg=, option.<name>=, book=, ponder. */
std::vector<std::string> EngineArguments(const EngineEntry &entry);

/** The match's or the tournament's dialog. */
class CompetitionDialog final : public DialogFrame
{
  public:
    /**
     * @param is_tournament Whether it is a tournament (any engines, a double round robin) rather than a match.
     * @param engines The configured engines.
     */
    CompetitionDialog(bool is_tournament, const std::vector<EngineEntry> &engines);

    std::function<void(const cli::TournamentOptions &options)> on_start; ///< Start pressed, the options sound.
    std::function<void()>                                      on_close; ///< Cancelled.

    /** @return The command line's arguments the dialog stands for, after the subcommand. */
    std::vector<std::string> Arguments() const;

    /**
     * The options, read by the command line's own reader.
     * @param error Why not, when not (may be null).
     * @return The options.
     */
    std::optional<cli::TournamentOptions> Options(std::string *error = nullptr) const;

    /** Start pressed: handed on when sound, else the reason shown. */
    void Start();

    /** @return Whether it is a tournament. */
    bool is_tournament() const
    {
        return is_tournament_;
    }

    /**
     * An engine's box (a tournament) or list (a match), by the engine's place.
     * @param index The engine in a tournament's list; 0 or 1 for a match's two lists.
     * @return The box, for a tournament; null otherwise.
     */
    CheckBox *EngineBox(std::size_t index);

    /** @return A match's two lists of engines. */
    DropDown &first()
    {
        return first_;
    }

    /** @return A match's second engine. */
    DropDown &second()
    {
        return second_;
    }

    /**
     * A line of the dialog by its caption.
     * @param caption "Time control", "Games", "Rounds", "Concurrency", "Max moves", "PGN file", "Openings",
     *        "Opening plies", "Arguments".
     * @return The line, or null.
     */
    reckless::view::TextField *Field(const std::string &caption);

    /**
     * A box of the dialog by its text's start.
     * @param start "Repeat", "Double", "Resign", "Draw".
     * @return The box, or null.
     */
    CheckBox *Box(const std::string &start);

    /** @return What the note at the foot says. */
    const std::string &note() const
    {
        return note_.text();
    }

  protected:
    void LayOutContent(reckless::gfx::RectI area) override;

  private:
    /** A captioned line. */
    struct Line
    {
        std::unique_ptr<Caption>                   caption; ///< Its caption.
        std::unique_ptr<reckless::view::TextField> field;   ///< The line.
        std::string                                name;    ///< Its caption's text.
    };

    /**
     * Add a captioned line.
     * @param caption Its caption.
     * @param value Its value.
     * @param tip Its tooltip.
     */
    void AddLine(const std::string &caption, const std::string &value, const std::string &tip);

    bool                                    is_tournament_;   ///< A tournament.
    std::vector<EngineEntry>                engines_;         ///< The engines.
    Caption                                 engines_label_;   ///< "Engines".
    DropDown                                first_;           ///< A match's first engine.
    DropDown                                second_;          ///< A match's second engine.
    std::vector<std::unique_ptr<CheckBox>>  engine_boxes_;    ///< A tournament's engines.
    std::vector<Line>                       lines_;           ///< The captioned lines.
    std::vector<std::unique_ptr<CheckBox>>  boxes_;           ///< The boxes.
    std::unique_ptr<reckless::view::Button> choose_openings_; ///< Choose... for the openings.
    std::unique_ptr<reckless::view::Button> choose_pgn_;      ///< Choose... for the PGN file.
    Caption                                 note_;            ///< What is wrong, or a hint.
};

} // namespace uci_host::app
