#pragma once
/// @file TestSuite.h EPD test suites: positions with the move an engine should find (bm, "best move") or must not play
/// (am, "avoid move"), as suites such as Win at Chess and the Bratko-Kopec test write them, read from EPD and run
/// against one engine - each position searched in turn on a thread of its own for a time or a depth, and solved when
/// the engine's move is one of the best moves (or, with none given, none of the moves to avoid).
///
/// An EPD line is a FEN's four position fields, then operations, each an opcode, its operands and a semicolon:
/// "bm Qd1+; id \"WAC.001\";". bm and am take moves in SAN, read in the line's position; hmvc and fmvn set the
/// counters; id names the position; any other operation is passed over. Blank lines and lines starting with '#' are
/// skipped.

#include "chess/Move.h"
#include "chess/Position.h"
#include "chess/Result.h"
#include "uci/EngineConfig.h"
#include "uci/ThreadSafeQueue.h"
#include "uci/UciProtocol.h"

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace amphitheater::match
{

/** A position of a test suite. */
struct TestPosition
{
    std::string              id;          ///< Its name: the id operation, else "line n".
    chess::Position          position;    ///< The position.
    std::vector<chess::Move> best_moves;  ///< The moves that solve it (bm); empty with avoid moves.
    std::vector<chess::Move> avoid_moves; ///< The moves that fail it (am).
    int                      line = 0;    ///< Its line in the file, from 1.
};

/**
 * Read a test suite's line.
 * @param line The line.
 * @param number Its line number, from 1, for the default name.
 * @return The position, or why the line is not one: a FEN that cannot be read, a move that is not legal there, or no
 *         bm and no am.
 */
Result<TestPosition> ParseTestPosition(std::string_view line, int number);

/**
 * Read a test suite.
 * @param text The EPD file's text.
 * @param errors The lines skipped and why, appended (may be null).
 * @return The positions.
 */
std::vector<TestPosition> ParseTestSuite(std::string_view text, std::vector<std::string> *errors = nullptr);

/**
 * Whether a move solves a position.
 * @param position The position.
 * @param move The engine's move.
 * @return Whether it is one of the best moves and none of the moves to avoid.
 */
bool Solves(const TestPosition &position, chess::Move move);

/** How a suite is run. */
struct TestSuiteSettings
{
    uci::EngineConfig                  engine;    ///< The engine.
    std::optional<chess::Milliseconds> move_time; ///< Each position's time; none with a depth.
    std::optional<int>                 depth;     ///< Each position's depth; none with a time.
};

/** What the engine made of one position. */
struct TestOutcome
{
    int                       index = 0;         ///< The position, from 0, in the suite's order.
    std::string               move;              ///< The engine's move in SAN; empty where it gave none.
    bool                      is_solved = false; ///< Whether the move solves the position.
    std::optional<uci::Score> score;             ///< Its last score, from the side to move's point of view.
    int                       depth = 0;         ///< The depth it reached.
};

/** A suite run against one engine, on a thread of its own. */
class TestSuiteRunner
{
  public:
    /**
     * @param settings The engine and how long it searches.
     * @param positions The suite.
     */
    TestSuiteRunner(TestSuiteSettings settings, std::vector<TestPosition> positions);

    /** Stops the run and waits for its thread, the engine quit. */
    ~TestSuiteRunner();

    TestSuiteRunner(const TestSuiteRunner &)            = delete;
    TestSuiteRunner &operator=(const TestSuiteRunner &) = delete;

    /** The engine started and the positions searched in turn, on a thread of their own. */
    void Start();

    /** No more positions searched: the one under way finishes, and the run ends. */
    void Stop();

    /** @return The next position's outcome, without waiting; none while there is none yet. */
    std::optional<TestOutcome> Next();

    /** @return Whether the run has ended: every position done, stopped, or failed. */
    bool IsFinished() const
    {
        return is_finished_;
    }

    /** @return Why the run failed; empty when it did not. */
    std::string Failure() const;

    /** @return The suite. */
    const std::vector<TestPosition> &positions() const
    {
        return positions_;
    }

  private:
    /** The thread's work. */
    void Run();

    /** @param failure Why the run failed. */
    void Fail(std::string failure);

    TestSuiteSettings            settings_;           ///< The engine and its search.
    std::vector<TestPosition>    positions_;          ///< The suite.
    ThreadSafeQueue<TestOutcome> outcomes_;           ///< What the thread has done.
    std::atomic<bool>            is_stopping_{false}; ///< Stop asked for.
    std::atomic<bool>            is_finished_{false}; ///< The thread has ended.
    mutable std::mutex           mutex_;              ///< Guards the failure.
    std::string                  failure_;            ///< Why it failed.
    std::thread                  thread_;             ///< The thread.
};

} // namespace amphitheater::match
