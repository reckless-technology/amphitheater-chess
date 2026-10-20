// Polyglot books: keys against the test keys the format's description publishes ("PolyGlot book format",
// hgm.nubati.net/book_format.html), move packing with castling as the king taking its rook, and a book built here,
// round-tripped through its bytes and a file.

#include "chess/Notation.h"
#include "chess/PolyglotBook.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>

namespace amphitheater::chess
{
namespace
{

Position After(std::initializer_list<const char *> moves)
{
    Position position = Position::Start();
    for (const char *text : moves)
    {
        const auto move = ParseUciMove(position, text);
        EXPECT_TRUE(move) << text;
        position = position.MakeMove(*move);
    }
    return position;
}

TEST(Polyglot, KeysMatchTheFormatsTestKeys)
{
    const struct
    {
        std::initializer_list<const char *> moves;
        const char                         *fen;
        std::uint64_t                       key;
    } cases[] = {
        {{}, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 0x463b96181691fc9cULL},
        {{"e2e4"}, "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", 0x823c9b50fd114196ULL},
        {{"e2e4", "d7d5"}, "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2", 0x0756b94461c50fb0ULL},
        {{"e2e4", "d7d5", "e4e5"},
         "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2",
         0x662fafb965db29d4ULL},
        // An en passant capture is possible: the file counts.
        {{"e2e4", "d7d5", "e4e5", "f7f5"},
         "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3",
         0x22a48b5a8e47ff78ULL},
        // A king's move: castling rights go.
        {{"e2e4", "d7d5", "e4e5", "f7f5", "e1e2"},
         "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPPKPPP/RNBQ1BNR b kq - 0 3",
         0x652a607ca3f242c1ULL},
        {{"e2e4", "d7d5", "e4e5", "f7f5", "e1e2", "e8f7"},
         "rnbq1bnr/ppp1pkpp/8/3pPp2/8/8/PPPPKPPP/RNBQ1BNR w - - 0 4",
         0x00fdd303c946bdd9ULL},
        // A double step beside a pawn of the side to move.
        {{"a2a4", "b7b5", "h2h4", "b5b4", "c2c4"},
         "rnbqkbnr/p1pppppp/8/8/PpP4P/8/1P1PPPP1/RNBQKBNR b KQkq c3 0 3",
         0x3c8123ea7b067637ULL},
        // An en passant capture made, and a rook's move: one castling right goes.
        {{"a2a4", "b7b5", "h2h4", "b5b4", "c2c4", "b4c3", "a1a3"},
         "rnbqkbnr/p1pppppp/8/8/P6P/R1p5/1P1PPPP1/1NBQKBNR b Kkq - 0 4",
         0x5c3f9b829b279560ULL},
    };

    for (const auto &entry : cases)
    {
        const Position played = After(entry.moves);
        EXPECT_EQ(PolyglotKey(played), entry.key) << entry.fen;
        const auto read = Position::FromFen(entry.fen);
        ASSERT_TRUE(read) << entry.fen;
        EXPECT_EQ(PolyglotKey(*read), entry.key) << entry.fen;
    }
}

TEST(Polyglot, AnEnPassantFileCountsWithAPawnBesideEvenWhenTheCaptureIsIllegal)
{
    // White's e5 pawn is pinned to its king by the rook on a5... along the fifth rank, so exd6 is illegal, but the
    // format hashes the file all the same.
    const auto pinned = *Position::FromFen("8/8/8/r2pP2K/8/8/8/k7 w - d6 0 1");
    EXPECT_FALSE(pinned.HasLegalEnPassantCapture());
    const auto no_square = *Position::FromFen("8/8/8/r2pP2K/8/8/8/k7 w - - 0 1");
    EXPECT_EQ(PolyglotKey(pinned) ^ PolyglotKey(no_square), kPolyglotRandom64[772 + 3]);
    // With no pawn beside it, the square does not count.
    const auto alone = *Position::FromFen("4k3/8/8/3p4/8/8/8/4K3 w - d6 0 1");
    const auto plain = *Position::FromFen("4k3/8/8/3p4/8/8/8/4K3 w - - 0 1");
    EXPECT_EQ(PolyglotKey(alone), PolyglotKey(plain));
}

TEST(Polyglot, PacksMovesAsTheFormatDoes)
{
    const Position start = Position::Start();
    EXPECT_EQ(PolyglotBook::EncodeMove(*ParseUciMove(start, "e2e4")), (4 << 6) | (1 << 9) | 4 | (3 << 3));
    const Position castling = *Position::FromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    EXPECT_EQ(PolyglotBook::EncodeMove(*ParseUciMove(castling, "e1g1")), (4 << 6) | 7) << "e1h1";
    EXPECT_EQ(PolyglotBook::EncodeMove(*ParseUciMove(castling, "e1c1")), (4 << 6) | 0) << "e1a1";
    const Position black = *Position::FromFen("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1");
    EXPECT_EQ(PolyglotBook::EncodeMove(*ParseUciMove(black, "e8g8")), (4 << 6) | (7 << 9) | 7 | (7 << 3));
    const Position promotion = *Position::FromFen("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1");
    EXPECT_EQ(PolyglotBook::EncodeMove(*ParseUciMove(promotion, "b7b8q")) >> 12, 4);
    EXPECT_EQ(PolyglotBook::EncodeMove(*ParseUciMove(promotion, "b7b8n")) >> 12, 1);
    EXPECT_EQ(PolyglotBook::EncodeMove(*ParseUciMove(promotion, "b7b8b")) >> 12, 2);
    EXPECT_EQ(PolyglotBook::EncodeMove(*ParseUciMove(promotion, "b7b8r")) >> 12, 3);
}

std::vector<PolyglotEntry> FixtureEntries()
{
    const Position start  = Position::Start();
    const Position e4     = After({"e2e4"});
    const Position castle = *Position::FromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    const auto     entry  = [](const Position &position, const char *uci, std::uint16_t weight) {
        return PolyglotEntry{PolyglotKey(position), PolyglotBook::EncodeMove(*ParseUciMove(position, uci)), weight, 0};
    };
    std::vector<PolyglotEntry> entries{
        entry(e4, "e7e5", 7),    entry(start, "d2d4", 10), entry(start, "e2e4", 20),
        entry(start, "g1f3", 0), entry(castle, "e1g1", 3),
    };
    // An entry whose move is not legal in its position: a corrupt book's, never offered.
    entries.push_back({PolyglotKey(start), PolyglotBook::EncodeMove(*ParseUciMove(e4, "e7e5")), 50, 0});
    return entries;
}

TEST(Polyglot, ABookRoundTripsThroughItsBytes)
{
    const auto bytes = PolyglotBook::Encode(FixtureEntries());
    ASSERT_EQ(bytes.size(), 6u * 16);
    const auto book = PolyglotBook::FromBytes(bytes);
    ASSERT_TRUE(book) << book.ErrorMessage();
    EXPECT_EQ(book->Size(), 6u);
    // The bytes written from the book's own entries are the bytes read.
    std::vector<PolyglotEntry> reread;
    for (const auto &key : {PolyglotKey(Position::Start()), PolyglotKey(After({"e2e4"})),
                            PolyglotKey(*Position::FromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"))})
    {
        const auto entries = book->Entries(key);
        reread.insert(reread.end(), entries.begin(), entries.end());
    }
    EXPECT_EQ(PolyglotBook::Encode(reread), bytes);

    const Position start = Position::Start();
    EXPECT_EQ(book->Entries(PolyglotKey(start)).size(), 4u);
    const auto moves = book->Moves(start);
    ASSERT_EQ(moves.size(), 3u) << "the illegal entry is left out";
    std::mt19937_64 generator(1);
    EXPECT_EQ(book->Choose(start, BookSelection::kBestWeight, generator)->ToUci(), "e2e4");

    // Weighted at random: e2e4 two thirds of the time, d2d4 a third, g1f3 (weight 0) never.
    std::map<std::string, int> counts;
    for (int draw = 0; draw < 3000; ++draw)
    {
        ++counts[book->Choose(start, BookSelection::kWeightedRandom, generator)->ToUci()];
    }
    EXPECT_EQ(counts.count("g1f3"), 0u);
    EXPECT_NEAR(counts["e2e4"] / 3000.0, 2.0 / 3, 0.04);
    EXPECT_NEAR(counts["d2d4"] / 3000.0, 1.0 / 3, 0.04);
    // One seed, one sequence.
    std::mt19937_64 first(42), second(42);
    for (int draw = 0; draw < 20; ++draw)
    {
        EXPECT_EQ(book->Choose(start, BookSelection::kWeightedRandom, first),
                  book->Choose(start, BookSelection::kWeightedRandom, second));
    }

    // Castling, stored as the king taking its rook, comes back as the host's castling move.
    const Position castle   = *Position::FromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    const auto     castling = book->Choose(castle, BookSelection::kBestWeight, generator);
    ASSERT_TRUE(castling);
    EXPECT_EQ(castling->MoveType(), Move::kCastling);
    EXPECT_EQ(castling->ToUci(), "e1g1");
    // A position the book does not hold.
    EXPECT_FALSE(book->Choose(After({"d2d4"}), BookSelection::kWeightedRandom, generator));
}

TEST(Polyglot, LoadsFromAFileAndRefusesWhatIsNotABook)
{
    const auto path  = std::filesystem::temp_directory_path() / "amphitheater_polyglot_test.bin";
    const auto bytes = PolyglotBook::Encode(FixtureEntries());
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    const std::u8string text = path.u8string();
    const auto          book = PolyglotBook::Load(std::string(text.begin(), text.end()));
    ASSERT_TRUE(book) << book.ErrorMessage();
    EXPECT_EQ(book->Size(), 6u);
    std::filesystem::remove(path);

    EXPECT_FALSE(PolyglotBook::Load("/no/such/book.bin"));
    std::vector<std::uint8_t> ragged(bytes.begin(), bytes.end() - 3);
    EXPECT_FALSE(PolyglotBook::FromBytes(ragged));
    // Two entries out of key order: key 2, then key 1.
    std::vector<std::uint8_t> unsorted(32, 0);
    unsorted[7]        = 2;
    unsorted[23]       = 1;
    const auto refused = PolyglotBook::FromBytes(unsorted);
    ASSERT_FALSE(refused);
    EXPECT_NE(refused.ErrorMessage().find("out of order"), std::string::npos);
    EXPECT_TRUE(PolyglotBook::FromBytes({}));
}

} // namespace
} // namespace amphitheater::chess
