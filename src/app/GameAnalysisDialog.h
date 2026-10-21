#pragma once
/// @file GameAnalysisDialog.h Game > Analyse the game: the engine that goes over every position of the line shown, how
/// long it thinks on each - seconds or a depth - whether each move it judges to have cost its mover is marked (?! an
/// inaccuracy, ? a mistake, ?? a blunder), and whether the engine's best line is added as a variation where a move was
/// a mistake or a blunder.

#include "Controls.h"
#include "EngineCatalogue.h"
#include "match/GameAnalysis.h"

#include "TextField.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace amphitheater::app
{

/** What the dialog asks for. */
struct GameAnalysisChoice
{
    match::GameAnalysisSettings settings;               ///< The engine and its search.
    std::string                 engine_name;            ///< The engine, as the list names it.
    bool                        does_mark_moves = true; ///< Mark each move that cost its mover: ?!, ? or ??.
    bool                        does_add_lines  = true; ///< Add the engine's line where a move was a mistake or worse.
};

/** The game analysis's dialog. */
class GameAnalysisDialog final : public DialogFrame
{
  public:
    /** @param engines The configured engines, offered to analyse with; at least one. */
    explicit GameAnalysisDialog(const std::vector<EngineEntry> &engines);

    std::function<void(const GameAnalysisChoice &choice)> on_start; ///< Analyse pressed, the choice sound.
    std::function<void()>                                 on_close; ///< Cancelled.

    /**
     * The choice as it stands, when it is sound.
     * @param error Why not, when not (may be null).
     * @return The choice.
     */
    std::optional<GameAnalysisChoice> Choice(std::string *error = nullptr) const;

    /** Analyse pressed: the choice handed on when it is sound, else the reason shown. */
    void Start();

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

    /** @return Whether moves are marked. */
    CheckBox &mark()
    {
        return mark_;
    }

    /** @return Whether the engine's lines are added. */
    CheckBox &lines()
    {
        return lines_;
    }

  protected:
    void LayOutContent(reckless::gfx::RectI area) override;

  private:
    std::vector<EngineEntry>  engines_;       ///< The engines offered.
    Caption                   engine_label_;  ///< "Engine".
    DropDown                  engine_;        ///< The engine.
    Caption                   seconds_label_; ///< "Seconds a move".
    reckless::view::TextField seconds_;       ///< The seconds each position is searched.
    Caption                   depth_label_;   ///< "Or depth".
    reckless::view::TextField depth_;         ///< A depth instead, or empty.
    CheckBox                  mark_;          ///< Mark the moves.
    CheckBox                  lines_;         ///< Add the engine's lines.
    Caption                   note_;          ///< What is wrong, or a hint.
};

} // namespace amphitheater::app
