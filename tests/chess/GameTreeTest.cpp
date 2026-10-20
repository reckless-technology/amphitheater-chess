// A game with its variations: read from PGN and written back as it was, lines followed along their main
// continuations, a move added as a variation or found where it already is, a variation promoted and one taken off.

#include "chess/GameTree.h"
#include "chess/Notation.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace amphitheater::chess
{
namespace
{

/**
 * @param tree A tree.
 * @param line A line's nodes.
 * @return The line's moves in SAN, parted by spaces.
 */
std::string Sans(const GameTree &tree, const std::vector<GameTree::NodeId> &line)
{
    std::string text;
    for (const GameTree::NodeId id : line)
    {
        text += (text.empty() ? "" : " ") + tree.node(id).san;
    }
    return text;
}

/**
 * @param position A position.
 * @param san A move in it, in SAN.
 * @return The move.
 */
Move MoveOf(const Position &position, const std::string &san)
{
    auto move = ParseSan(position, san);
    EXPECT_TRUE(move) << san;
    return move ? *move : Move::None();
}

constexpr const char *kAnnotated = R"([Event "Variations"]
[Site "?"]
[Date "2026.10.20"]
[Round "-"]
[White "A"]
[Black "B"]
[Result "*"]

1. e4 {the king's pawn} e5 (1... c5 2. Nf3 (2. c3 d5) 2... d6) (1... e6 $2) 2. Nf3 $1 Nc6 3. Bb5 *

)";

} // namespace

TEST(GameTree, APgnGamesLinesAreReadAndWrittenBackAsTheyWere)
{
    std::vector<std::string> errors;
    const auto               games = ReadPgn(kAnnotated, &errors);
    ASSERT_EQ(games.size(), 1u) << (errors.empty() ? "" : errors.front());
    const GameTree tree = GameTree::FromPgn(games.front());
    EXPECT_EQ(Sans(tree, tree.MainLine()), "e4 e5 Nf3 Nc6 Bb5");

    PgnGame written = games.front();
    written.moves   = tree.ToPgnLine();
    EXPECT_EQ(WritePgn(written), WritePgn(games.front())) << "variations, comments and NAGs kept in their places";

    const GameTree::NodeId e4 = tree.MainLine().front();
    EXPECT_EQ(tree.node(e4).comment, "the king's pawn");
    ASSERT_EQ(tree.node(e4).children.size(), 3u) << "e5, then c5 and e6 as variations";
    EXPECT_EQ(tree.node(tree.node(e4).children[2]).nags, std::vector<int>{2});
}

TEST(GameTree, ALineRunsThroughANodeAndOnAlongTheMainContinuations)
{
    const GameTree         tree = GameTree::FromPgn(ReadPgn(kAnnotated).front());
    const GameTree::NodeId e4   = tree.MainLine().front();
    const GameTree::NodeId c5   = tree.node(e4).children[1];
    EXPECT_EQ(Sans(tree, tree.LineThrough(c5)), "e4 c5 Nf3 d6") << "on along the variation's own main line";
    const GameTree::NodeId c3 =
        tree.node(c5).children[0] == tree.LineThrough(c5)[2] ? tree.node(c5).children[1] : tree.node(c5).children[0];
    EXPECT_EQ(Sans(tree, tree.LineThrough(c3)), "e4 c5 c3 d5") << "a variation within a variation";
    EXPECT_EQ(tree.GameOf(tree.LineThrough(c3)).CurrentPosition().ToFen(),
              tree.node(tree.LineThrough(c3).back()).position.ToFen());
    EXPECT_FALSE(tree.IsMainContinuation(c5));
    EXPECT_TRUE(tree.IsMainContinuation(e4));
    EXPECT_TRUE(tree.IsMainContinuation(GameTree::kRoot));
    EXPECT_TRUE(GameTree().MainLine().empty());
}

TEST(GameTree, AMoveAddedIsTheMainContinuationOrAVariationOrTheOneAlreadyThere)
{
    GameTree               tree;
    const GameTree::NodeId e4 = tree.Add(GameTree::kRoot, MoveOf(Position::Start(), "e4"));
    ASSERT_NE(e4, GameTree::kNone);
    const Position         after_e4 = tree.node(e4).position;
    const GameTree::NodeId e5       = tree.Add(e4, MoveOf(after_e4, "e5"));
    const GameTree::NodeId c5       = tree.Add(e4, MoveOf(after_e4, "c5"));
    EXPECT_EQ(tree.node(e4).children, (std::vector<GameTree::NodeId>{e5, c5})) << "the first is the main line";
    EXPECT_EQ(tree.Add(e4, MoveOf(after_e4, "c5")), c5) << "a move already there is found, not added again";
    EXPECT_EQ(tree.Add(e4, MoveOf(Position::Start(), "d4")), GameTree::kNone) << "White's move, Black to move";
    EXPECT_EQ(tree.node(c5).ply, 2);
    EXPECT_EQ(tree.node(c5).san, "c5");

    const Game game = tree.GameOf(tree.LineThrough(c5));
    EXPECT_EQ(game.SanMoves(), (std::vector<std::string>{"e4", "c5"}));
    const GameTree linear = GameTree::FromGame(game);
    EXPECT_EQ(Sans(linear, linear.MainLine()), "e4 c5");
}

TEST(GameTree, AVariationPromotedBecomesTheMainLineAndOneRemovedIsGone)
{
    GameTree               tree = GameTree::FromPgn(ReadPgn(kAnnotated).front());
    const GameTree::NodeId e4   = tree.MainLine().front();
    const GameTree::NodeId e5   = tree.node(e4).children[0];
    const GameTree::NodeId c5   = tree.node(e4).children[1];
    const GameTree::NodeId e6   = tree.node(e4).children[2];

    tree.Promote(c5);
    EXPECT_EQ(tree.node(e4).children, (std::vector<GameTree::NodeId>{c5, e5, e6})) << "the old main line next";
    EXPECT_EQ(Sans(tree, tree.MainLine()), "e4 c5 Nf3 d6");
    EXPECT_TRUE(tree.IsMainContinuation(c5));

    tree.Remove(e5);
    EXPECT_EQ(tree.node(e4).children, (std::vector<GameTree::NodeId>{c5, e6}));
    tree.Remove(GameTree::kRoot);
    EXPECT_EQ(Sans(tree, tree.MainLine()), "e4 c5 Nf3 d6") << "the root is never taken off";

    PgnGame written        = ReadPgn(kAnnotated).front();
    written.moves          = tree.ToPgnLine();
    const std::string text = WritePgn(written);
    EXPECT_NE(text.find("1. e4 {the king's pawn} 1... c5 (1... e6 $2) 2. Nf3 (2. c3 d5) 2... d6"), std::string::npos)
        << text;
}

} // namespace amphitheater::chess
