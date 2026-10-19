#pragma once
/// @file Panels.h The window's smaller pieces: a section's label, a player's strip with its clock, the game's card,
/// and the status bar.

#include "Color.h"
#include "Text.h"
#include "View.h"

#include <string>
#include <utility>
#include <vector>

namespace uci_host::app
{

/** A section's label over its card: "MOVES", "GAME", "ANALYSIS", in the label font. */
class SectionLabel final : public reckless::view::View
{
  public:
    /** @param text The label, shown in capitals. */
    explicit SectionLabel(std::string text);

    /** @return The label, as shown. */
    const std::string &text() const
    {
        return text_;
    }

    /**
     * The label at the left, centred down.
     * @param c The canvas.
     */
    void Paint(reckless::gfx::Canvas &c) override;

  private:
    std::string text_; ///< The label, in capitals.
};

/** What a player's strip shows. */
struct PlayerStripState
{
    bool        is_white = true;    ///< The side: its dot's colour.
    std::string name;               ///< The engine's name, or the person's.
    std::string details;            ///< "White · engine · Threads 1, Hash 64 MB, ponder".
    std::string clock;              ///< The clock's face, "4:11.6"; empty for no clock.
    bool        is_running = false; ///< Whether this side's clock runs: its face lit amber.
    bool        has_lost   = false; ///< Whether the game ended against this side: its face in the error colour.
};

/** A player's strip above or below the board: the side's dot, the name and the details, and the clock. */
class PlayerStrip final : public reckless::view::View
{
  public:
    PlayerStrip();

    /** @param state What to show; repaints when it differs. */
    void SetState(PlayerStripState state);

    /** @return What it shows. */
    const PlayerStripState &state() const
    {
        return state_;
    }

    /** @return Where the clock's face is drawn, in the strip's coordinates. */
    reckless::gfx::RectF ClockBounds() const;

    /**
     * The card, the dot, the text and the clock.
     * @param c The canvas.
     */
    void Paint(reckless::gfx::Canvas &c) override;

  private:
    PlayerStripState state_; ///< What it shows.
};

/** One line of the game's card. */
struct CardLine
{
    std::string label;           ///< "Event".
    std::string value;           ///< "Engine match, game 7 of 20".
    bool        is_mono = false; ///< Set in the monospaced face (a FEN), and wrapped onto two lines.
};

/** The game's card: its labels down the left, their values beside them. */
class GameCard final : public reckless::view::View
{
  public:
    GameCard();

    /** @param lines The lines, in order; repaints. */
    void SetLines(std::vector<CardLine> lines);

    /** @return The lines. */
    const std::vector<CardLine> &lines() const
    {
        return lines_;
    }

    /** @return The height the lines take, in design pixels. */
    int PreferredHeight() const;

    /**
     * The card and its lines.
     * @param c The canvas.
     */
    void Paint(reckless::gfx::Canvas &c) override;

  private:
    std::vector<CardLine> lines_; ///< The lines.
};

/** The status bar across the window's foot: short phrases parted by space. */
class StatusBar final : public reckless::view::View
{
  public:
    StatusBar();

    /** @param items The phrases, in order; repaints when they differ. */
    void SetItems(std::vector<std::string> items);

    /** @return The phrases. */
    const std::vector<std::string> &items() const
    {
        return items_;
    }

    /**
     * The bar and its phrases.
     * @param c The canvas.
     */
    void Paint(reckless::gfx::Canvas &c) override;

  private:
    std::vector<std::string> items_; ///< The phrases.
};

/**
 * A clock's face for a time: minutes, seconds and tenths under an hour, hours, minutes and seconds over it.
 * @param milliseconds The time left; a negative one is shown as 0:00.0.
 * @return "4:11.6", "0:19.6", "1:02:03".
 */
std::string ClockText(long long milliseconds);

} // namespace uci_host::app
