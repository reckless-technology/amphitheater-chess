#pragma once
/// @file GameAnalysis.h An engine over every position of a game, one after another on a thread of its own: its score,
/// its depth and its best line in each - the start, after each move, and after the last - and each move judged by what
/// it cost its mover in winning chances against the engine's best. Winning chances follow the curve lichess publishes,
/// 2 / (1 + e^(-0.00368208 cp)) - 1 from -1 (lost) to 1 (won), a mate counting as a win; a move that costs more than
/// 0.1 of them is an inaccuracy, 0.2 a mistake and 0.3 a blunder. A position the game ends in (mate or stalemate) is
/// scored by the rules, not searched.

#include "chess/Position.h"
#include "uci/EngineConfig.h"
#include "uci/ThreadSafeQueue.h"
#include "uci/UciProtocol.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace amphitheater::match
{

/** How a game is analysed. */
struct GameAnalysisSettings
{
    uci::EngineConfig                  engine;    ///< The engine.
    std::optional<chess::Milliseconds> move_time; ///< Each position's search time; none with a depth.
    std::optional<int>                 depth;     ///< Each position's search depth; none with a time.
};

/** What the engine made of one position of the game. */
struct PositionAnalysis
{
    int          ply          = 0;             ///< The position: 0 the start, n after the n-th move.
    chess::Color side_to_move = chess::kWhite; ///< Whose move it is there.
    /** Its score from the side to move's point of view, as UCI gives it ("mate 0": the side to move is mated); none
        where the engine gave none. */
    std::optional<uci::Score> score;
    int                       depth = 0; ///< The depth it reached.
    std::vector<std::string>  pv;        ///< Its best line, in UCI notation; empty at a game's end.
};

/** What a move cost its mover in winning chances against the engine's best. */
enum class MoveQuality
{
    kGood,       ///< 0.1 or less.
    kInaccuracy, ///< More than 0.1: "?!".
    kMistake,    ///< More than 0.2: "?".
    kBlunder,    ///< More than 0.3: "??".
};

/**
 * White's winning chances in a position, from -1 (lost) to 1 (won).
 * @param score The score, from the side to move's point of view.
 * @param side_to_move The side to move.
 * @return The chances: a mate 1 or -1, else 2 / (1 + e^(-0.00368208 cp)) - 1.
 */
double WhiteWinningChances(const uci::Score &score, chess::Color side_to_move);

/**
 * Judge a move by the position before it and the one after it.
 * @param before The position before the move: the engine's best there.
 * @param after The position after it.
 * @return What it cost its mover; none where either position has no score.
 */
std::optional<MoveQuality> JudgeMove(const PositionAnalysis &before, const PositionAnalysis &after);

/**
 * @param quality A move's quality.
 * @return Its NAG: 6 ("?!"), 2 ("?") or 4 ("??"); 0 for a good move.
 */
int NagOf(MoveQuality quality);

/** An engine over a game's positions, on a thread of its own. */
class GameAnalyser
{
  public:
    /**
     * @param settings The engine and how long it searches.
     * @param start The position before the first move.
     * @param moves The game's moves, in UCI notation, each legal.
     */
    GameAnalyser(GameAnalysisSettings settings, chess::Position start, std::vector<std::string> moves);

    /** Stops the analysis and waits for its thread, the engine quit. */
    ~GameAnalyser();

    GameAnalyser(const GameAnalyser &)            = delete;
    GameAnalyser &operator=(const GameAnalyser &) = delete;

    /** The engine started and the positions searched, the start's first, on a thread of their own. */
    void Start();

    /** No more positions searched: the one under way finishes, and the analysis ends. */
    void Stop();

    /** @return The next position's analysis, as soon as it is done, without waiting; none while there is none yet. */
    std::optional<PositionAnalysis> Next();

    /** @return Whether the analysis has ended: every position done, stopped, or failed. */
    bool IsFinished() const
    {
        return is_finished_;
    }

    /** @return Why the analysis failed: the engine would not start or quit; empty when it did not. */
    std::string Failure() const;

    /** @return The positions the game has: one more than its moves. */
    int PositionCount() const
    {
        return static_cast<int>(moves_.size()) + 1;
    }

    /** @return The engine's name, as its handshake gave it; empty until it has started. */
    std::string EngineName() const;

  private:
    /** The thread's work: the engine started, each position searched and queued. */
    void Run();

    /**
     * @param failure Why the analysis failed.
     */
    void Fail(std::string failure);

    GameAnalysisSettings              settings_;           ///< The engine and its search.
    chess::Position                   start_;              ///< The position before the first move.
    std::vector<std::string>          moves_;              ///< The game's moves.
    ThreadSafeQueue<PositionAnalysis> results_;            ///< What the thread has done.
    std::atomic<bool>                 is_stopping_{false}; ///< Stop asked for.
    std::atomic<bool>                 is_finished_{false}; ///< The thread has ended.
    mutable std::mutex                mutex_;              ///< Guards the failure and the name.
    std::string                       failure_;            ///< Why it failed.
    std::string                       engine_name_;        ///< The engine's name.
    std::thread                       thread_;             ///< The thread.
};

} // namespace amphitheater::match
