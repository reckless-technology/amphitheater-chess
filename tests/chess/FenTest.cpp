// FEN: strict and tolerant reading, every error a FEN can have, and the round trip.

#include "chess/Position.h"

#include <gtest/gtest.h>

namespace uci_host::chess
{
namespace
{

TEST(Fen, StartPositionRoundTrips)
{
    const Position start = Position::Start();
    EXPECT_EQ(start.ToFen(), Position::kStartFen);
    EXPECT_EQ(start.SideToMove(), kWhite);
    EXPECT_EQ(start.PieceOn(Square{"e1"}), kKing);
    EXPECT_EQ(start.ColorOn(Square{"e8"}), kBlack);
    EXPECT_EQ(start.Pieces(kPawn, kBlack).PopCount(), 8);
    EXPECT_FALSE(start.EnPassantSquare());
    EXPECT_EQ(start.HalfmoveClock(), 0);
    EXPECT_EQ(start.FullmoveNumber(), 1);
    EXPECT_TRUE(start.Castling().May(kWhite, true));
    EXPECT_TRUE(start.Castling().May(kBlack, false));
    EXPECT_FALSE(start.IsInCheck());
}

TEST(Fen, AssortedPositionsRoundTripStrictly)
{
    for (const char *fen : {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                            "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2",
                            "8/8/8/8/8/8/8/K6k b - - 99 150", "4k3/8/8/8/8/8/8/R3K3 w Q - 0 1",
                            "r3k3/8/8/8/8/8/8/4K3 b q - 7 40", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"})
    {
        const auto position = Position::FromFen(fen);
        ASSERT_TRUE(position) << fen << ": " << position.ErrorMessage();
        EXPECT_EQ(position->ToFen(), fen);
    }
}

TEST(Fen, TolerantReadsAnEpdAndRunsOfSpace)
{
    const auto position =
        Position::FromFen("  rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR   W  KQkq -  ", FenMode::kTolerant);
    ASSERT_TRUE(position) << position.ErrorMessage();
    EXPECT_EQ(position->ToFen(), Position::kStartFen);
    EXPECT_FALSE(Position::FromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -"));
    EXPECT_FALSE(Position::FromFen(" rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
    EXPECT_FALSE(Position::FromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq  - 0 1"));
    const auto zero = Position::FromFen("4k3/8/8/8/8/8/8/4K3 w - - 3 0", FenMode::kTolerant);
    ASSERT_TRUE(zero);
    EXPECT_EQ(zero->FullmoveNumber(), 1);
    EXPECT_FALSE(Position::FromFen("4k3/8/8/8/8/8/8/4K3 w - - 3 0"));
}

TEST(Fen, TolerantDropsWhatThePiecesDoNotAllow)
{
    // No rook on h1, no king on e8: those rights go; the en passant square has no pawn behind it.
    const auto position = Position::FromFen("r3k3/8/8/8/8/8/8/R3K3 w KQkq e6 0 1", FenMode::kTolerant);
    ASSERT_TRUE(position) << position.ErrorMessage();
    EXPECT_EQ(position->ToFen(), "r3k3/8/8/8/8/8/8/R3K3 w Qq - 0 1");
    const auto shredder = Position::FromFen("r3k2r/8/8/8/8/8/8/R3K2R w HAha - 0 1", FenMode::kTolerant);
    ASSERT_TRUE(shredder);
    EXPECT_EQ(shredder->Castling().ToFenString(), "KQkq");
    EXPECT_FALSE(Position::FromFen("r3k2r/8/8/8/8/8/8/R3K2R w HAha - 0 1"));
}

TEST(Fen, StrictRefusesEveryKindOfMistake)
{
    const struct
    {
        const char *fen;
        const char *reason;
    } cases[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1", "eight ranks"},
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR/8 w KQkq - 0 1", "more than eight ranks"},
        {"rnbqkbnr/ppppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "more than 8 squares"},
        {"rnbqkbnr/ppp6p/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "more than 8 squares"},
        {"rnbqkbnr/ppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "7 squares"},
        {"rnbqkbnr/ppppxppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "not a piece letter"},
        {"rnbq1bnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQ - 0 1", "exactly one king"},
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKKNR w KQkq - 0 1", "exactly one king"},
        {"4k3/pppppppp/p7/8/8/8/8/4K3 w - - 0 1", "more than eight pawns"},
        {"4k3/8/8/8/8/8/8/4K2p w - - 0 1", "first or the last rank"},
        {"4k3/8/8/8/8/8/8/4K3 x - - 0 1", "side to move"},
        {"4k3/8/8/8/8/8/8/R3K2r b - - 0 1", "not to move is in check"},
        {"4k3/8/8/8/8/8/8/4K3 w X - 0 1", "not a castling right"},
        {"4k3/8/8/8/8/8/8/4K3 w K - 0 1", "no king and rook"},
        {"4k2r/8/8/8/8/8/8/4K2R w KK - 0 1", "appears twice"},
        {"4k3/8/8/8/8/8/8/4K3 w - e9 0 1", "not an en passant square"},
        {"4k3/8/8/8/8/8/8/4K3 w - e6 0 1", "double step"},
        {"4k3/8/8/8/8/8/8/4K3 w - - x 1", "halfmove clock"},
        {"4k3/8/8/8/8/8/8/4K3 w - - 0 -1", "move number"},
        {"4k3/8/8/8/8/8/8/4K3 w - - 0", "six fields"},
    };

    for (const auto &entry : cases)
    {
        const auto position = Position::FromFen(entry.fen);
        ASSERT_FALSE(position) << entry.fen << " read as " << position->ToFen();
        EXPECT_NE(position.ErrorMessage().find(entry.reason), std::string::npos)
            << entry.fen << " said: " << position.ErrorMessage();
    }
    // Too many pieces: seventeen white men (eight pawns, nine pieces).
    EXPECT_FALSE(Position::FromFen("4k3/8/8/8/NNNN4/NNNN4/PPPPPPPP/4K3 w - - 0 1"));
    EXPECT_TRUE(Position::FromFen("4k3/8/8/8/NNNN4/NNNN4/PPPPPPPP/4K3 w - - 0 1", FenMode::kTolerant));
    EXPECT_FALSE(Position::FromFen("4k3/8/8/8/8/8/8/4K3 w - - 0 1 extra"));
}

TEST(Fen, EnPassantSquareIsCheckedOnBothSides)
{
    const auto white = Position::FromFen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2");
    ASSERT_TRUE(white) << white.ErrorMessage();
    ASSERT_TRUE(white->EnPassantSquare());
    EXPECT_EQ(white->EnPassantSquare()->ToString(), "d6");
    EXPECT_TRUE(white->HasLegalEnPassantCapture());
    const auto black = Position::FromFen("4k3/8/8/8/3Pp3/8/8/4K3 b - d3 0 1");
    ASSERT_TRUE(black) << black.ErrorMessage();
    EXPECT_TRUE(black->HasLegalEnPassantCapture());
    EXPECT_FALSE(Position::FromFen("4k3/8/8/8/3Pp3/8/8/4K3 b - d6 0 1"));
}

TEST(Fen, EqualityComparesEveryField)
{
    const Position start = Position::Start();
    auto           other = *Position::FromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 2");
    EXPECT_FALSE(start == other);
    EXPECT_TRUE(start == *Position::FromFen(Position::kStartFen));
}

} // namespace
} // namespace uci_host::chess
