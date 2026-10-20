// An opening book read from PGN (PgnBook.h): each move weighted by the games that play it in its position, a game's
// variations counted with it and a game counted once a move, the depth it reaches, and the files it refuses.

#include "chess/PgnBook.h"
#include "chess/Notation.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

namespace amphitheater::chess
{
namespace
{

/** Three games: 1.e4 e5 2.Nf3, 1.e4 c5 with 1...e5 2.Nc3 beside it, and 1.d4. */
constexpr const char *kGames = "[Event \"a\"]\n\n1. e4 e5 2. Nf3 *\n\n"
                               "[Event \"b\"]\n\n1. e4 c5 (1... e5 2. Nc3) (1... e5 2. Nf3) *\n\n"
                               "[Event \"c\"]\n\n1. d4 *\n";

/**
 * A book's moves in a position, by SAN.
 * @param book The book.
 * @param position The position.
 * @return Each move's weight.
 */
std::map<std::string, int> MovesOf(const PolyglotBook &book, const Position &position)
{
    std::map<std::string, int> moves;
    for (const auto &[move, weight] : book.Moves(position))
    {
        moves[MoveToSan(position, move)] = weight;
    }
    return moves;
}

/**
 * The position after some moves from the start.
 * @param sans The moves.
 * @return The position.
 */
Position After(std::initializer_list<const char *> sans)
{
    Position position = Position::Start();
    for (const char *san : sans)
    {
        position = position.MakeMove(*ParseSan(position, san));
    }
    return position;
}

} // namespace

TEST(PgnBook, EachMoveIsWeightedByTheGamesThatPlayItThereVariationsIncluded)
{
    const auto book = BookFromPgn(kGames);
    ASSERT_TRUE(book) << book.ErrorMessage();
    EXPECT_EQ(MovesOf(*book, Position::Start()), (std::map<std::string, int>{{"e4", 2}, {"d4", 1}}));
    EXPECT_EQ(MovesOf(*book, After({"e4"})), (std::map<std::string, int>{{"e5", 2}, {"c5", 1}}))
        << "the second game plays 1...e5 in two variations, and counts once";
    EXPECT_EQ(MovesOf(*book, After({"e4", "e5"})), (std::map<std::string, int>{{"Nf3", 2}, {"Nc3", 1}}));
}

TEST(PgnBook, ReachesOnlyThePliesAskedFor)
{
    const auto book = BookFromPgn(kGames, 1);
    ASSERT_TRUE(book);
    EXPECT_EQ(MovesOf(*book, Position::Start()).size(), 2u);
    EXPECT_TRUE(MovesOf(*book, After({"e4"})).empty());
}

TEST(PgnBook, RefusesATextWithNoGameOrNoMove)
{
    std::vector<std::string> errors;
    EXPECT_FALSE(BookFromPgn("", kPgnBookPlies, &errors));
    const auto empty = BookFromPgn("[Event \"a\"]\n\n*\n");
    ASSERT_FALSE(empty);
    EXPECT_EQ(empty.ErrorMessage(), "its games have no moves");
}

} // namespace amphitheater::chess
