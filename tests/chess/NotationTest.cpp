// SAN and UCI notation: printing, parsing, every tolerance, every error, and the round trip over every legal move of
// the perft suite's positions.

#include "chess/Notation.h"

#include <gtest/gtest.h>

#include <fstream>
#include <string>

namespace uci_host::chess
{
namespace
{

Position FromFen(const char *fen)
{
    const auto position = Position::FromFen(fen, FenMode::kTolerant);
    EXPECT_TRUE(position) << fen << ": " << position.ErrorMessage();
    return *position;
}

std::string San(const Position &position, const char *uci)
{
    const auto move = ParseUciMove(position, uci);
    EXPECT_TRUE(move) << uci << ": " << move.ErrorMessage();
    return MoveToSan(position, *move);
}

TEST(Notation, PrintsSanForEveryKindOfMove)
{
    const Position start = Position::Start();
    EXPECT_EQ(San(start, "e2e4"), "e4");
    EXPECT_EQ(San(start, "g1f3"), "Nf3");
    const Position kiwipete = FromFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    EXPECT_EQ(San(kiwipete, "e1g1"), "O-O");
    EXPECT_EQ(San(kiwipete, "e1c1"), "O-O-O");
    EXPECT_EQ(San(kiwipete, "d5e6"), "dxe6");
    EXPECT_EQ(San(kiwipete, "e5f7"), "Nxf7");
    EXPECT_EQ(San(kiwipete, "f3h3"), "Qxh3");
    EXPECT_EQ(San(FromFen("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1"), "b7b8q"), "b8=Q+");
    EXPECT_EQ(San(FromFen("2r1k3/1P6/8/8/8/8/8/4K3 w - - 0 1"), "b7c8n"), "bxc8=N");
    EXPECT_EQ(San(FromFen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2"), "e5d6"), "exd6");
    EXPECT_EQ(MoveToSan(start, Move::None()), "--");
}

TEST(Notation, MarksCheckAndMate)
{
    const Position fools = FromFen("rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq - 0 2");
    EXPECT_EQ(San(fools, "d8h4"), "Qh4#");
    EXPECT_EQ(San(FromFen("4k3/8/8/8/8/8/8/R3K3 w Q - 0 1"), "a1a8"), "Ra8+");
}

TEST(Notation, DisambiguatesByFileThenRankThenSquare)
{
    // Two knights on one rank: the file.
    EXPECT_EQ(San(FromFen("4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1"), "b1d2"), "Nbd2");
    // Two rooks on one file: the rank.
    EXPECT_EQ(San(FromFen("4k3/R7/8/8/8/8/R7/4K3 w - - 0 1"), "a2a4"), "R2a4");
    // Three queens: a file and a rank are both shared, so the square.
    const Position queens = FromFen("7k/8/8/8/Q1Q5/8/Q7/4K3 w - - 0 1");
    EXPECT_EQ(San(queens, "a4b3"), "Qa4b3");
    EXPECT_EQ(San(queens, "c4b3"), "Qcb3");
    // A pinned knight cannot move, so the other needs no disambiguation.
    EXPECT_EQ(San(FromFen("4k3/4r3/8/8/8/8/4N3/1N2K3 w - - 0 1"), "b1c3"), "Nc3");
}

TEST(Notation, ParsesSanTolerantly)
{
    const Position kiwipete = FromFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    for (const char *text : {"O-O", "0-0", "o-o", "O-O+", " O-O "})
    {
        const auto move = ParseSan(kiwipete, text);
        ASSERT_TRUE(move) << text << ": " << move.ErrorMessage();
        EXPECT_EQ(move->ToUci(), "e1g1") << text;
    }
    EXPECT_EQ(ParseSan(kiwipete, "0-0-0")->ToUci(), "e1c1");

    const struct
    {
        const char *text;
        const char *uci;
    } cases[] = {
        {"Nxf7", "e5f7"},   {"Nf7", "e5f7"},    {"Nexf7", "e5f7"},  {"N5f7", "e5f7"}, {"Ne5xf7", "e5f7"},
        {"Ne5-f7", "e5f7"}, {"Nxf7!?", "e5f7"}, {"Nxf7?!", "e5f7"}, {"dxe6", "d5e6"}, {"de6", "d5e6"},
        {"Qxh3", "f3h3"},   {"g2xh3", "g2h3"},  {"e1g1", "e1g1"},   {"e1h1", "e1g1"}, {"a2a3", "a2a3"},
        {"a2-a3", "a2a3"},  {"a3", "a2a3"},     {"Kf1", "e1f1"},    {"kf1", "e1f1"},  {"qxh3", "f3h3"},
    };

    for (const auto &entry : cases)
    {
        const auto move = ParseSan(kiwipete, entry.text);
        ASSERT_TRUE(move) << entry.text << ": " << move.ErrorMessage();
        EXPECT_EQ(move->ToUci(), entry.uci) << entry.text;
    }
    const Position promotion = FromFen("2r1k3/1P6/8/8/8/8/8/4K3 w - - 0 1");
    for (const char *text : {"bxc8=Q+", "bxc8Q", "bxc8(Q)", "bxc8=q", "b7c8q", "bc8Q"})
    {
        const auto move = ParseSan(promotion, text);
        ASSERT_TRUE(move) << text << ": " << move.ErrorMessage();
        EXPECT_EQ(move->ToUci(), "b7c8q") << text;
    }
    const Position en_passant = FromFen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2");
    EXPECT_EQ(ParseSan(en_passant, "exd6e.p.")->ToUci(), "e5d6");
    EXPECT_EQ(ParseSan(en_passant, "exd6 e.p.")->ToUci(), "e5d6");
}

TEST(Notation, RefusesIllegalAmbiguousAndIncompleteSan)
{
    const Position knights   = FromFen("4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1");
    const auto     ambiguous = ParseSan(knights, "Nd2");
    ASSERT_FALSE(ambiguous);
    EXPECT_NE(ambiguous.ErrorMessage().find("ambiguous"), std::string::npos);
    EXPECT_FALSE(ParseSan(knights, "Nd4"));
    EXPECT_FALSE(ParseSan(knights, ""));
    EXPECT_FALSE(ParseSan(knights, "+"));
    EXPECT_FALSE(ParseSan(knights, "O-O"));
    EXPECT_FALSE(ParseSan(knights, "Zz9"));
    EXPECT_FALSE(ParseSan(knights, "N"));
    EXPECT_FALSE(ParseSan(knights, "Nbbd2"));
    EXPECT_FALSE(ParseSan(knights, "Nd2d2d2"));
    const auto promotion = ParseSan(FromFen("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1"), "b8");
    ASSERT_FALSE(promotion);
    EXPECT_NE(promotion.ErrorMessage().find("promotion"), std::string::npos);
    EXPECT_FALSE(ParseSan(FromFen("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1"), "b8=K"));
}

TEST(Notation, ParsesAndRefusesUci)
{
    const Position start = Position::Start();
    EXPECT_EQ(ParseUciMove(start, "e2e4")->ToUci(), "e2e4");
    EXPECT_EQ(ParseUciMove(start, " g1f3 ")->ToUci(), "g1f3");
    EXPECT_FALSE(ParseUciMove(start, "e2e5"));
    EXPECT_FALSE(ParseUciMove(start, "e2"));
    EXPECT_FALSE(ParseUciMove(start, "e2e4qq"));
    EXPECT_FALSE(ParseUciMove(start, "z2e4"));
    EXPECT_FALSE(ParseUciMove(start, "e2e4x"));
    EXPECT_FALSE(ParseUciMove(start, "0000"));
    EXPECT_FALSE(ParseUciMove(start, "(none)"));
    const Position promotion = FromFen("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1");
    EXPECT_EQ(ParseUciMove(promotion, "b7b8n")->PromotedPiece(), kKnight);
    EXPECT_FALSE(ParseUciMove(promotion, "b7b8"));
    EXPECT_FALSE(ParseUciMove(promotion, "b7b8k"));
    EXPECT_EQ(MoveToUci(Move::None()), "0000");
}

TEST(Notation, EveryLegalMoveOfThePerftSuiteRoundTrips)
{
    std::ifstream file(std::string(UCI_HOST_TEST_DATA_DIR) + "/standard-perft.epd");
    std::string   line;
    int           moves = 0;
    while (std::getline(file, line))
    {
        const auto fen      = line.substr(0, line.find(';'));
        const auto position = Position::FromFen(fen, FenMode::kTolerant);
        ASSERT_TRUE(position) << fen;
        // The position and every position one move on, so checks, mates and promotions all appear.
        std::vector<Position> positions{*position};
        for (const Move &move : position->GenerateLegalMoves())
        {
            positions.push_back(position->MakeMove(move));
        }
        for (const Position &each : positions)
        {
            for (const Move &move : each.GenerateLegalMoves())
            {
                const std::string san  = MoveToSan(each, move);
                const auto        back = ParseSan(each, san);
                ASSERT_TRUE(back) << each.ToFen() << " " << san << ": " << back.ErrorMessage();
                EXPECT_EQ(*back, move) << each.ToFen() << " " << san;
                const auto uci = ParseUciMove(each, MoveToUci(move));
                ASSERT_TRUE(uci);
                EXPECT_EQ(*uci, move);
                ++moves;
            }
        }
    }
    EXPECT_GT(moves, 20000);
    std::printf("notation: %d moves round-tripped through SAN and UCI\n", moves);
}

} // namespace
} // namespace uci_host::chess
