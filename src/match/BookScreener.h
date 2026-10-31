#pragma once
/// @file BookScreener.h Screening an opening book against an engine's own judgement: a move scoring far enough below
/// the best move at its position is marked, so the book stops offering moves the engine would decline.
///
/// A book built from what strong players play is not a book a given engine would play. Frequency says a move is
/// popular; it says nothing about whether this engine, with this evaluation, rates it. So a book can offer a move the
/// engine scores as losing most of a pawn, and will play it because the book said so.
///
/// This marks such a move with a trailing `?`, which `TextBook` reads and the engines' loaders replay without ever
/// selecting. **Marking, not deleting**, and the difference matters: a `?` keeps the rest of its line, so the engine
/// still has its prepared reply to a move it would not play itself - the opponent may well play it. Deleting the move
/// truncates every line through it; measured on the suite's own book, that cost 45% of its lines, 2,225 positions and
/// its answer to 13 opponent second moves.
///
/// Where every move at a position is marked, the position has nothing it may play, which would drop the engine out of
/// book silently and exactly where an opponent had surprised it. Those positions are given the engine's own choice,
/// unmarked - at zero loss by construction, being the move the margin is measured from.
///
/// ## Why it drives the engine over UCI
///
/// The screener needs no engine source, links none, and knows nothing about any engine's evaluation. It asks an
/// engine over UCI and believes the answer. That is not a compromise for portability; it is the design:
///
/// - **One screener serves every engine.** The predecessor, `pawnstar/tools/filter_book.cpp`, was compiled against
///   one engine's evaluation, so each engine needed its own copy, and each copy rotted separately. That one broke in
///   five independent ways and nothing noticed, because it was a tool in one repository that nothing built against
///   and no test ran.
/// - **It cannot embed what it must not.** Linking the engine put a 12 MB `Game` on the stack, which overflowed it
///   on every platform the moment the net grew. Over a pipe there is nothing to embed.
/// - **It can be tested.** The fake engine in `tests/fake_engine/` answers `go` with a score, so the screening logic
///   is exercised in CI with no engine present and no net.
///
/// ## The three guarantees, and why each is needed
///
/// 1. **One thread.** Under Lazy SMP the helper threads race through a shared transposition table, so the same search
///    returns a different score each time. `Threads = 1` makes a search reproducible.
/// 2. **`ucinewgame` before every search.** Without it a search inherits the table from the search before it - and
///    the parent search writes a fail-low bound for every move it rejects, including the move whose score is taken
///    next, which then reads its own rejection back as a bound and confirms it. Unclearing that table made one
///    (position, move) pair score 90, 98, 106, 111 and 126 cp in a single run of the predecessor.
/// 3. **Every score from a restricted search.** An unrestricted search's score and a single-move search's score are
///    not the same quantity: after 1.Nf3 g6 an unrestricted depth-12 search scores `e2e4` at +190, and `e2e4`
///    searched alone at the same depth scores +102. A root holding twenty-odd moves prunes, orders and re-searches
///    nothing like a root holding one, so "depth 12" is not the same work in each. Measured against the unrestricted
///    figure, every book reply to 1.Nf3 g6 came out more than 75 cp short and the position lost every move it had.
///    So the engine's own choice is re-scored the same restricted way each book move is, and like is compared with
///    like.

#include "chess/Result.h"
#include "chess/TextBook.h"
#include "uci/Engine.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace amphitheater::match
{

/** What to screen, with which engine, and how hard. */
struct BookScreenerSettings
{
    std::string       book;         ///< The book to read, in the engines' text format.
    std::string       out;          ///< Where to write the screened book; may be the same path.
    uci::EngineConfig engine;       ///< The engine to ask. Its options are set by the screener, not by the caller.
    int               depth  = 12;  ///< The fixed search depth, for every score.
    int               margin = 100; ///< A move is marked when it scores this many centipawns or more below the best.

    /// Every line to and from the engine, if a caller wants it. A screening run is thousands of searches whose
    /// verdicts end up committed to a book, so being able to read back exactly what was asked and answered is worth
    /// having - and it is how the suite checks that the options below are the ones the engine actually ran with.
    uci::EngineLogger logger;
};

/** One move the screener marked, kept so a run can be reviewed rather than believed. */
struct ScreenedMove
{
    std::string fen;        ///< The position it was played from.
    std::string move;       ///< The move, in coordinate notation.
    int         best   = 0; ///< The engine's own choice there, re-scored restricted.
    int         scored = 0; ///< This move's own score, restricted, at the same depth.
    int         loss   = 0; ///< best - scored, which is what the margin is compared against.
};

/** How a screening run went. */
struct BookScreenerSummary
{
    std::size_t openings      = 0;   ///< Openings in the book.
    std::size_t searches      = 0;   ///< Searches run, which is the cost of the run.
    std::size_t marked        = 0;   ///< Moves marked, counted as distinct (position, move) pairs.
    std::size_t refilled      = 0;   ///< Positions given the engine's own move because every move they had is marked.
    std::size_t lines_written = 0;   ///< Lines in the written book, including those added by refilling.
    std::vector<ScreenedMove> marks; ///< Every mark, in the order they were decided.
    std::vector<std::string>  problems; ///< Lines that did not replay, and any search that failed.
};

/** Progress, for a caller that wants to show it. */
struct BookScreenerProgress
{
    std::size_t positions_done  = 0; ///< Positions judged.
    std::size_t positions_total = 0; ///< Positions to judge.
    std::size_t marked          = 0; ///< Marks so far.
};

/**
 * Screen a book.
 *
 * The engine is started once and driven in one thread: a search is milliseconds and the determinism the verdicts
 * depend on is per-engine state, so parallelism here would mean an engine per worker and a memory cost to match.
 * `BookMaker` makes the same choice for the same reason.
 *
 * @param settings What to screen and with what.
 * @param progress Called as positions are judged, or empty. Called from this thread.
 * @param cancel Polled between positions; return true to stop. A cancelled run writes nothing.
 * @return What it did, or why it could not.
 */
Result<BookScreenerSummary> ScreenBook(const BookScreenerSettings                      &settings,
                                       const std::function<void(BookScreenerProgress)> &progress = {},
                                       const std::function<bool()>                     &cancel   = {});

} // namespace amphitheater::match
