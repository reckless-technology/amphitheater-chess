#pragma once
/// @file Evaluations.h The evaluation graph's data: each engine's scores along a game, every one from White's point of
/// view (+ White is better, - Black is), whichever side the engine played or had to move. An engine reports its score
/// from the side to move in the position it searches (UCI's convention); these turn it to White's before anything is
/// drawn, so a game's two engines read on one scale and side by side.

#include "chess/ChessTypes.h"
#include "chess/Pgn.h"
#include "match/GameRunner.h"
#include "uci/UciProtocol.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace amphitheater::app
{

/** What a mate counts as on the graph, in pawns: past any scale it draws, so it is held at the scale's edge. */
inline constexpr double kMatePawns = 100.0;

/** One score on the graph, from White's point of view. */
struct EvalPoint
{
    double             pawns = 0.0; ///< White's score in pawns; +kMatePawns or -kMatePawns for a mate.
    std::optional<int> mate;        ///< For a mate: the moves to it, + when White mates, - when Black does.

    bool operator==(const EvalPoint &) const = default; ///< Points are equal when every field is.
};

/** One engine's scores along a game. */
struct EvalSeries
{
    std::string                           name;   ///< Whose scores they are, as the legend names it.
    std::vector<std::optional<EvalPoint>> points; ///< [n - 1]: the score with the n-th move; none where it gave none.

    bool operator==(const EvalSeries &) const = default; ///< Series are equal when every field is.
};

/**
 * A score as the graph takes it, from White's point of view.
 * @param score The score as the engine reported it, from the side to move's point of view.
 * @param side_to_move The side to move in the position it searched.
 * @return The point: a mate held at kMatePawns on the side that mates (a mate in 0 against the side to move).
 */
EvalPoint EvalPointOf(const uci::Score &score, chess::Color side_to_move);

/**
 * A point as the window writes it: "+0.42", "-1.10", "#5" (White mates), "#-3" (Black mates).
 * @param point The point.
 * @return The text.
 */
std::string EvalText(const EvalPoint &point);

/**
 * A point as a PGN comment's [%eval] command, as EvalPointOfComment reads it back: "[%eval 0.35]", "[%eval -1.20]",
 * "[%eval #3]", "[%eval #-2]".
 * @param point The point; a mate in 0 has no command.
 * @return The command; empty for a mate in 0.
 */
std::string EvalCommand(const EvalPoint &point);

/**
 * A game's series from its moves' notes: one for each side an engine played, White's first, each holding the scores
 * its engine gave with its own moves - at its own plies, so the two interleave - from White's point of view.
 * @param notes The game's notes, one per move.
 * @param ply_count The game's length in plies (each series is this long).
 * @param names White's and Black's names.
 * @param is_engine Whether White and Black are engines; a person's side has no series.
 * @return The series.
 */
std::vector<EvalSeries> SeriesOfNotes(const std::vector<match::MoveNote> &notes, std::size_t ply_count,
                                      const std::array<std::string, 2> &names, const std::array<bool, 2> &is_engine);

/**
 * The score a PGN move comment gives, from White's point of view: the host's own "+0.35/18 1.2s" and "-M3/20 0.4s"
 * (the leading score, which these files write from White's side), or a "[%eval 0.35]" or "[%eval #-3]" command.
 * @param comment The comment.
 * @return The point, or none for a comment with no score ("book", "1.2s", words).
 */
std::optional<EvalPoint> EvalPointOfComment(std::string_view comment);

/**
 * A PGN game's series from its move comments: with "[%eval]" commands, one series of them named "Evaluation" (an
 * analysis of the whole game); else one for each side whose moves carry scores, named by its White or Black tag.
 * @param pgn The game.
 * @return The series, each as long as the main line; none when no comment has a score.
 */
std::vector<EvalSeries> SeriesOfPgn(const chess::PgnGame &pgn);

} // namespace amphitheater::app
