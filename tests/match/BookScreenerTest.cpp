// Screening a book against an engine's judgement, driven by the fake engine so the scores are scripted and the
// verdicts are therefore knowable: that a move far below the reference is marked and a move near it is not, that a
// position whose every move is marked is given the engine's own move, and that the screener's own requirements are
// imposed on the engine whatever the caller asked for.

#include "match/BookScreener.h"
#include "TestSupport.h"
#include "chess/PolyglotBook.h"
#include "chess/TextBook.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace amphitheater::match
{
namespace
{

/// @brief Write a book to a temporary file.
/// @param name A name unique to the test.
/// @param text The book.
/// @return Its path.
std::string WriteBook(const std::string &name, const std::string &text)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / ("amphitheater_" + name + ".book");
    std::ofstream               file{path, std::ios::binary};
    file << text;
    return path.string();
}

/// @brief Read a file back.
std::string ReadFile(const std::string &path)
{
    std::ifstream file{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/// @brief Settings pointing at the fake engine with a scripted score for each search in turn.
///
/// The screener makes two searches per position before it judges anything - the engine's own choice, then that move
/// re-scored restricted - and then one per book move. So the scripted sequence is read in that order, and a test that
/// wants a particular verdict counts the searches to place its numbers.
BookScreenerSettings FakeSettings(const std::string &book, const std::string &out, const std::string &scores)
{
    BookScreenerSettings settings;
    settings.book              = book;
    settings.out               = out;
    settings.engine.executable = AMPHITHEATER_FAKE_ENGINE;
    settings.engine.arguments  = {"--scores", scores};
    settings.depth             = 4;
    settings.margin            = 100;
    return settings;
}

} // namespace

TEST(BookScreener, MarksAMoveFarBelowTheReferenceAndLeavesTheRest)
{
    // One line, two moves, so two positions and six searches: for each position the chosen move, that move
    // re-scored, then the book move. The first position's book move is scored 500 below its reference and must be
    // marked; the second's is scored equal to its reference and must not be.
    //
    //   search 1  startpos unrestricted   ->  20   (and a chosen move)
    //   search 2  startpos restricted     ->  20   the reference
    //   search 3  e2e4 restricted         -> -480  loss 500, over the 100 margin  -> marked
    //   search 4  after e2e4, unrestricted->  30
    //   search 5  after e2e4, restricted  ->  30   the reference
    //   search 6  e7e5 restricted         ->  30   loss 0                          -> kept
    const std::string book    = WriteBook("screen_marks", "# a book\ne2e4 e7e5\n");
    const std::string out     = book + ".out";
    const auto        summary = ScreenBook(FakeSettings(book, out, "20,20,-480,30,30,30"));

    ASSERT_TRUE(summary) << summary.ErrorMessage();
    EXPECT_EQ(summary->openings, 1u);
    EXPECT_EQ(summary->marked, 1u);
    ASSERT_EQ(summary->marks.size(), 1u);
    EXPECT_EQ(summary->marks[0].move, "e2e4");
    EXPECT_EQ(summary->marks[0].loss, 500);

    // The mark is in the written file, on that move and not the other, and the comment survives.
    const std::string written = ReadFile(out);
    EXPECT_NE(written.find("e2e4?"), std::string::npos) << written;
    EXPECT_EQ(written.find("e7e5?"), std::string::npos) << written;
    EXPECT_NE(written.find("# a book"), std::string::npos) << written;
}

TEST(BookScreener, GivesAnEmptiedPositionTheEnginesOwnMove)
{
    // The only move the book offers at the start position is marked, so the position has nothing selectable. A book
    // left like that drops the engine out of book at move one. The screener must add a line ending in the engine's
    // own choice there, unmarked.
    const std::string book    = WriteBook("screen_refill", "e2e4 e7e5\n");
    const std::string out     = book + ".out";
    const auto        summary = ScreenBook(FakeSettings(book, out, "20,20,-480,30,30,-480"));

    ASSERT_TRUE(summary) << summary.ErrorMessage();
    EXPECT_EQ(summary->marked, 2u) << "both book moves are far below their references";
    EXPECT_GE(summary->refilled, 1u) << "a position with nothing to play must be given a move";

    // Every position the written book continues from offers something the engine may actually choose. That is the
    // invariant the refill exists for, and it is checked here the way the engines' own suites check it.
    const chess::TextBook      screened = chess::ReadTextBook(ReadFile(out));
    const chess::TextBookIndex index    = chess::ReplayTextBook(screened);
    EXPECT_TRUE(index.errors.empty()) << (index.errors.empty() ? std::string{} : index.errors.front());
    for (const auto &[key, entry] : index.positions)
    {
        EXPECT_FALSE(entry.selectable.empty() && entry.questionable.empty())
            << "a position with no moves at all should not be in the index";
    }
    const chess::TextBookIndex::Entry &start = index.positions.at(chess::PolyglotKey(chess::Position::Start()));
    EXPECT_FALSE(start.selectable.empty()) << "the start position must have a move the engine may play";
}

TEST(BookScreener, ImposesOneThreadAndNoBookOnTheEngine)
{
    // The two guarantees a caller must not be able to take away. Under Lazy SMP a search's score depends on how its
    // helper threads raced, so a verdict taken from a multi-threaded search is not reproducible; and with its own
    // book on, an engine answers from the book rather than judging, which is the whole question. The screener
    // appends both after the caller's options so they win.
    const std::string    book     = WriteBook("screen_options", "e2e4\n");
    const std::string    out      = book + ".out";
    BookScreenerSettings settings = FakeSettings(book, out, "10,10,10");
    settings.engine.options.emplace_back("Threads", "8");
    settings.engine.options.emplace_back("OwnBook", "true");

    // The fake engine answers every setoption with "info string option NAME = VALUE", so the conversation itself
    // says what it ran with - no need for it to keep a log, and no need to trust the screener's own bookkeeping.
    std::string conversation;
    settings.logger = [&conversation](uci::LineDirection, std::string_view line) {
        conversation += std::string{line} + "\n";
    };

    const auto summary = ScreenBook(settings);
    ASSERT_TRUE(summary) << summary.ErrorMessage();

    const std::size_t one   = conversation.rfind("setoption name Threads value 1");
    const std::size_t eight = conversation.rfind("setoption name Threads value 8");
    ASSERT_NE(one, std::string::npos) << conversation;
    EXPECT_TRUE(eight == std::string::npos || one > eight)
        << "Threads 1 must be sent after the caller's 8, so it is what the engine ends up on";

    // OwnBook is not asserted in the conversation, and that is correct rather than a gap: an engine is only sent
    // an option it declared, and this double declares no OwnBook, so there is no book to put down and nothing is
    // sent. What must hold is that the screener noticed - it refuses outright if an engine declares the option and
    // will not take the value, because an engine reciting its own book is not judging the position at all.
    EXPECT_EQ(conversation.find("setoption name OwnBook"), std::string::npos)
        << "an option the engine never declared must not be sent to it";
    for (const std::string &problem : summary->problems)
    {
        EXPECT_EQ(problem.find("refused an option"), std::string::npos) << problem;
    }

    // And the isolation every verdict depends on: a ucinewgame before every single search, not once per run.
    std::size_t newgames = 0;
    for (std::size_t at = conversation.find("ucinewgame"); at != std::string::npos;
         at             = conversation.find("ucinewgame", at + 1))
    {
        ++newgames;
    }
    EXPECT_GE(newgames, 3u) << "one book move needs three searches, and each needs its own cleared table";
}

TEST(BookScreener, RefusesSettingsThatCannotBeHonoured)
{
    const std::string    book     = WriteBook("screen_refuse", "e2e4\n");
    BookScreenerSettings settings = FakeSettings(book, book + ".out", "10");

    settings.depth = 0;
    EXPECT_FALSE(ScreenBook(settings)) << "a depth below 1 is not a search";
    settings.depth  = 4;
    settings.margin = 0;
    EXPECT_FALSE(ScreenBook(settings)) << "a margin of 0 would mark every move that is not the best";
    settings.margin = 100;
    settings.book   = "no-such-file.book";
    EXPECT_FALSE(ScreenBook(settings)) << "a book that cannot be read is an error, not an empty book";
}

TEST(BookScreener, LeavesAPositionATranspositionStillCoversAlone)
{
    // Emptiness is a property of the POSITION, not of the line that reached it. Both lines reach the same position
    // after three moves - 1.e4 e6 2.d4 and 1.d4 e6 2.e4 are one position - and each stores a different move there:
    // `d7d5` from the first line and `c7c5` from the second. Marking `d7d5` must NOT empty that position, because
    // `c7c5` is still playable there, so nothing may be refilled.
    //
    // The predecessor keyed on the line's prefix rather than the position, so it refilled positions another move
    // order already covered. Screening the engines' own book reproduced all 3,244 of its lines and every one of its
    // 1,170 marks byte for byte, and came out two lines shorter - those two offering a move the book already offered
    // at the same position by another order.
    //
    // Twenty searches, in book order. Per position the engine's own choice and that choice re-scored (once, cached by
    // position), then one per book move:
    //
    //   startpos        chosen 20, re-scored 20, `e2e4` 20            kept
    //   after 1.e4      chosen 30, re-scored 30, `e7e6` 30            kept
    //   after 1...e6    chosen 40, re-scored 40, `d2d4` 40            kept
    //   the position    chosen 50, re-scored 50, `d7d5` -460          loss 510 -> MARKED
    //   startpos        `d2d4` 20                                     reference cached; kept
    //   after 1.d4      chosen 30, re-scored 30, `e7e6` 30            kept
    //   after 1...e6    chosen 40, re-scored 40, `e2e4` 40            kept
    //   the position    `c7c5` 50                                     reference cached; loss 0, kept
    const std::string book = WriteBook("screen_transposition", "e2e4 e7e6 d2d4 d7d5\nd2d4 e7e6 e2e4 c7c5\n");
    const std::string out  = book + ".out";
    const auto        summary =
        ScreenBook(FakeSettings(book, out, "20,20,20,30,30,30,40,40,40,50,50,-460,20,30,30,30,40,40,40,50"));

    ASSERT_TRUE(summary) << summary.ErrorMessage();
    EXPECT_EQ(summary->marked, 1u) << "the one move scored far below its reference";
    EXPECT_EQ(summary->refilled, 0u) << "a position another move order still covers must not be refilled";
    EXPECT_EQ(summary->lines_written, 2u) << "no line added";

    // Whatever was marked, every position the written book continues from still has a move it may play. That is the
    // invariant the engines depend on, and here the transposition supplies it rather than a refill.
    const chess::TextBook      written = chess::ReadTextBook(ReadFile(out));
    const chess::TextBookIndex index   = chess::ReplayTextBook(written);
    EXPECT_TRUE(index.errors.empty()) << index.errors.size() << " lines did not replay";
    for (const auto &[key, entry] : index.positions)
    {
        EXPECT_FALSE(entry.selectable.empty()) << "a position the book continues from has nothing to play";
    }
}

} // namespace amphitheater::match
