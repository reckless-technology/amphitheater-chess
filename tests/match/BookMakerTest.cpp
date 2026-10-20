// A book made from a library of games (BookMaker.h), and what it reads them with: the DEFLATE decoder held to zlib's
// streams of every block type, the zip reader held to Python's zipfile's archive, the files a library holds, the moves
// of each side a book takes, the games a move must be played in, progress and its stop, and what it refuses. The
// library is tests/data/library, written by make_library.py beside it.

#include "match/BookMaker.h"
#include "match/Inflate.h"
#include "match/Openings.h"
#include "match/ZipArchive.h"

#include "chess/Notation.h"
#include "chess/PgnBook.h"
#include "chess/PolyglotBook.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>

namespace amphitheater::match
{
namespace
{

/** @return The test library's folder. */
std::filesystem::path Library()
{
    return std::filesystem::path(AMPHITHEATER_TEST_DATA_DIR) / "library";
}

/**
 * @param path A file.
 * @return Its bytes.
 */
std::string Bytes(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/**
 * A book's moves in a position, by SAN.
 * @param book The book.
 * @param sans The moves from the start to the position.
 * @return Each move's weight.
 */
std::map<std::string, int> MovesAfter(const chess::PolyglotBook &book, std::initializer_list<const char *> sans)
{
    chess::Position position = chess::Position::Start();
    for (const char *san : sans)
    {
        position = position.MakeMove(*chess::ParseSan(position, san));
    }
    std::map<std::string, int> moves;
    for (const auto &[move, weight] : book.Moves(position))
    {
        moves[chess::MoveToSan(position, move)] = weight;
    }
    return moves;
}

/**
 * @param name The file's name.
 * @return A .bin under the system's temporary folder to write a book to, gone before the test.
 */
std::string ScratchBook(const std::string &name)
{
    const auto file = std::filesystem::temp_directory_path() / (name + ".bin");
    std::filesystem::remove(file);
    return file.string();
}

} // namespace

TEST(Inflate, DecodesZlibsStoredFixedAndDynamicBlocksAlike)
{
    const std::string text = Bytes(Library() / "deflate" / "text.txt");
    ASSERT_GT(text.size(), 65535u) << "more than a stored block holds";
    for (const char *name : {"stored", "fixed", "dynamic"})
    {
        const std::string stream = Bytes(Library() / "deflate" / (std::string(name) + ".deflate"));
        const auto        bytes  = Inflate({reinterpret_cast<const std::uint8_t *>(stream.data()), stream.size()});
        ASSERT_TRUE(bytes) << name << ": " << bytes.ErrorMessage();
        EXPECT_EQ(std::string(bytes->begin(), bytes->end()), text) << name;
    }
}

TEST(Inflate, RefusesAStreamCutShortCorruptOrLargerThanAllowed)
{
    const std::string stream = Bytes(Library() / "deflate" / "dynamic.deflate");
    const auto       *data   = reinterpret_cast<const std::uint8_t *>(stream.data());
    EXPECT_FALSE(Inflate({data, stream.size() / 2})) << "cut short";
    EXPECT_FALSE(Inflate({data, stream.size()}, 0, 1000)) << "more than allowed";
    const std::uint8_t reserved[] = {0x07};
    EXPECT_EQ(Inflate(reserved).ErrorMessage(), "a block of the reserved type");
    const std::uint8_t bad_stored[] = {0x01, 0x05, 0x00, 0x00, 0x00};
    EXPECT_FALSE(Inflate(bad_stored)) << "a length that does not match its complement";
    EXPECT_FALSE(Inflate({})) << "nothing";
}

TEST(ZipArchive, ListsAnArchivesFilesAndReadsStoredAndDeflatedOnesAlike)
{
    const auto archive = ZipArchive::Open(Library() / "folder" / "more.zip");
    ASSERT_TRUE(archive) << archive.ErrorMessage();
    std::vector<std::string> names;
    for (const ZipEntry &entry : archive->entries())
    {
        names.push_back(entry.name);
    }
    EXPECT_EQ(names, (std::vector<std::string>{"three.pgn", "sub/four.pgn", "readme.txt"})) << "its folder left out";
    const auto three = archive->Read(archive->entries()[0]);
    ASSERT_TRUE(three) << three.ErrorMessage();
    EXPECT_NE(three->find("1. e4 e5 2. Nf3 Nf6"), std::string::npos);
    EXPECT_EQ(archive->entries()[1].method, 0);
    const auto four = archive->Read(archive->entries()[1]);
    ASSERT_TRUE(four) << four.ErrorMessage();
    EXPECT_NE(four->find("1. c4 e5"), std::string::npos);

    EXPECT_FALSE(ZipArchive::Open(Library() / "folder" / "one.pgn")) << "not an archive";
    EXPECT_FALSE(ZipArchive::Open(Library() / "missing.zip"));
}

TEST(BookMaker, AFolderHoldsItsPgnFilesAndArchivesAndThoseOfTheFoldersUnderIt)
{
    const auto sources = FindGameSources((Library() / "folder").string());
    ASSERT_TRUE(sources) << sources.ErrorMessage();
    std::vector<std::string> names;
    for (const GameSource &source : *sources)
    {
        names.push_back(source.entry ? *source.entry : source.file.filename().string());
    }
    EXPECT_EQ(names, (std::vector<std::string>{"two.PGN", "three.pgn", "sub/four.pgn", "one.pgn"}));

    const auto single = FindGameSources((Library() / "folder" / "one.pgn").string());
    ASSERT_TRUE(single);
    EXPECT_EQ(single->size(), 1u);
    const auto zipped = FindGameSources((Library() / "folder" / "more.zip").string());
    ASSERT_TRUE(zipped);
    EXPECT_EQ(zipped->size(), 2u);
    EXPECT_FALSE(FindGameSources((Library() / "deflate").string())) << "no PGN file";
    EXPECT_FALSE(FindGameSources((Library() / "missing").string()));
}

TEST(BookMaker, TakesEachGamesFirstMovesOfEachSideWeightedByTheGamesThatPlayThem)
{
    BookMakerSettings settings;
    settings.games          = (Library() / "folder").string();
    settings.moves_per_side = 1;
    settings.out            = ScratchBook("amphitheater-makebook-one-move");
    std::vector<BookMakerProgress> told;
    const auto                     made = MakeBook(settings, [&](const BookMakerProgress &progress) {
        told.push_back(progress);
        return true;
    });
    ASSERT_TRUE(made) << made.ErrorMessage();
    EXPECT_EQ(made->sources, 4u);
    EXPECT_EQ(made->games, 5);
    EXPECT_TRUE(made->problems.empty());
    ASSERT_FALSE(told.empty());
    EXPECT_EQ(told.back().sources_total, 4u);
    EXPECT_EQ(told.back().games, 5);

    const auto book = chess::PolyglotBook::Load(settings.out);
    ASSERT_TRUE(book) << book.ErrorMessage();
    EXPECT_EQ(MovesAfter(*book, {}), (std::map<std::string, int>{{"e4", 3}, {"d4", 1}, {"c4", 1}}));
    EXPECT_EQ(MovesAfter(*book, {"e4"}), (std::map<std::string, int>{{"e5", 2}, {"c5", 1}}));
    EXPECT_TRUE(MovesAfter(*book, {"e4", "e5"}).empty()) << "one move of each side";
    EXPECT_EQ(made->positions, 4u);

    settings.moves_per_side = 2;
    ASSERT_TRUE(MakeBook(settings));
    const auto deeper = chess::PolyglotBook::Load(settings.out);
    ASSERT_TRUE(deeper);
    EXPECT_EQ(MovesAfter(*deeper, {"e4", "e5"}), (std::map<std::string, int>{{"Nf3", 2}}));
    EXPECT_EQ(MovesAfter(*deeper, {"e4", "e5", "Nf3"}), (std::map<std::string, int>{{"Nc6", 1}, {"Nf6", 1}}));
}

TEST(BookMaker, LeavesOutMovesPlayedInFewerGamesThanAsked)
{
    BookMakerSettings settings;
    settings.games       = (Library() / "folder").string();
    settings.least_games = 2;
    settings.out         = ScratchBook("amphitheater-makebook-least");
    ASSERT_TRUE(MakeBook(settings));
    const auto book = chess::PolyglotBook::Load(settings.out);
    ASSERT_TRUE(book);
    EXPECT_EQ(MovesAfter(*book, {}), (std::map<std::string, int>{{"e4", 3}}));
    EXPECT_EQ(MovesAfter(*book, {"e4", "e5"}), (std::map<std::string, int>{{"Nf3", 2}}));

    settings.least_games = 10;
    EXPECT_FALSE(MakeBook(settings)) << "no move is played that often";
}

TEST(BookMaker, StopsWhenProgressSaysSoAndWritesNothing)
{
    BookMakerSettings settings;
    settings.games  = (Library() / "folder").string();
    settings.out    = ScratchBook("amphitheater-makebook-stopped");
    const auto made = MakeBook(settings, [](const BookMakerProgress &progress) { return progress.sources_done < 2; });
    ASSERT_FALSE(made);
    EXPECT_EQ(made.ErrorMessage(), "stopped");
    EXPECT_FALSE(std::filesystem::exists(settings.out));
}

TEST(BookMaker, RefusesSettingsItCannotUse)
{
    BookMakerSettings settings;
    settings.games = (Library() / "folder").string();
    EXPECT_FALSE(MakeBook(settings)) << "no file to write";
    settings.out            = ScratchBook("amphitheater-makebook-refused");
    settings.moves_per_side = 0;
    EXPECT_FALSE(MakeBook(settings));
    settings.moves_per_side = 7;
    settings.games          = (Library() / "deflate").string();
    EXPECT_FALSE(MakeBook(settings)) << "no PGN file";
}

TEST(PgnBookBuilder, ScalesWeightsPastTheFormatsMostAlike)
{
    // 66,000 games of 1.e4, past the format's 65,535, and 22,000 of 1.d4.
    std::string text;
    for (int game = 0; game < 88000; ++game)
    {
        text += game % 4 == 3 ? "1. d4 *\n" : "1. e4 *\n";
    }
    chess::PgnBookBuilder builder(1);
    builder.Add(text);
    EXPECT_EQ(builder.games(), 88000);
    const chess::Position      start = chess::Position::Start();
    std::map<std::string, int> weights;
    for (const auto &entry : builder.Entries())
    {
        const std::uint16_t e4                  = chess::PolyglotBook::EncodeMove(*chess::ParseSan(start, "e4"));
        weights[entry.move == e4 ? "e4" : "d4"] = entry.weight;
    }
    EXPECT_EQ(weights["e4"], 65535) << "the most played at the format's most";
    EXPECT_EQ(weights["d4"], 22000 * 65535 / 66000) << "the rest in proportion";
}

} // namespace amphitheater::match
