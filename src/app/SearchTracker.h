#pragma once
/// @file SearchTracker.h One engine's search as the window shows it, followed from the lines it exchanges: the position
/// it searches (from the "position" the host sent), whether it thinks or ponders and since when, its latest line at
/// each multi-PV rank - depth, score from White's side, and the line in SAN - and its best line at each of the last few
/// depths, with the figures of its search.

#include "AnalysisPanel.h"
#include "Evaluations.h"
#include "chess/Move.h"
#include "chess/Position.h"
#include "uci/UciProtocol.h"

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace amphitheater::app
{

/**
 * A line of UCI moves in SAN, numbered from a position: "13.Ne2 Ne8 14.f4", or "13...Ne8 14.f4" from Black's move.
 * The line stops at the first move that is not legal.
 * @param position The position it starts from.
 * @param pv The moves, in UCI notation.
 * @param most The most moves to write; 0 for all.
 * @return The text.
 */
std::string PvToSan(const chess::Position &position, const std::vector<std::string> &pv, std::size_t most = 0);

/**
 * A score from White's side, as the window writes it: "+0.42", "-1.10", "#5" (White mates), "#-3" (Black mates); a
 * bound marked before it from White's side, ">+0.42" for at least, "<-1.10" for at most (a lower bound on Black's
 * score is an upper bound on White's).
 * @param score The score, from the side to move's point of view.
 * @param side_to_move Whose point of view that is.
 * @return The text.
 */
std::string WhiteScoreText(const uci::Score &score, chess::Color side_to_move);

/**
 * A score from White's side in pawns, for the evaluation graph: a mate counts as a large score.
 * @param score The score, from the side to move's point of view.
 * @param side_to_move Whose point of view that is.
 * @return The pawns.
 */
double WhitePawns(const uci::Score &score, chess::Color side_to_move);

/**
 * The name an engine's lines and block go under in a game: its own, or with its side after it - "Pawnstar (White)" -
 * when its opponent has the same name, so two copies of one engine are told apart.
 * @param name The engine's name.
 * @param opponent Its opponent's name.
 * @param side The side it plays.
 * @return The label.
 */
std::string EngineLabel(const std::string &name, const std::string &opponent, chess::Color side);

/**
 * A count of nodes as a person reads it: "812 n", "6.4 kn", "11.2 Mn", "1.05 Gn".
 * @param nodes The count.
 * @return The text.
 */
std::string NodesText(std::int64_t nodes);

/** One engine's search, followed. */
class SearchTracker
{
  public:
    using TimePoint = std::chrono::steady_clock::time_point; ///< When something happened.

    /** @param name The engine's name, as its block shows it. */
    explicit SearchTracker(std::string name);

    /** @return The engine's name. */
    const std::string &name() const
    {
        return name_;
    }

    /**
     * A search begins.
     * @param position The position it searches.
     * @param is_ponder Whether it ponders (on the move that led to the position) rather than thinks.
     * @param at When.
     */
    void Begin(const chess::Position &position, bool is_ponder, TimePoint at);

    /**
     * A ponder search becomes the engine's own: "ponderhit".
     * @param at When.
     */
    void PonderHit(TimePoint at);

    /**
     * An "info" line of the search.
     * @param info Its fields.
     */
    void Info(const uci::SearchInfo &info);

    /** The search has answered: "bestmove". Its lines are kept, shown as the last word. */
    void End();

    /**
     * Follow a line the host exchanged with the engine: "position", "go", "ponderhit" and the engine's "info" and
     * "bestmove" each move the search on; anything else is passed over.
     * @param is_to_engine Whether the host wrote it.
     * @param line The line.
     * @param at When.
     */
    void Feed(bool is_to_engine, std::string_view line, TimePoint at);

    /** @return Whether a search is under way. */
    bool IsSearching() const
    {
        return is_searching_;
    }

    /** @return Whether the search under way is a ponder search. */
    bool IsPondering() const
    {
        return is_searching_ && is_ponder_;
    }

    /** @return The position searched last. */
    const chess::Position &position() const
    {
        return position_;
    }

    /** @return The best line's first move, when it is legal in the position searched. */
    std::optional<chess::Move> BestMove() const;

    /** @return The best line's score from White's side in pawns, when there is one. */
    std::optional<double> BestWhitePawns() const;

    /** @return The best line's score from White's side as text, when there is one. */
    std::optional<std::string> BestScoreText() const;

    /** @return The best line's score from White's side as the graph takes it, when there is one. */
    std::optional<EvalPoint> BestEvalPoint() const;

    /**
     * The engine's block for the analysis panel.
     * @param now The time, for "thinking 4.8 s".
     * @param most_lines The most lines to show.
     * @return The block.
     */
    EngineBlock Block(TimePoint now, std::size_t most_lines = kDepthsKept) const;

    /** The depths whose best line is kept, the newest last, for an engine that gives one line at a time. */
    static constexpr std::size_t kDepthsKept = 6;

  private:
    /** One multi-PV rank's latest line. */
    struct Line
    {
        int                       depth    = 0; ///< Its depth.
        int                       seldepth = 0; ///< Its selective depth.
        std::optional<uci::Score> score;        ///< Its score, from the side to move's point of view.
        std::vector<std::string>  pv;           ///< Its moves, in UCI notation.
    };

    std::string                 name_;                 ///< The engine's name.
    chess::Position             position_;             ///< The position searched.
    chess::Position             sent_position_;        ///< The position the last "position" named.
    std::string                 sent_last_move_;       ///< Its last move, numbered in SAN, for a ponder search's chip.
    bool                        is_searching_ = false; ///< A search is under way.
    bool                        is_ponder_    = false; ///< It is a ponder search.
    std::string                 ponder_san_;           ///< The move pondered on, in SAN.
    TimePoint                   started_{};            ///< When the search (or its ponderhit) began.
    std::map<int, Line>         lines_;                ///< The latest line at each rank.
    std::vector<Line>           depths_;               ///< The best line at each depth, oldest first.
    std::optional<std::int64_t> nodes_;                ///< The latest node count.
    std::optional<std::int64_t> nps_;                  ///< The latest speed.
    std::optional<int>          hash_full_;            ///< The latest hash fill, in permille.
};

} // namespace amphitheater::app
