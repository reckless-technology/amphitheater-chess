#include "Evaluations.h"

#include <algorithm>
#include <cstdio>

namespace amphitheater::app
{

namespace
{

/**
 * A decimal number as a comment writes it, in the C locale whatever the process's: "0.35", "-1.2", "12".
 * @param text The text, nothing else around it.
 * @return The number, or none for text that is not one.
 */
std::optional<double> ReadDecimal(std::string_view text)
{
    double      sign  = 1.0;
    std::size_t index = 0;
    if (index < text.size() && (text[index] == '+' || text[index] == '-'))
    {
        sign = text[index] == '-' ? -1.0 : 1.0;
        ++index;
    }
    // The digits as one integer over a power of ten, so "0.35" reads as 35 / 100.0 exactly as a score of 35 does.
    long long digits    = 0;
    double    divisor   = 1.0;
    bool      has_digit = false;
    for (; index < text.size() && text[index] >= '0' && text[index] <= '9'; ++index)
    {
        digits    = digits * 10 + (text[index] - '0');
        has_digit = true;
        if (digits > 1000000000LL)
        {
            return std::nullopt;
        }
    }
    if (index < text.size() && text[index] == '.')
    {
        for (++index; index < text.size() && text[index] >= '0' && text[index] <= '9'; ++index)
        {
            if (divisor < 1e9)
            {
                digits = digits * 10 + (text[index] - '0');
                divisor *= 10.0;
            }
            has_digit = true;
        }
    }
    if (!has_digit || index != text.size())
    {
        return std::nullopt;
    }
    return sign * static_cast<double>(digits) / divisor;
}

/**
 * A mate's point from White's side.
 * @param moves The moves to mate: + White mates, - Black mates.
 * @return The point.
 */
EvalPoint MatePoint(int moves)
{
    return EvalPoint{moves >= 0 ? kMatePawns : -kMatePawns, moves};
}

/**
 * A score token from White's side: "+0.35", "-0.35", "0.35", "+M5", "-M3", "#5", "#-3".
 * @param token The token.
 * @param is_sign_required Whether a number in pawns must carry its sign (the host's comments always do; a bare "1.2"
 *        is a time there).
 * @return The point, or none.
 */
std::optional<EvalPoint> ReadScoreToken(std::string_view token, bool is_sign_required)
{
    if (token.empty())
    {
        return std::nullopt;
    }
    if (token.front() == '#')
    {
        const auto moves = ReadDecimal(token.substr(1));
        return moves ? std::optional<EvalPoint>(MatePoint(static_cast<int>(*moves))) : std::nullopt;
    }
    const bool has_sign = token.front() == '+' || token.front() == '-';
    if (has_sign && token.size() > 1 && token[1] == 'M')
    {
        const auto moves = ReadDecimal(token.substr(2));
        if (!moves)
        {
            return std::nullopt;
        }
        return MatePoint(static_cast<int>(token.front() == '-' ? -*moves : *moves));
    }
    if (is_sign_required && !has_sign)
    {
        return std::nullopt;
    }
    const auto pawns = ReadDecimal(token);
    return pawns ? std::optional<EvalPoint>(EvalPoint{*pawns, std::nullopt}) : std::nullopt;
}

/**
 * The text up to the first of a set of ends.
 * @param text The text.
 * @param ends The characters that end it.
 * @return The token.
 */
std::string_view TokenOf(std::string_view text, std::string_view ends)
{
    const std::size_t end = text.find_first_of(ends);
    return text.substr(0, end == std::string_view::npos ? text.size() : end);
}

} // namespace

EvalPoint EvalPointOf(const uci::Score &score, chess::Color side_to_move)
{
    const uci::Score white = uci::WhitesView(score, side_to_move);
    if (white.kind == uci::Score::Kind::kMate)
    {
        if (white.value == 0)
        {
            // Mate in 0: the side to move is mated.
            return EvalPoint{side_to_move == chess::kWhite ? -kMatePawns : kMatePawns, 0};
        }
        return MatePoint(white.value);
    }
    return EvalPoint{white.value / 100.0, std::nullopt};
}

std::string EvalText(const EvalPoint &point)
{
    if (point.mate)
    {
        return "#" + std::to_string(*point.mate);
    }
    char text[32];
    std::snprintf(text, sizeof text, "%+.2f", point.pawns);
    return text;
}

std::string EvalCommand(const EvalPoint &point)
{
    if (point.mate)
    {
        return *point.mate == 0 ? std::string() : "[%eval #" + std::to_string(*point.mate) + "]";
    }
    char text[48];
    std::snprintf(text, sizeof text, "[%%eval %.2f]", point.pawns);
    return text;
}

std::vector<EvalSeries> SeriesOfNotes(const std::vector<match::MoveNote> &notes, std::size_t ply_count,
                                      const std::array<std::string, 2> &names, const std::array<bool, 2> &is_engine)
{
    std::array<EvalSeries, 2> sides;
    for (int side = 0; side < 2; ++side)
    {
        sides[side].name = names[side];
        sides[side].points.assign(ply_count, std::nullopt);
    }
    for (std::size_t ply = 0; ply < notes.size() && ply < ply_count; ++ply)
    {
        const match::MoveNote &note = notes[ply];
        if (note.score)
        {
            // The mover was the side to move in the position its engine searched.
            sides[note.mover].points[ply] = EvalPointOf(*note.score, note.mover);
        }
    }
    std::vector<EvalSeries> series;
    for (int side = 0; side < 2; ++side)
    {
        if (is_engine[side])
        {
            series.push_back(std::move(sides[side]));
        }
    }
    return series;
}

std::optional<EvalPoint> EvalPointOfComment(std::string_view comment)
{
    if (const std::size_t eval = comment.find("[%eval"); eval != std::string_view::npos)
    {
        std::string_view rest = comment.substr(eval + 6);
        rest.remove_prefix(std::min(rest.find_first_not_of(' '), rest.size()));
        return ReadScoreToken(TokenOf(rest, " ,]"), false);
    }
    comment.remove_prefix(std::min(comment.find_first_not_of(" \t\r\n"), comment.size()));
    return ReadScoreToken(TokenOf(comment, "/ ,\t\r\n"), true);
}

std::vector<EvalSeries> SeriesOfPgn(const chess::PgnGame &pgn)
{
    const std::size_t count    = pgn.moves.size();
    bool              has_eval = false;
    for (const chess::PgnMove &move : pgn.moves)
    {
        has_eval |= move.comment.find("[%eval") != std::string::npos;
    }
    if (has_eval)
    {
        EvalSeries series{"Evaluation", std::vector<std::optional<EvalPoint>>(count)};
        for (std::size_t ply = 0; ply < count; ++ply)
        {
            if (pgn.moves[ply].comment.find("[%eval") != std::string::npos)
            {
                series.points[ply] = EvalPointOfComment(pgn.moves[ply].comment);
            }
        }
        return {std::move(series)};
    }
    std::array<EvalSeries, 2> sides;
    std::array<bool, 2>       has_points{};
    for (int side = 0; side < 2; ++side)
    {
        const std::string tag = pgn.Tag(side == chess::kWhite ? "White" : "Black");
        sides[side].name      = tag.empty() || tag == "?" ? (side == chess::kWhite ? "White" : "Black") : tag;
        sides[side].points.assign(count, std::nullopt);
    }
    chess::Color mover = pgn.start.SideToMove();
    for (std::size_t ply = 0; ply < count; ++ply)
    {
        if (const auto point = EvalPointOfComment(pgn.moves[ply].comment))
        {
            sides[mover].points[ply] = point;
            has_points[mover]        = true;
        }
        mover = chess::EnemyOf(mover);
    }
    std::vector<EvalSeries> series;
    for (int side = 0; side < 2; ++side)
    {
        if (has_points[side])
        {
            series.push_back(std::move(sides[side]));
        }
    }
    return series;
}

} // namespace amphitheater::app
