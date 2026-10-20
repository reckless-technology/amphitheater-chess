#pragma once
/// @file Statistics.h What a match's or a tournament's results add up to: scores, an Elo difference with its error,
/// standings with the Sonneborn-Berger tiebreak, and a crosstable.
///
/// The Elo difference is the logistic model's: a score fraction s is a difference of -400 log10(1/s - 1). Its 95 %
/// interval comes from the trinomial variance of one game's score - wins, draws and losses each about the mean - so a
/// match of many draws is measured as the more certain thing it is; the margin is half the interval's width. The
/// likelihood of superiority is the normal approximation over the decisive games, 0.5 (1 + erf((W - L) / sqrt(2 (W +
/// L)))).

#include "chess/Game.h"

#include <optional>
#include <string>
#include <vector>

namespace amphitheater::match
{

/** Wins, draws and losses from one side's view. */
struct ScoreLine
{
    int wins   = 0; ///< Games won.
    int draws  = 0; ///< Games drawn.
    int losses = 0; ///< Games lost.

    /** @return How many games. */
    int Games() const
    {
        return wins + draws + losses;
    }

    /** @return Points: a win one, a draw a half. */
    double Points() const
    {
        return wins + 0.5 * draws;
    }

    /** @return Points over games, 0 with no games. */
    double Fraction() const
    {
        return Games() == 0 ? 0.0 : Points() / Games();
    }
};

/** An Elo difference estimated from a score. */
struct EloEstimate
{
    std::optional<double> difference;   ///< The difference; nothing when the score is all wins or all losses.
    std::optional<double> error_margin; ///< Half the 95 % interval's width; nothing when it cannot be measured.
    double                likelihood_of_superiority = 0.5; ///< The chance the first side is the stronger.
    double                draw_ratio                = 0.0; ///< Draws over games.
};

/**
 * Estimate an Elo difference.
 * @param score The first side's wins, draws and losses.
 * @return The estimate.
 */
EloEstimate EstimateElo(const ScoreLine &score);

/**
 * An Elo difference from a score fraction.
 * @param fraction The score fraction, strictly between 0 and 1.
 * @return The difference.
 */
double EloFromFraction(double fraction);

/** One finished game, as the tables count it. */
struct GameResultEntry
{
    int               white  = 0;                        ///< White's index among the engines.
    int               black  = 0;                        ///< Black's index.
    chess::GameResult result = chess::GameResult::kNone; ///< The result (kNone counts for neither).
};

/** One engine's line in the standings. */
struct Standing
{
    int         engine = 0;           ///< Its index.
    std::string name;                 ///< Its name.
    ScoreLine   score;                ///< Its wins, draws and losses.
    double      sonneborn_berger = 0; ///< Each opponent's points, times what was scored against it.
    int         rank             = 0; ///< Its place, from 1; tied engines share one.
};

/**
 * The standings: points first, then Sonneborn-Berger, then the name.
 * @param names Each engine's name, by index.
 * @param results The finished games.
 * @return One line per engine, best first.
 */
std::vector<Standing> ComputeStandings(const std::vector<std::string>     &names,
                                       const std::vector<GameResultEntry> &results);

/** Points scored by each engine against each other. */
struct Crosstable
{
    std::vector<std::vector<ScoreLine>> cells; ///< cells[i][j]: engine i's score against engine j.

    /**
     * Build it.
     * @param engines How many engines.
     * @param results The finished games.
     * @return The crosstable.
     */
    static Crosstable Build(int engines, const std::vector<GameResultEntry> &results);
};

/**
 * The standings as a text table: rank, name, points, games, score percentage, wins, draws, losses, Sonneborn-Berger.
 * @param standings The standings.
 * @return The table, one line per engine after a heading.
 */
std::string FormatStandings(const std::vector<Standing> &standings);

/**
 * The crosstable as text: one row per engine in the standings' order, each cell the points against that column's
 * engine over the games played ("1.5/2"), a dot for an engine against itself.
 * @param names Each engine's name, by index.
 * @param standings The standings, for the order.
 * @param table The crosstable.
 * @return The table.
 */
std::string FormatCrosstable(const std::vector<std::string> &names, const std::vector<Standing> &standings,
                             const Crosstable &table);

/**
 * A two-engine match's score line and Elo, as a person reads them: "Score of A vs B: 3 - 2 - 5  [0.550] 10" and
 * "Elo difference: 34.9 +/- 120.3, LOS: 70.2 %, DrawRatio: 50.0 %".
 * @param first The first engine's name.
 * @param second The second's.
 * @param score The first's wins, draws and losses.
 * @return The two lines.
 */
std::string FormatMatchScore(const std::string &first, const std::string &second, const ScoreLine &score);

} // namespace amphitheater::match
