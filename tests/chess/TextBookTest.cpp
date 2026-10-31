// The engines' text opening books: the grammar (comments, move numbers, the questionable mark), the round trip that
// a screening tool depends on, and what replaying a book says about each position it reaches.

#include "chess/TextBook.h"
#include "chess/Notation.h"
#include "chess/PolyglotBook.h"

#include "TestSupport.h"

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>

namespace amphitheater::chess
{
namespace
{

std::uint64_t KeyAfter(std::initializer_list<const char *> moves)
{
    Position position = Position::Start();
    for (const char *text : moves)
    {
        const auto move = ParseUciMove(position, text);
        EXPECT_TRUE(move) << text;
        position = position.MakeMove(*move);
    }
    return PolyglotKey(position);
}

/// A book exercising every part of the grammar at once: a header comment, a blank line, a plain line, a line with a
/// questionable move in the middle of it, move numbers, and a trailing comment on a line that has moves.
constexpr const char *kGrammar = "# Pawnstar's opening book.\n"
                                 "#   min games a move  8\n"
                                 "\n"
                                 "e2e4 e7e5 g1f3\n"
                                 "e2e4 g7g6 f2f4 f8g7? g1f3\n"
                                 "1. d2d4 d7d5 2. c2c4\n"
                                 "e2e4 c7c5 g1f3 # the Sicilian\n";

} // namespace

TEST(TextBook, ReadsTheGrammar)
{
    const TextBook book = ReadTextBook(kGrammar);

    ASSERT_EQ(book.lines.size(), 7u) << "two comments, a blank line and four openings";
    EXPECT_EQ(book.OpeningCount(), 4u);
    EXPECT_EQ(book.QuestionableTokenCount(), 1u);

    EXPECT_FALSE(book.lines[0].HasMoves());
    EXPECT_EQ(book.lines[0].comment, "# Pawnstar's opening book.");
    EXPECT_FALSE(book.lines[2].HasMoves()) << "the blank line is a line with nothing on it";

    // A move number is a token but not a move, so a line carrying them replays as three moves.
    EXPECT_EQ(book.lines[5].tokens.size(), 5u);
    EXPECT_EQ(book.lines[5].moves.size(), 3u);
    EXPECT_EQ(book.lines[5].moves[0].coordinate, "d2d4");

    // The questionable mark is separated from the move: a reader asking what move it is must not see the '?'.
    EXPECT_EQ(book.lines[4].moves[3].coordinate, "f8g7");
    EXPECT_TRUE(book.lines[4].moves[3].is_questionable);
    EXPECT_FALSE(book.lines[4].moves[4].is_questionable);

    // A comment after moves belongs to that line, and the moves before it are read as moves.
    EXPECT_EQ(book.lines[6].moves.size(), 3u);
    EXPECT_EQ(book.lines[6].comment, "# the Sicilian");
}

TEST(TextBook, WritesBackWhatItRead)
{
    // The contract a screening tool depends on. It rewrites a book in place, and a diff showing every line changed
    // would hide the lines it meant to change - so reading and writing must be the identity on an unchanged book.
    EXPECT_EQ(WriteTextBook(ReadTextBook(kGrammar)), std::string(kGrammar));
}

TEST(TextBook, WritesLfWhateverItRead)
{
    // These bytes are shipped in three repositories which check that all three agree, so a book written on Windows
    // must not differ from the same book written on Linux. A CR read is dropped rather than preserved.
    const std::string crlf    = "# a book\r\ne2e4 e7e5\r\n";
    const std::string written = WriteTextBook(ReadTextBook(crlf));
    EXPECT_EQ(written, "# a book\ne2e4 e7e5\n");
    EXPECT_EQ(written.find('\r'), std::string::npos);
}

TEST(TextBook, ReplayIndexesPositionsNotLines)
{
    const TextBookIndex index = ReplayTextBook(ReadTextBook(kGrammar));

    EXPECT_TRUE(index.errors.empty());
    EXPECT_EQ(index.replayed, 4u);

    // Three lines open 1.e4 and one opens 1.d4, and the start position holds each move once however many lines pass
    // through it: this index answers "what may be played here", not "how heavily".
    const TextBookIndex::Entry &start = index.positions.at(KeyAfter({}));
    EXPECT_EQ(start.selectable.size(), 2u);
    EXPECT_TRUE(start.questionable.empty());

    // The questionable move is recorded where it was played, and is not selectable there.
    const TextBookIndex::Entry &after_f4 = index.positions.at(KeyAfter({"e2e4", "g7g6", "f2f4"}));
    EXPECT_TRUE(after_f4.selectable.empty()) << "f8g7? is the only move here and it is marked";
    ASSERT_EQ(after_f4.questionable.size(), 1u);
    EXPECT_EQ(after_f4.questionable[0].ToUci(), "f8g7");

    // And the reply after it is selectable, which is the whole point of marking rather than deleting: the engine
    // keeps its answer to a move it would not play itself.
    const TextBookIndex::Entry &after_bishop = index.positions.at(KeyAfter({"e2e4", "g7g6", "f2f4", "f8g7"}));
    ASSERT_EQ(after_bishop.selectable.size(), 1u);
    EXPECT_EQ(after_bishop.selectable[0].ToUci(), "g1f3");
}

TEST(TextBook, ReportsABadLineAndKeepsTheRest)
{
    // One corrupt line is a line to fix. A reader that gave up on the file could not say which line was wrong, and a
    // reader that said nothing would let a truncated book load and play almost nothing.
    const TextBookIndex index = ReplayTextBook(ReadTextBook("e2e4 e7e5\n"
                                                            "e2e4 e7e9\n"
                                                            "d2d4 d7d5\n"));
    ASSERT_EQ(index.errors.size(), 1u);
    EXPECT_NE(index.errors[0].find("line 2"), std::string::npos) << index.errors[0];
    EXPECT_NE(index.errors[0].find("e7e9"), std::string::npos) << index.errors[0];
    EXPECT_EQ(index.replayed, 2u) << "the other two lines still replayed";

    // The legal prefix of the bad line is kept: e2e4 was played before the bad token.
    EXPECT_EQ(index.positions.at(KeyAfter({})).selectable.size(), 2u);
}

TEST(TextBook, RoundTripsAShippedBook)
{
    // The acceptance check for the format, against the article rather than a fixture: a book the engines actually
    // ship must read and write back byte for byte. A fixture cannot prove this - the grammar it exercises is the
    // grammar I thought of - and copying a 160 KB book in here would make a fourth copy of a file whose three
    // existing copies are deliberately identical. So the book is named from the environment, as the integration
    // suite names a real engine, and the test skips when it is not.
    const auto path = testing::EnvironmentVariable("AMPHITHEATER_TEST_BOOK");
    if (!path)
    {
        GTEST_SKIP() << "AMPHITHEATER_TEST_BOOK names no book";
    }
    std::ifstream file{*path, std::ios::binary};
    ASSERT_TRUE(file) << *path;
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    ASSERT_FALSE(text.empty()) << *path;

    const TextBook book = ReadTextBook(text);
    EXPECT_EQ(WriteTextBook(book), text) << "writing back " << *path << " did not reproduce it";

    // And it is a book, not just a header that happens to round-trip.
    EXPECT_GT(book.OpeningCount(), 385u) << "the 385-line book this one replaced is the floor";
    EXPECT_GT(book.QuestionableTokenCount(), 0u) << "a screened book carries '?' marks";

    const TextBookIndex index = ReplayTextBook(book);
    EXPECT_TRUE(index.errors.empty()) << index.errors.size() << " line(s) did not replay, first: "
                                      << (index.errors.empty() ? std::string{} : index.errors.front());
    EXPECT_EQ(index.replayed, book.OpeningCount()) << "every opening replayed from the start position";

    // The invariant the engines' own suites check from the other side: no position the book continues from is left
    // with nothing it may actually play, because a marked move cannot be chosen.
    std::size_t dead_ends = 0;
    for (const auto &[key, entry] : index.positions)
    {
        if (entry.selectable.empty())
        {
            ++dead_ends;
        }
    }
    EXPECT_EQ(dead_ends, 0u) << "positions whose every move is marked, so the engine would leave the book there";
}

} // namespace amphitheater::chess
