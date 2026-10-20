#include "Statistics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace amphitheater::match
{

namespace
{

/** The two-sided 95 % quantile of the standard normal distribution. */
constexpr double kNormal95 = 1.959963984540054;

/**
 * Text padded on the right to a width.
 * @param text The text.
 * @param width The width.
 * @return The padded text (the text itself when it is wider).
 */
std::string PadRight(const std::string &text, std::size_t width)
{
    return text.size() >= width ? text : text + std::string(width - text.size(), ' ');
}

/**
 * Text padded on the left to a width.
 * @param text The text.
 * @param width The width.
 * @return The padded text.
 */
std::string PadLeft(const std::string &text, std::size_t width)
{
    return text.size() >= width ? text : std::string(width - text.size(), ' ') + text;
}

/**
 * A number with a fixed number of decimals.
 * @param value The number.
 * @param decimals How many decimals.
 * @return The text.
 */
std::string Fixed(double value, int decimals)
{
    char text[64];
    std::snprintf(text, sizeof text, "%.*f", decimals, value);
    return text;
}

/**
 * Points as a person writes them: "3", "2.5".
 * @param points Whole or half points.
 * @return The text.
 */
std::string PointsText(double points)
{
    return points == std::floor(points) ? Fixed(points, 0) : Fixed(points, 1);
}

} // namespace

double EloFromFraction(double fraction)
{
    return -400.0 * std::log10(1.0 / fraction - 1.0);
}

EloEstimate EstimateElo(const ScoreLine &score)
{
    EloEstimate estimate;
    const int   games = score.Games();
    if (games == 0)
    {
        return estimate;
    }
    estimate.draw_ratio = static_cast<double>(score.draws) / games;
    const int decisive  = score.wins + score.losses;
    if (decisive > 0)
    {
        estimate.likelihood_of_superiority =
            0.5 * (1.0 + std::erf((score.wins - score.losses) / std::sqrt(2.0 * decisive)));
    }
    const double fraction = score.Fraction();
    if (fraction <= 0.0 || fraction >= 1.0)
    {
        return estimate;
    }
    estimate.difference = EloFromFraction(fraction) + 0.0; // + 0.0 turns an even score's -0 into 0.
    // One game's score is 1, 0.5 or 0; its variance about the mean, then the mean's.
    const double variance  = (score.wins * std::pow(1.0 - fraction, 2) + score.draws * std::pow(0.5 - fraction, 2) +
                              score.losses * std::pow(fraction, 2)) /
                             games;
    const double deviation = std::sqrt(variance / games);
    const double low       = fraction - kNormal95 * deviation;
    const double high      = fraction + kNormal95 * deviation;
    if (low > 0.0 && high < 1.0)
    {
        estimate.error_margin = (EloFromFraction(high) - EloFromFraction(low)) / 2.0;
    }
    return estimate;
}

Crosstable Crosstable::Build(int engines, const std::vector<GameResultEntry> &results)
{
    Crosstable table;
    table.cells.assign(static_cast<std::size_t>(engines), std::vector<ScoreLine>(static_cast<std::size_t>(engines)));
    for (const auto &game : results)
    {
        if (game.result == chess::GameResult::kNone || game.white < 0 || game.black < 0 || game.white >= engines ||
            game.black >= engines)
        {
            continue;
        }
        ScoreLine &white = table.cells[game.white][game.black];
        ScoreLine &black = table.cells[game.black][game.white];
        switch (game.result)
        {
        case chess::GameResult::kWhiteWins:
            ++white.wins;
            ++black.losses;
            break;
        case chess::GameResult::kBlackWins:
            ++white.losses;
            ++black.wins;
            break;
        default:
            ++white.draws;
            ++black.draws;
            break;
        }
    }
    return table;
}

std::vector<Standing> ComputeStandings(const std::vector<std::string>     &names,
                                       const std::vector<GameResultEntry> &results)
{
    const int             engines = static_cast<int>(names.size());
    const Crosstable      table   = Crosstable::Build(engines, results);
    std::vector<Standing> standings(static_cast<std::size_t>(engines));
    for (int engine = 0; engine < engines; ++engine)
    {
        Standing &standing = standings[engine];
        standing.engine    = engine;
        standing.name      = names[engine];
        for (int opponent = 0; opponent < engines; ++opponent)
        {
            const ScoreLine &cell = table.cells[engine][opponent];
            standing.score.wins += cell.wins;
            standing.score.draws += cell.draws;
            standing.score.losses += cell.losses;
        }
    }
    for (int engine = 0; engine < engines; ++engine)
    {
        for (int opponent = 0; opponent < engines; ++opponent)
        {
            standings[engine].sonneborn_berger +=
                table.cells[engine][opponent].Points() * standings[opponent].score.Points();
        }
    }
    std::sort(standings.begin(), standings.end(), [](const Standing &left, const Standing &right) {
        if (left.score.Points() != right.score.Points())
        {
            return left.score.Points() > right.score.Points();
        }
        if (left.sonneborn_berger != right.sonneborn_berger)
        {
            return left.sonneborn_berger > right.sonneborn_berger;
        }
        return left.name < right.name;
    });
    for (std::size_t index = 0; index < standings.size(); ++index)
    {
        const bool is_tied    = index > 0 && standings[index].score.Points() == standings[index - 1].score.Points() &&
                                standings[index].sonneborn_berger == standings[index - 1].sonneborn_berger;
        standings[index].rank = is_tied ? standings[index - 1].rank : static_cast<int>(index) + 1;
    }
    return standings;
}

std::string FormatStandings(const std::vector<Standing> &standings)
{
    std::size_t name_width = 4;
    for (const auto &standing : standings)
    {
        name_width = std::max(name_width, standing.name.size());
    }
    std::string text = PadLeft("Rank", 4) + "  " + PadRight("Name", name_width) + "  " + PadLeft("Points", 6) + "  " +
                       PadLeft("Games", 5) + "  " + PadLeft("Score", 6) + "  " + PadLeft("W", 4) + "  " +
                       PadLeft("D", 4) + "  " + PadLeft("L", 4) + "  " + PadLeft("SB", 7) + "\n";
    for (const auto &standing : standings)
    {
        text += PadLeft(std::to_string(standing.rank), 4) + "  " + PadRight(standing.name, name_width) + "  " +
                PadLeft(PointsText(standing.score.Points()), 6) + "  " +
                PadLeft(std::to_string(standing.score.Games()), 5) + "  " +
                PadLeft(Fixed(100.0 * standing.score.Fraction(), 1) + "%", 6) + "  " +
                PadLeft(std::to_string(standing.score.wins), 4) + "  " +
                PadLeft(std::to_string(standing.score.draws), 4) + "  " +
                PadLeft(std::to_string(standing.score.losses), 4) + "  " +
                PadLeft(Fixed(standing.sonneborn_berger, 2), 7) + "\n";
    }
    return text;
}

std::string FormatCrosstable(const std::vector<std::string> &names, const std::vector<Standing> &standings,
                             const Crosstable &table)
{
    std::size_t name_width = 4;
    for (const auto &name : names)
    {
        name_width = std::max(name_width, name.size());
    }
    // A cell is as wide as its widest entry, and at least as wide as its column's number.
    std::size_t cell_width = 3;
    for (const auto &row : table.cells)
    {
        for (const auto &cell : row)
        {
            cell_width = std::max(cell_width, (PointsText(cell.Points()) + "/" + std::to_string(cell.Games())).size());
        }
    }
    std::string text = PadLeft("", 3) + "  " + PadRight("Name", name_width);
    for (std::size_t column = 0; column < standings.size(); ++column)
    {
        text += "  " + PadLeft(std::to_string(column + 1), cell_width);
    }
    text += '\n';
    for (std::size_t row = 0; row < standings.size(); ++row)
    {
        const int engine = standings[row].engine;
        text += PadLeft(std::to_string(row + 1), 3) + "  " + PadRight(names[engine], name_width);
        for (const auto &column : standings)
        {
            const ScoreLine  &cell  = table.cells[engine][column.engine];
            const std::string entry = column.engine == engine ? "."
                                      : cell.Games() == 0
                                          ? "-"
                                          : PointsText(cell.Points()) + "/" + std::to_string(cell.Games());
            text += "  " + PadLeft(entry, cell_width);
        }
        text += '\n';
    }
    return text;
}

std::string FormatMatchScore(const std::string &first, const std::string &second, const ScoreLine &score)
{
    std::string       text = "Score of " + first + " vs " + second + ": " + std::to_string(score.wins) + " - " +
                             std::to_string(score.losses) + " - " + std::to_string(score.draws) + "  [" +
                             Fixed(score.Fraction(), 3) + "] " + std::to_string(score.Games()) + "\n";
    const EloEstimate elo  = EstimateElo(score);
    text += "Elo difference: ";
    if (elo.difference)
    {
        text += Fixed(*elo.difference, 1) + " +/- " + (elo.error_margin ? Fixed(*elo.error_margin, 1) : "nan");
    }
    else
    {
        text += score.Games() == 0 ? "nan" : (score.Fraction() >= 1.0 ? "inf" : "-inf");
    }
    text += ", LOS: " + Fixed(100.0 * elo.likelihood_of_superiority, 1) +
            " %, DrawRatio: " + Fixed(100.0 * elo.draw_ratio, 1) + " %\n";
    return text;
}

} // namespace amphitheater::match
