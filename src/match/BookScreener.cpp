/// @file BookScreener.cpp Screening an opening book against an engine's own judgement.

#include "BookScreener.h"

#include "chess/Notation.h"
#include "chess/PolyglotBook.h"
#include "uci/Engine.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

namespace amphitheater::match
{
namespace
{

/// A mate is worth more than any centipawn score, and a move that mates must never be marked. Mapping mates onto a
/// large centipawn value lets one subtraction serve both kinds, which is all the margin comparison needs.
constexpr int kMateValue = 1000000;

/// @brief A score as a single comparable number, from the side to move's point of view.
/// @param score What the engine reported.
/// @return Centipawns, or +/- kMateValue less the distance, so a nearer mate compares higher.
int Comparable(const uci::Score &score)
{
    if (score.kind == uci::Score::Kind::kMate)
    {
        return score.value >= 0 ? kMateValue - score.value : -kMateValue - score.value;
    }
    return score.value;
}

/// What one fixed-depth search answered.
struct Scored
{
    bool        ok    = false;
    int         value = 0; ///< Comparable score, side-to-move relative.
    std::string best_move; ///< The move the engine answered with.
};

/// @brief Search one position to a fixed depth, optionally restricted to a single root move.
///
/// `NewGame` before every search is not a courtesy: it sends `ucinewgame`, which clears the transposition table. A
/// search that inherits the previous search's table reads that search's fail-low bounds for the moves it rejected -
/// including, next time round, the very move whose score is being taken - and confirms them. See BookScreener.h.
///
/// @param engine The engine, already started and set to one thread.
/// @param fen The position.
/// @param depth The fixed depth.
/// @param only_move Coordinate notation of the one root move to allow, or empty for an unrestricted search.
/// @return The score, or `ok == false` if the engine did not answer in time.
Scored SearchOnce(uci::Engine &engine, const std::string &fen, int depth, const std::string &only_move)
{
    if (!engine.NewGame())
    {
        return {};
    }
    if (!engine.SetPosition(fen, {}))
    {
        return {};
    }
    uci::GoParameters parameters;
    parameters.depth = depth;
    if (!only_move.empty())
    {
        parameters.search_moves = {only_move};
    }
    // A fixed-depth search of an opening position is milliseconds, so a deadline this generous only ever fires on an
    // engine that has stopped answering - which must be reported, not waited on for ever.
    const auto              deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    const uci::SearchResult result   = engine.Search(parameters, deadline);
    if (result.ending != uci::SearchResult::Ending::kBestMove || !result.info.score)
    {
        return {};
    }
    return {true, Comparable(*result.info.score), result.best_move.move};
}

} // namespace

Result<BookScreenerSummary> ScreenBook(const BookScreenerSettings                      &settings,
                                       const std::function<void(BookScreenerProgress)> &progress,
                                       const std::function<bool()>                     &cancel)
{
    if (settings.depth < 1)
    {
        return Error{"a screening depth must be at least 1"};
    }
    if (settings.margin < 1)
    {
        return Error{"a screening margin must be at least 1 centipawn"};
    }

    std::ifstream input{settings.book, std::ios::binary};
    if (!input)
    {
        return Error{"cannot read the book '" + settings.book + "'"};
    }
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    chess::TextBook   book = chess::ReadTextBook(text);
    if (book.OpeningCount() == 0)
    {
        return Error{"'" + settings.book + "' holds no openings"};
    }

    uci::EngineConfig config = settings.engine;
    // The screener's own requirements, appended so they win over anything the caller asked for: one thread, because
    // Lazy SMP makes a search's score depend on how its helper threads raced; and the book off, because a book move
    // is a canned reply and the engine's own judgement is the whole question.
    config.options.emplace_back("Threads", "1");
    config.options.emplace_back("OwnBook", "false");
    uci::Engine engine{config};
    if (settings.logger)
    {
        engine.SetLogger(settings.logger);
    }
    if (const auto started = engine.Start(); !started)
    {
        return Error{"the engine did not start: " + started.ErrorMessage()};
    }

    BookScreenerSummary summary;
    summary.openings = book.OpeningCount();

    // An option the engine never declared cannot be set, and Engine records that as a warning rather than failing
    // the start - which is right in general and dangerous here, because two of those options are the screener's
    // guarantees. So check that each one it needs either took effect or was never the engine's to offer.
    //
    // `Threads` matters because under Lazy SMP a search's score depends on how its helper threads raced through the
    // shared table, so a verdict taken from a multi-threaded search is not reproducible. An engine that declares
    // `Threads` and refused to be set to 1 cannot be screened with; one that declares no `Threads` at all is
    // single-threaded already and there is nothing to impose.
    //
    // `OwnBook` matters more. An engine answering a search from its own opening book is not judging the position at
    // all - it is reciting a canned reply - so every verdict taken from it would be meaningless while looking
    // perfectly reasonable. If the engine has a book and would not put it down, stop.
    for (const auto &warning : engine.Warnings())
    {
        summary.problems.push_back("the engine refused an option: " + warning);
    }
    for (const char *needed : {"Threads", "OwnBook"})
    {
        const uci::EngineOption *declared = engine.FindOption(needed);
        if (declared == nullptr)
        {
            continue; // not this engine's option: no threads to pin, or no book to put down
        }
        const bool refused = std::ranges::any_of(engine.Warnings(), [needed](const std::string &warning) {
            return warning.find(needed) != std::string::npos;
        });
        if (refused)
        {
            return Error{std::string{"the engine declares "} + needed +
                         " but would not take the screener's value, and a verdict from it could not be trusted"};
        }
    }

    // A position's reference: the engine's own choice there, re-scored the same restricted way every book move is.
    struct Reference
    {
        bool        ok    = false;
        int         value = 0;
        std::string move;
    };

    std::map<std::uint64_t, Reference> references;
    std::map<std::string, bool>        verdicts; // (position key, move) -> marked

    // Count the work first, so progress means something to a caller showing it.
    {
        const chess::TextBookIndex index = chess::ReplayTextBook(book);
        summary.problems                 = index.errors;
        BookScreenerProgress counted;
        counted.positions_total = index.positions.size();
        if (progress)
        {
            progress(counted);
        }
    }

    std::size_t positions_done = 0;
    for (chess::TextBookLine &line : book.lines)
    {
        if (!line.HasMoves())
        {
            continue;
        }
        chess::Position position   = chess::Position::Start();
        std::size_t     move_index = 0;

        for (chess::TextBookMove &move : line.moves)
        {
            if (cancel && cancel())
            {
                return Error{"cancelled"};
            }
            const auto parsed = chess::ParseUciMove(position, move.coordinate);
            if (!parsed)
            {
                summary.problems.push_back("'" + move.coordinate + "' is not legal in " + position.ToFen());
                break;
            }
            const std::uint64_t key = chess::PolyglotKey(position);
            const std::string   fen = position.ToFen();

            Reference &reference = references[key];
            if (!reference.ok)
            {
                // Two searches per position, once: the engine's own choice, then that move re-scored restricted so it
                // is the same kind of number as every book move's.
                const Scored chosen = SearchOnce(engine, fen, settings.depth, {});
                ++summary.searches;
                if (chosen.ok && !chosen.best_move.empty())
                {
                    const Scored rescored = SearchOnce(engine, fen, settings.depth, chosen.best_move);
                    ++summary.searches;
                    if (rescored.ok)
                    {
                        reference = {true, rescored.value, chosen.best_move};
                    }
                }
                if (!reference.ok)
                {
                    summary.problems.push_back("no reference score for " + fen);
                }
                ++positions_done;
                if (progress)
                {
                    BookScreenerProgress step;
                    step.positions_done = positions_done;
                    step.marked         = summary.marked;
                    progress(step);
                }
            }

            const std::string verdict_key = std::to_string(key) + ":" + move.coordinate;
            if (const auto seen = verdicts.find(verdict_key); seen != verdicts.end())
            {
                move.is_questionable = seen->second;
            }
            else if (!reference.ok)
            {
                move.is_questionable = false; // nothing to judge against; leave the book as it was
            }
            else
            {
                const Scored scored = SearchOnce(engine, fen, settings.depth, move.coordinate);
                ++summary.searches;
                const int  loss       = reference.value - scored.value;
                const bool marked     = scored.ok && loss > settings.margin;
                move.is_questionable  = marked;
                verdicts[verdict_key] = marked;
                if (!scored.ok)
                {
                    summary.problems.push_back("no score for " + move.coordinate + " in " + fen);
                }
                else if (marked)
                {
                    ++summary.marked;
                    summary.marks.push_back({fen, move.coordinate, reference.value, scored.value, loss});
                }
            }

            // The token carries the verdict, because the token is what is written back out.
            line.tokens[move_index] = move.is_questionable ? move.coordinate + "?" : move.coordinate;
            position                = position.MakeMove(*parsed);
            ++move_index;
        }
    }

    // ---- Where every move at a position is marked, the position has nothing it may play ----
    //
    // A marked move is replayed but never chosen, so such a position would drop the engine out of book silently, and
    // exactly where an opponent had just played something unusual. The engine's own choice goes there, unmarked: it
    // is at zero loss by construction, being the move the margin is measured from.
    const chess::TextBookIndex after = chess::ReplayTextBook(book);
    std::set<std::uint64_t>    refilled;
    for (const auto &[key, entry] : after.positions)
    {
        if (!entry.selectable.empty())
        {
            continue;
        }
        const auto reference = references.find(key);
        if (reference == references.end() || !reference->second.ok)
        {
            summary.problems.push_back("a position has nothing to play and no reference move to give it");
            continue;
        }
        refilled.insert(key);
    }

    if (!refilled.empty())
    {
        // Find a way to reach each emptied position, keeping the marks on the prefix: written bare, the prefix would
        // put every marked move along the way back into the selectable set and undo the marking that created the line.
        std::map<std::uint64_t, std::vector<std::string>> reached_by;
        for (const chess::TextBookLine &line : book.lines)
        {
            if (!line.HasMoves())
            {
                continue;
            }
            chess::Position          position = chess::Position::Start();
            std::vector<std::string> prefix;
            for (const chess::TextBookMove &move : line.moves)
            {
                const auto parsed = chess::ParseUciMove(position, move.coordinate);
                if (!parsed)
                {
                    break;
                }
                reached_by.try_emplace(chess::PolyglotKey(position), prefix);
                prefix.push_back(move.is_questionable ? move.coordinate + "?" : move.coordinate);
                position = position.MakeMove(*parsed);
            }
        }
        for (const std::uint64_t key : refilled)
        {
            const auto route = reached_by.find(key);
            if (route == reached_by.end())
            {
                // Unreachable as the book stands - every emptied position was reached by some line, which is how it
                // came to be in the index at all - but a silent `continue` here would drop a position that needs a
                // move and leave the book looking well-formed. That is exactly how the predecessor stayed broken.
                summary.problems.push_back("no way to reach the emptied position " + std::to_string(key) +
                                           ", so it was left with nothing to play");
                continue;
            }
            chess::TextBookLine added;
            added.tokens = route->second;
            added.tokens.push_back(references[key].move);
            for (const std::string &token : added.tokens)
            {
                chess::TextBookMove move;
                move.is_questionable = token.back() == '?';
                move.coordinate      = move.is_questionable ? token.substr(0, token.size() - 1) : token;
                added.moves.push_back(move);
            }
            book.lines.push_back(std::move(added));
            ++summary.refilled;
        }
    }

    std::ofstream output{settings.out, std::ios::binary};
    if (!output)
    {
        return Error{"cannot write '" + settings.out + "'"};
    }
    const std::string written = chess::WriteTextBook(book);
    output.write(written.data(), static_cast<std::streamsize>(written.size()));
    if (!output)
    {
        return Error{"writing '" + settings.out + "' failed"};
    }
    summary.lines_written = book.OpeningCount();
    return summary;
}

} // namespace amphitheater::match
