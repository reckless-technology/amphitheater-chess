#pragma once
/// @file NewGameDialog.h Game > New game: who plays each side - the person at the window or any configured engine -
/// the time control, and where the game starts.

#include "Controls.h"
#include "EngineCatalogue.h"
#include "PlaySession.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace amphitheater::app
{

/** A time control a person picks from the dialog's list. */
struct TimeControlPreset
{
    const char *label; ///< As the list shows it: "5 min + 3 s".
    const char *text;  ///< As TimeControl::Parse reads it: "300+3", or "" for no clock.
};

/** @return The presets the dialog offers, the last of which is "Custom", read from the line beside it. */
const std::vector<TimeControlPreset> &TimeControlPresets();

/** The new game's dialog. */
class NewGameDialog final : public DialogFrame
{
  public:
    /**
     * @param engines The configured engines, offered for either side.
     * @param can_start_from_shown Whether there is a position shown to start from other than the start.
     */
    NewGameDialog(const std::vector<EngineEntry> &engines, bool can_start_from_shown);

    std::function<void(const PlaySettings &settings, bool from_shown)> on_start; ///< Start pressed, settings sound.
    std::function<void()>                                              on_close; ///< Cancelled.

    /**
     * The settings as they stand, when they are sound.
     * @param error Why not, when not (may be null).
     * @return The settings; the start position is the caller's to fill.
     */
    std::optional<PlaySettings> Settings(std::string *error = nullptr) const;

    /** @return Whether the game starts from the position shown. */
    bool StartsFromShown() const;

    /** @return The white side's list. */
    DropDown &white()
    {
        return white_;
    }

    /** @return The black side's list. */
    DropDown &black()
    {
        return black_;
    }

    /** @return The time control's list. */
    DropDown &time()
    {
        return time_;
    }

    /** @return The custom time control's line. */
    reckless::view::TextField &custom()
    {
        return custom_;
    }

    /** @return Where the game starts from: the starting position or the one shown. */
    DropDown &start()
    {
        return start_;
    }

    /** Start pressed: the game handed on when the settings are sound, else the reason shown. */
    void Start();

  protected:
    void LayOutContent(reckless::gfx::RectI area) override;

  private:
    std::vector<EngineEntry>  engines_;     ///< The engines offered.
    Caption                   white_label_; ///< "White".
    DropDown                  white_;       ///< White's player.
    Caption                   black_label_; ///< "Black".
    DropDown                  black_;       ///< Black's player.
    Caption                   time_label_;  ///< "Time control".
    DropDown                  time_;        ///< The time control.
    reckless::view::TextField custom_;      ///< A custom time control.
    Caption                   start_label_; ///< "Start from".
    DropDown                  start_;       ///< The start position or the one shown.
    Caption                   note_;        ///< What is wrong, or a hint.
};

} // namespace amphitheater::app
