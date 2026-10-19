#include "TimeControl.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <utility>

namespace uci_host::chess
{

std::optional<Milliseconds> ParseSeconds(std::string_view text)
{
    if (text.empty())
    {
        return std::nullopt;
    }
    const auto             point    = text.find('.');
    const std::string_view whole    = text.substr(0, point);
    std::string_view       fraction = point == std::string_view::npos ? std::string_view{} : text.substr(point + 1);
    if ((whole.empty() && fraction.empty()) || fraction.size() > 3 ||
        (point != std::string_view::npos && fraction.empty()))
    {
        return std::nullopt;
    }
    std::int64_t seconds = 0;
    if (!whole.empty())
    {
        const auto result = std::from_chars(whole.data(), whole.data() + whole.size(), seconds);
        if (result.ec != std::errc{} || result.ptr != whole.data() + whole.size() || seconds < 0 ||
            seconds > 1'000'000'000)
        {
            return std::nullopt;
        }
    }
    std::int64_t millis = 0;
    for (std::size_t index = 0; index < 3; ++index)
    {
        millis *= 10;
        if (index < fraction.size())
        {
            if (fraction[index] < '0' || fraction[index] > '9')
            {
                return std::nullopt;
            }
            millis += fraction[index] - '0';
        }
    }
    return Milliseconds{seconds * 1000 + millis};
}

namespace
{

/**
 * Read a time as seconds or minutes:seconds.
 * @param text The text.
 * @param does_allow_minutes Whether "m:ss" is read (not in PGN, whose colon separates sessions).
 * @return The time, or nothing.
 */
std::optional<Milliseconds> ParseTime(std::string_view text, bool does_allow_minutes)
{
    const auto colon = text.find(':');
    if (colon == std::string_view::npos || !does_allow_minutes)
    {
        return ParseSeconds(text);
    }
    const auto minutes = ParseSeconds(text.substr(0, colon));
    const auto seconds = ParseSeconds(text.substr(colon + 1));
    if (!minutes || !seconds || minutes->count() % 1000 != 0)
    {
        return std::nullopt;
    }
    return *minutes * 60 + *seconds;
}

/**
 * Read a positive whole number.
 * @param text The text.
 * @return The number, or nothing.
 */
std::optional<int> ParseMoves(std::string_view text)
{
    int        value  = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size() || value <= 0)
    {
        return std::nullopt;
    }
    return value;
}

/**
 * Read one session, "[moves/]time[+increment]".
 * @param text The session's text.
 * @param does_allow_minutes Whether times may be minutes:seconds.
 * @return The session, or nothing.
 */
std::optional<TimeControlSession> ParseSession(std::string_view text, bool does_allow_minutes)
{
    TimeControlSession session;
    const auto         slash = text.find('/');
    if (slash != std::string_view::npos)
    {
        const auto moves = ParseMoves(text.substr(0, slash));
        if (!moves)
        {
            return std::nullopt;
        }
        session.moves = *moves;
        text          = text.substr(slash + 1);
    }
    const auto plus = text.find('+');
    const auto time = ParseTime(text.substr(0, plus), does_allow_minutes);
    if (!time)
    {
        return std::nullopt;
    }
    session.time = *time;
    if (plus != std::string_view::npos)
    {
        const auto increment = ParseTime(text.substr(plus + 1), does_allow_minutes);
        if (!increment)
        {
            return std::nullopt;
        }
        session.increment = *increment;
    }
    return session;
}

/**
 * A session as text, "[moves/]seconds[+increment]".
 * @param session The session.
 * @return The text.
 */
std::string SessionText(const TimeControlSession &session)
{
    std::string text;
    if (session.moves > 0)
    {
        text += std::to_string(session.moves) + '/';
    }
    text += SecondsText(session.time);
    if (session.increment.count() > 0)
    {
        text += '+' + SecondsText(session.increment);
    }
    return text;
}

} // namespace

std::string SecondsText(Milliseconds time)
{
    const std::int64_t millis = time.count();
    std::string        text   = std::to_string(millis / 1000);
    std::int64_t       rest   = std::llabs(millis % 1000);
    if (rest != 0)
    {
        std::string fraction = std::to_string(1000 + rest).substr(1);
        while (fraction.back() == '0')
        {
            fraction.pop_back();
        }
        if (millis < 0 && millis / 1000 == 0)
        {
            text = "-0";
        }
        text += '.' + fraction;
    }
    return text;
}

Result<TimeControl> TimeControl::Parse(std::string_view text)
{
    TimeControl control;
    if (text == "inf" || text == "infinite")
    {
        return control;
    }
    std::size_t start = 0;
    while (true)
    {
        const auto comma   = text.find(',', start);
        const auto session = ParseSession(text.substr(start, comma - start), true);
        if (!session)
        {
            return Error{"'" + std::string(text) +
                         "' is not a time control: sessions of [moves/]time[+increment], separated by commas"};
        }
        if (!control.sessions.empty() && control.sessions.back().moves == 0)
        {
            return Error{"'" + std::string(text) + "': a session after one that lasts the rest of the game"};
        }
        control.sessions.push_back(*session);
        if (comma == std::string_view::npos)
        {
            return control;
        }
        start = comma + 1;
    }
}

Result<TimeControl> TimeControl::ParsePgnTag(std::string_view text)
{
    TimeControl control;
    if (text == "-" || text == "?" || text.empty())
    {
        return control;
    }
    if (text.front() == '*')
    {
        return Error{"sandclock time controls ('" + std::string(text) + "') are not supported"};
    }
    if (text.size() > 5 && text.substr(text.size() - 5) == "/move")
    {
        const auto move_time = ParseSeconds(text.substr(0, text.size() - 5));
        if (!move_time)
        {
            return Error{"'" + std::string(text) + "' is not a PGN time control"};
        }
        control.move_time = *move_time;
        return control;
    }
    std::size_t start = 0;
    while (true)
    {
        const auto colon   = text.find(':', start);
        const auto session = ParseSession(text.substr(start, colon - start), false);
        if (!session || (!control.sessions.empty() && control.sessions.back().moves == 0))
        {
            return Error{"'" + std::string(text) + "' is not a PGN time control"};
        }
        control.sessions.push_back(*session);
        if (colon == std::string_view::npos)
        {
            return control;
        }
        start = colon + 1;
    }
}

std::string TimeControl::ToString() const
{
    std::string text;
    for (const auto &session : sessions)
    {
        text += (text.empty() ? "" : ",") + SessionText(session);
    }
    const auto add = [&](const std::string &part) { text += (text.empty() ? "" : " ") + part; };
    if (move_time)
    {
        add("st=" + SecondsText(*move_time));
    }
    if (depth)
    {
        add("depth=" + std::to_string(*depth));
    }
    if (nodes)
    {
        add("nodes=" + std::to_string(*nodes));
    }
    return text.empty() ? "inf" : text;
}

std::string TimeControl::ToPgnTag() const
{
    if (!sessions.empty())
    {
        std::string text;
        for (const auto &session : sessions)
        {
            text += (text.empty() ? "" : ":") + SessionText(session);
        }
        return text;
    }
    if (move_time)
    {
        return SecondsText(*move_time) + "/move";
    }
    return "-";
}

Clock::Clock(TimeControl control) : control_(std::move(control))
{
    if (!control_.sessions.empty())
    {
        remaining_ = control_.sessions.front().time;
    }
}

const TimeControlSession *Clock::CurrentSession() const
{
    if (control_.sessions.empty())
    {
        return nullptr;
    }
    return &control_.sessions[std::min<std::size_t>(session_index_, control_.sessions.size() - 1)];
}

Milliseconds Clock::Increment() const
{
    const auto *session = CurrentSession();
    return session ? session->increment : Milliseconds{0};
}

int Clock::MovesToGo() const
{
    const auto *session = CurrentSession();
    return session && session->moves > 0 ? session->moves - session_moves_ : 0;
}

std::optional<Milliseconds> Clock::TimeAvailable() const
{
    std::optional<Milliseconds> available;
    if (!control_.sessions.empty())
    {
        available = remaining_;
    }
    if (control_.move_time)
    {
        available = available ? std::min(*available, *control_.move_time) : *control_.move_time;
    }
    return available;
}

bool Clock::Spend(Milliseconds elapsed, Milliseconds margin)
{
    if (has_flag_fallen_)
    {
        return false;
    }
    ++moves_made_;
    if (control_.move_time && elapsed > *control_.move_time + margin)
    {
        has_flag_fallen_ = true;
    }
    const auto *session = CurrentSession();
    if (session == nullptr)
    {
        return !has_flag_fallen_;
    }
    remaining_ -= elapsed;
    if (remaining_ < -margin)
    {
        has_flag_fallen_ = true;
    }
    if (has_flag_fallen_)
    {
        return false;
    }
    remaining_ += session->increment;
    if (session->moves > 0 && ++session_moves_ == session->moves)
    {
        session_moves_ = 0;
        if (session_index_ + 1 < static_cast<int>(control_.sessions.size()))
        {
            ++session_index_;
        }
        remaining_ += CurrentSession()->time;
    }
    return true;
}

} // namespace uci_host::chess
