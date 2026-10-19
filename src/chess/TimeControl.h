#pragma once
/// @file TimeControl.h Time controls, and the clock that keeps one side's time under them.
///
/// A time control is any mix of: sessions of moves in a time with an increment (40/120+30: forty moves in two
/// minutes with thirty seconds added after each move; a session of no move count lasts the rest of the game; the last
/// session with a move count repeats), a fixed time per move, a depth and a node limit, or none of these (infinite).
/// The clock does the arithmetic and is told how long each move took, so it never reads a real clock itself: the
/// caller measures, and the tests feed it numbers.

#include "Result.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace uci_host::chess
{

/** Milliseconds, the unit of every time here (and of UCI's wtime, btime and movetime). */
using Milliseconds = std::chrono::milliseconds;

/** One session of a conventional time control. */
struct TimeControlSession
{
    int          moves = 0;    ///< Moves to make in the session; 0 for the rest of the game.
    Milliseconds time{0};      ///< The time the session adds to the clock.
    Milliseconds increment{0}; ///< The time added after every move made in the session.
    bool         operator==(const TimeControlSession &) const = default; ///< Sessions are equal when every field is.
};

/** A time control: sessions on a clock, a fixed time per move, a depth or a node limit, or any mix of them. */
struct TimeControl
{
    std::vector<TimeControlSession> sessions;  ///< The sessions, in order; empty for no clock.
    std::optional<Milliseconds>     move_time; ///< A fixed time for every move (UCI's movetime).
    std::optional<int>              depth;     ///< A depth limit (UCI's depth).
    std::optional<std::int64_t>     nodes;     ///< A node limit (UCI's nodes).

    /** @return Whether there is a clock to keep (sessions or a time per move). */
    bool IsTimed() const
    {
        return !sessions.empty() || move_time.has_value();
    }

    /** @return Whether nothing limits a search: no clock, no time per move, no depth and no node limit. */
    bool IsInfinite() const
    {
        return !IsTimed() && !depth && !nodes;
    }

    /**
     * Read a time control as a command line writes it: sessions separated by commas, each "[moves/]time[+increment]"
     * with the times in seconds (decimals allowed) or as minutes:seconds - "40/120+30", "60+0.6", "2:00+1",
     * "40/90,30+30" - or "inf" for none.
     * @param text The text.
     * @return The time control (sessions only), or what is wrong with the text.
     */
    static Result<TimeControl> Parse(std::string_view text);

    /**
     * Read PGN's TimeControl tag: sessions separated by colons, each "moves/seconds", "seconds" or
     * "seconds+increment" ("40/7200:3600", "300+5"); "-" for none and "?" for unknown. Sandclock ("*180") is not
     * supported.
     * @param text The tag's value.
     * @return The time control, or what is wrong with the text.
     */
    static Result<TimeControl> ParsePgnTag(std::string_view text);

    /**
     * The time control as Parse reads it: the sessions ("40/120+30,60"), "st=1" for a fixed time per move,
     * "depth=N" and "nodes=N" joined by spaces, or "inf".
     * @return The text.
     */
    std::string ToString() const;

    /**
     * The time control as PGN's TimeControl tag writes it: the sessions joined by colons, the times in seconds; a
     * fixed time per move as "seconds/move"; "-" for none.
     * @return The tag's value.
     */
    std::string ToPgnTag() const;

    bool operator==(const TimeControl &) const = default; ///< Time controls are equal when every field is.
};

/**
 * Read a decimal number of seconds ("60", "0.6", "1.25"), to the millisecond; the locale plays no part.
 * @param text The text.
 * @return The time, or nothing when the text is not a non-negative decimal of at most three places.
 */
std::optional<Milliseconds> ParseSeconds(std::string_view text);

/**
 * Seconds as the shortest decimal text that reads back to the same milliseconds: "60", "0.6", "1.25".
 * @param time A time.
 * @return The seconds.
 */
std::string SecondsText(Milliseconds time);

/** One side's clock under a time control. */
class Clock
{
  public:
    /** A clock with no time control: never runs out. */
    Clock() = default;

    /**
     * A clock at the start of a game.
     * @param control The time control.
     */
    explicit Clock(TimeControl control);

    /** @return The time control. */
    const TimeControl &Control() const
    {
        return control_;
    }

    /** @return The time left on the clock (negative once the flag has fallen); zero with no sessions. */
    Milliseconds Remaining() const
    {
        return remaining_;
    }

    /** @return The increment added after this move. */
    Milliseconds Increment() const;

    /** @return Moves until the next session's time is added; 0 when the session lasts the rest of the game. */
    int MovesToGo() const;

    /** @return How many moves this clock has made. */
    int MovesMade() const
    {
        return moves_made_;
    }

    /**
     * The longest a move may take before the flag falls: the time remaining under sessions, the time per move under
     * a fixed time, the smaller of the two under both, nothing with no clock.
     * @return The time, or nothing.
     */
    std::optional<Milliseconds> TimeAvailable() const;

    /**
     * Charge a move to the clock: subtract the time it took, judge the flag, then add the increment and, at the end of
     * a session, the next session's time.
     * @param elapsed How long the move took.
     * @param margin How far past zero (or past the time per move) the clock may go before the flag falls.
     * @return False when the flag fell; the clock then stops (no increment, no new session).
     */
    bool Spend(Milliseconds elapsed, Milliseconds margin = Milliseconds{0});

    /** @return Whether the flag has fallen. */
    bool HasFlagFallen() const
    {
        return has_flag_fallen_;
    }

  private:
    /** @return The session the clock is in. */
    const TimeControlSession *CurrentSession() const;

    TimeControl  control_;                 ///< The time control.
    Milliseconds remaining_{0};            ///< The time left.
    int          session_index_   = 0;     ///< The session the clock is in (the last one repeats).
    int          session_moves_   = 0;     ///< Moves made in the current session.
    int          moves_made_      = 0;     ///< Moves made in the game.
    bool         has_flag_fallen_ = false; ///< Whether the flag has fallen.
};

} // namespace uci_host::chess
