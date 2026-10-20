// A position set up square by square: a position's pieces and fields read in and written back, pieces put, moved and
// taken off, castling rights and en passant squares held to what the pieces allow, and the position read back strictly
// with the reason it is not legal.

#include "chess/BoardSetup.h"

#include <gtest/gtest.h>

namespace amphitheater::chess
{
namespace
{

TEST(BoardSetup, APositionsFenRoundTrips)
{
    for (const char *fen :
         {Position::kStartFen.data(), "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
          "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2", "8/8/8/8/8/8/8/K6k b - - 99 150",
          "r3k3/8/8/8/8/8/8/4K3 b q - 7 40"})
    {
        const auto position = Position::FromFen(fen);
        ASSERT_TRUE(position) << fen;
        const BoardSetup setup = BoardSetup::FromPosition(*position);
        EXPECT_EQ(setup.ToFen(), fen);
        ASSERT_TRUE(setup.ToPosition()) << fen;
        EXPECT_EQ(setup.ToPosition()->ToFen(), fen);
    }
    EXPECT_EQ(BoardSetup::Start().ToFen(), Position::kStartFen);
}

TEST(BoardSetup, AnEmptyBoardIsWhiteToMoveAtMoveOneAndNoPosition)
{
    const BoardSetup setup;
    EXPECT_EQ(setup.ToFen(), "8/8/8/8/8/8/8/8 w - - 0 1");
    const auto position = setup.ToPosition();
    ASSERT_FALSE(position);
    EXPECT_EQ(position.ErrorMessage(), "white must have exactly one king, not 0");
}

TEST(BoardSetup, PiecesArePutMovedAndTakenOff)
{
    BoardSetup setup;
    setup.Put(Square{"e1"}, {kWhite, kKing});
    setup.Put(Square{"e8"}, {kBlack, kKing});
    setup.Put(Square{"d4"}, {kWhite, kQueen});
    EXPECT_EQ(setup.ToFen(), "4k3/8/8/8/3Q4/8/8/4K3 w - - 0 1");
    EXPECT_TRUE(setup.ToPosition());

    setup.Move(Square{"d4"}, Square{"d7"});
    EXPECT_EQ(setup.At(Square{"d4"}), SetupPiece{});
    EXPECT_EQ(setup.At(Square{"d7"}), (SetupPiece{kWhite, kQueen}));
    setup.Put(Square{"d7"}, {kBlack, kKnight}); // replaces the queen
    EXPECT_EQ(setup.ToFen(), "4k3/3n4/8/8/8/8/8/4K3 w - - 0 1");
    setup.Put(Square{"d7"}, {});
    EXPECT_EQ(setup.ToFen(), "4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    setup.Move(Square{"e1"}, Square{"e1"});
    EXPECT_EQ(setup.At(Square{"e1"}), (SetupPiece{kWhite, kKing})) << "a move onto its own square changes nothing";

    setup.Clear();
    EXPECT_EQ(setup.ToFen(), "8/8/8/8/8/8/8/8 w - - 0 1");
}

TEST(BoardSetup, CastlingRightsAreHeldToTheKingAndTheRookOnTheirSquares)
{
    BoardSetup setup = BoardSetup::Start();
    EXPECT_TRUE(setup.castling(kWhite, true));
    setup.Move(Square{"h1"}, Square{"h3"});
    EXPECT_FALSE(setup.castling(kWhite, true)) << "the rook left its corner: the right goes";
    EXPECT_FALSE(setup.CanCastle(kWhite, true));
    setup.Move(Square{"h3"}, Square{"h1"});
    EXPECT_FALSE(setup.castling(kWhite, true)) << "a right gone is not given back by the pieces alone";
    setup.SetCastling(kWhite, true, true);
    EXPECT_TRUE(setup.castling(kWhite, true));

    setup.Put(Square{"e8"}, {});
    EXPECT_FALSE(setup.castling(kBlack, true));
    EXPECT_FALSE(setup.castling(kBlack, false));
    setup.SetCastling(kBlack, false, true);
    EXPECT_FALSE(setup.castling(kBlack, false)) << "never given where the pieces do not allow it";
    setup.Put(Square{"e8"}, {kWhite, kKing});
    EXPECT_FALSE(setup.CanCastle(kBlack, true)) << "a king of the other colour is no king of Black's";
}

TEST(BoardSetup, EnPassantSquaresAreBehindAPawnThatCouldHaveJustMadeADoubleStep)
{
    BoardSetup setup = BoardSetup::Start();
    setup.Move(Square{"e2"}, Square{"e4"});
    setup.SetSideToMove(kBlack);
    EXPECT_EQ(setup.EnPassantSquares(), std::vector<Square>{Square{"e3"}});
    setup.SetEnPassant(Square{"e3"});
    EXPECT_EQ(setup.ToFen(), "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    EXPECT_TRUE(setup.ToPosition());

    setup.SetEnPassant(Square{"d3"});
    EXPECT_EQ(setup.en_passant(), std::optional<Square>{Square{"e3"}}) << "a square no pawn passed is ignored";
    setup.SetSideToMove(kWhite);
    EXPECT_FALSE(setup.en_passant()) << "White to move: White's own double step allows none";
    setup.SetSideToMove(kBlack);
    setup.SetEnPassant(Square{"e3"});
    setup.Put(Square{"e3"}, {kWhite, kKnight});
    EXPECT_FALSE(setup.en_passant()) << "a piece on the square it passed: no double step";
    setup.SetEnPassant(std::nullopt);
    EXPECT_FALSE(setup.en_passant());

    setup = BoardSetup::Start();
    setup.Move(Square{"d7"}, Square{"d5"});
    setup.Move(Square{"g7"}, Square{"g5"});
    EXPECT_EQ(setup.EnPassantSquares(), (std::vector<Square>{Square{"d6"}, Square{"g6"}})) << "in file order";
}

TEST(BoardSetup, TheCountersAreHeldToTheirRanges)
{
    BoardSetup setup = BoardSetup::Start();
    setup.SetMoveNumber(0);
    setup.SetHalfmoveClock(-3);
    EXPECT_EQ(setup.move_number(), 1);
    EXPECT_EQ(setup.halfmove_clock(), 0);
    setup.SetMoveNumber(42);
    setup.SetHalfmoveClock(17);
    EXPECT_EQ(setup.ToFen(), "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 17 42");
}

TEST(BoardSetup, APositionThatCouldNotAriseSaysWhy)
{
    BoardSetup setup;
    setup.Put(Square{"e1"}, {kWhite, kKing});
    setup.Put(Square{"e8"}, {kBlack, kKing});
    setup.Put(Square{"e7"}, {kWhite, kQueen});
    setup.SetSideToMove(kWhite);
    EXPECT_EQ(setup.ToPosition().ErrorMessage(), "the side not to move is in check");
    setup.SetSideToMove(kBlack);
    EXPECT_TRUE(setup.ToPosition());

    setup.Put(Square{"a8"}, {kWhite, kPawn});
    EXPECT_EQ(setup.ToPosition().ErrorMessage(), "a pawn stands on the first or the last rank");
    setup.Put(Square{"a8"}, {kWhite, kKing});
    EXPECT_EQ(setup.ToPosition().ErrorMessage(), "white must have exactly one king, not 2");
}

} // namespace
} // namespace amphitheater::chess
