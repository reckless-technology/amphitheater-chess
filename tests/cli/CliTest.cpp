// The command line in-process (RunCli): perft, matches and tournaments between fake engines, analysis, and every
// argument it refuses.

#include "cli/Cli.h"
#include "chess/Notation.h"
#include "chess/PolyglotBook.h"
#include "match/Openings.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace amphitheater::cli
{
namespace
{

struct Run
{
    int         code = 0;
    std::string out;
    std::string err;
};

Run RunWith(std::vector<std::string> arguments, const std::atomic<bool> *stop = nullptr)
{
    arguments.insert(arguments.begin(), "amphitheater-cli");
    std::ostringstream out;
    std::ostringstream err;
    Run                run;
    run.code = RunCli(arguments, out, err, stop);
    run.out  = out.str();
    run.err  = err.str();
    return run;
}

const std::string kFake = std::string("cmd=") + AMPHITHEATER_FAKE_ENGINE;

std::filesystem::path TempFile(const char *name)
{
    return std::filesystem::temp_directory_path() / name;
}

TEST(Cli, HelpVersionAndUnknownCommands)
{
    EXPECT_EQ(RunWith({}).code, 2);
    auto run = RunWith({"--help"});
    EXPECT_EQ(run.code, 0);
    EXPECT_NE(run.out.find("amphitheater-cli match"), std::string::npos);
    run = RunWith({"--version"});
    EXPECT_EQ(run.code, 0);
    EXPECT_NE(run.out.find("Reckless Amphitheater "), std::string::npos);
    EXPECT_EQ(run.out.find("Reckless Amphitheater " + VersionText()), 0u);
    run = RunWith({"juggle"});
    EXPECT_EQ(run.code, 2);
    EXPECT_NE(run.err.find("unknown command 'juggle'"), std::string::npos);
}

TEST(Cli, Perft)
{
    auto run = RunWith({"perft", "startpos", "3"});
    EXPECT_EQ(run.code, 0) << run.err;
    EXPECT_NE(run.out.find("Nodes: 8902"), std::string::npos) << run.out;
    run =
        RunWith({"perft", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R", "w", "KQkq", "-", "2", "-divide"});
    EXPECT_EQ(run.code, 0) << run.err;
    EXPECT_NE(run.out.find("e1g1: "), std::string::npos);
    EXPECT_NE(run.out.find("Nodes: 2039"), std::string::npos) << run.out;
    EXPECT_EQ(RunWith({"perft", "startpos"}).code, 2);
    EXPECT_EQ(RunWith({"perft", "startpos", "x"}).code, 2);
    EXPECT_EQ(RunWith({"perft", "8/8/8/8", "2"}).code, 2);
}

TEST(Cli, AMatchBetweenFakeEnginesWritesItsGames)
{
    const auto pgn = TempFile("amphitheater_cli_match.pgn");
    std::filesystem::remove(pgn);
    const auto epd = TempFile("amphitheater_cli_openings.epd");
    {
        std::ofstream file(epd);
        file << "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 id \"e4\";\n"
                "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq d3 id \"d4\";\n";
    }
    const auto run = RunWith({"match",
                              "-engine",
                              kFake,
                              "name=Alpha",
                              "-engine",
                              kFake,
                              "name=Beta",
                              "arg=--seed",
                              "arg=4",
                              "-each",
                              "tc=10+0.1",
                              "option.Hash=32",
                              "-games",
                              "2",
                              "-rounds",
                              "2",
                              "-repeat",
                              "-concurrency",
                              "2",
                              "-openings",
                              "file=" + epd.string(),
                              "order=random",
                              "-srand",
                              "3",
                              "-maxmoves",
                              "30",
                              "-resign",
                              "movecount=3",
                              "score=900",
                              "twosided=true",
                              "-draw",
                              "movenumber=20",
                              "movecount=6",
                              "score=5",
                              "-margin",
                              "50",
                              "-stall",
                              "5",
                              "-event",
                              "CLI Match",
                              "-site",
                              "Here",
                              "-pgnout",
                              pgn.string(),
                              "-recover"});
    EXPECT_EQ(run.code, 0) << run.err;
    EXPECT_NE(run.out.find("Started game 1 of 4"), std::string::npos) << run.out;
    EXPECT_NE(run.out.find("Score of Alpha vs Beta"), std::string::npos);
    EXPECT_NE(run.out.find("Finished match"), std::string::npos);
    const auto text = match::ReadTextFile(pgn.string());
    ASSERT_TRUE(text);
    const auto games = chess::ReadPgn(*text);
    ASSERT_EQ(games.size(), 4u);
    EXPECT_EQ(games[0].Tag("Event"), "CLI Match");
    EXPECT_EQ(games[0].Tag("Site"), "Here");
    std::filesystem::remove(pgn);
    std::filesystem::remove(epd);
}

TEST(Cli, ATournamentPrintsStandingsAndACrosstable)
{
    const auto run = RunWith({"tournament", "-engine", kFake,       "name=One", "-engine",      kFake,     "name=Two",
                              "arg=--seed", "arg=2",   "-engine",   kFake,      "name=Three",   "depth=1", "-st",
                              "0.5",        "-double", "-maxmoves", "10",       "-concurrency", "3"});
    EXPECT_EQ(run.code, 0) << run.err;
    EXPECT_NE(run.out.find("Started game 6 of 6"), std::string::npos) << run.out;
    EXPECT_NE(run.out.find("Rank  Name"), std::string::npos) << run.out;
    EXPECT_NE(run.out.find("Finished tournament"), std::string::npos);
}

TEST(Cli, DebugPrintsTheEnginesLines)
{
    const auto run = RunWith(
        {"match", "-engine", kFake, "-engine", kFake, "-depth", "1", "-nodes", "100", "-maxmoves", "1", "-debug"});
    EXPECT_EQ(run.code, 0) << run.err;
    EXPECT_NE(run.err.find(" < uci"), std::string::npos);
    EXPECT_NE(run.err.find(" > uciok"), std::string::npos);
}

TEST(Cli, AStopRequestEndsAMatchEarly)
{
    std::atomic<bool> stop = true;
    const auto        run  = RunWith(
        {"match", "-engine", kFake, "arg=--think", "arg=200", "-engine", kFake, "-tc", "60", "-games", "20"}, &stop);
    EXPECT_EQ(run.code, 0) << run.err;
    EXPECT_NE(run.err.find("Stopping"), std::string::npos);
}

TEST(Cli, AnEngineThatCannotStartIsReported)
{
    const auto run = RunWith({"match", "-engine", "cmd=no-such-engine-here", "-engine", kFake, "-st", "0.1"});
    EXPECT_EQ(run.code, 0) << run.err;
    EXPECT_NE(run.out.find("failed to start"), std::string::npos) << run.out;
}

TEST(Cli, RefusesBadMatchArguments)
{
    const struct
    {
        std::vector<std::string> arguments;
        const char              *reason;
    } cases[] = {
        {{"-engine", kFake, "-tc", "10"}, "exactly two engines"},
        {{"-engine", kFake, "-engine", kFake}, "no time control"},
        {{"-engine", kFake, "-engine", kFake, "-tc", "ten"}, "not a time control"},
        {{"-engine", kFake, "-engine", kFake, "-tc"}, "needs a value"},
        {{"-engine", kFake, "-engine", kFake, "-st", "x"}, "time in seconds"},
        {{"-engine", kFake, "-engine", kFake, "-st"}, "needs a value"},
        {{"-engine", kFake, "-engine", kFake, "-st", "1", "-games", "0"}, "at least 1"},
        {{"-engine", kFake, "-engine", kFake, "-st", "1", "-games"}, "needs a value"},
        {{"-engine", kFake, "-engine", kFake, "-st", "1", "-margin", "-5"}, "at least 0"},
        {{"-engine", kFake, "-engine", kFake, "-st", "1", "-margin"}, "needs a value"},
        {{"-engine", kFake, "-engine", kFake, "-st", "1", "-bogus"}, "unknown option"},
        {{"-engine", kFake, "-engine", kFake, "-st", "1", "stray"}, "unexpected"},
        {{"-engine", kFake, "colour=red", "-engine", kFake, "-st", "1"}, "unknown engine key"},
        {{"-engine", "name=x", "-engine", kFake, "-st", "1"}, "cmd="},
        {{"-engine", kFake, "proto=xboard", "-engine", kFake, "-st", "1"}, "only UCI"},
        {{"-engine", kFake, "tc=x", "-engine", kFake, "-st", "1"}, "not a time control"},
        {{"-engine", kFake, "st=x", "-engine", kFake, "-st", "1"}, "time in seconds"},
        {{"-engine", kFake, "depth=0", "-engine", kFake, "-st", "1"}, "at least 1"},
        {{"-engine", kFake, "nodes=x", "-engine", kFake, "-st", "1"}, "whole number"},
        {{"-engine", "novalue", "-engine", kFake, "-st", "1"}, "key=value"},
        {{"-engine", kFake, "-engine", kFake, "-st", "1", "-resign", "movecount=x"}, "whole number"},
        {{"-engine", kFake, "-engine", kFake, "-st", "1", "-resign", "movenumber=3"}, "no key"},
        {{"-engine", kFake, "-engine", kFake, "-st", "1", "-draw", "score=x"}, "whole number"},
        {{"-engine", kFake, "-engine", kFake, "-st", "1", "-openings", "format=epd"}, "file="},
        {{"-engine", kFake, "-engine", kFake, "-st", "1", "-openings", "file=/no/such.epd"}, "cannot open"},
        {{"-engine", kFake, "-engine", kFake, "-st", "1", "-openings", "file=x", "order=sideways"}, "has no"},
        {{"-engine", kFake, "-engine", kFake, "-st", "1", "-openings", "file=x", "plies=x"}, "whole number"},
        {{"-engine", kFake, "-engine", kFake, "-st", "1", "-pgnout"}, "needs a value"},
    };

    for (const auto &entry : cases)
    {
        std::vector<std::string> arguments{"match"};
        arguments.insert(arguments.end(), entry.arguments.begin(), entry.arguments.end());
        const auto run = RunWith(arguments);
        EXPECT_EQ(run.code, 2) << entry.reason;
        EXPECT_NE(run.err.find(entry.reason), std::string::npos) << "wanted '" << entry.reason << "', got " << run.err;
    }
    const auto tournament = RunWith({"tournament", "-engine", kFake, "-st", "1"});
    EXPECT_EQ(tournament.code, 2);
    EXPECT_NE(tournament.err.find("at least two engines"), std::string::npos);
}

TEST(Cli, ParsesOpeningsFromPgnByTheFileName)
{
    const auto pgn = TempFile("amphitheater_cli_openings.pgn");
    {
        std::ofstream file(pgn);
        file << "[Event \"x\"]\n\n1. e4 e5 2. Nf3 *\n";
    }
    const auto options = ParseTournamentArguments(
        {"-engine", kFake, "-engine", kFake, "-st", "1", "-openings", "file=" + pgn.string(), "plies=2", "start=1"},
        true);
    ASSERT_TRUE(options) << options.ErrorMessage();
    ASSERT_EQ(options->settings.openings.size(), 1u);
    EXPECT_EQ(options->settings.openings[0].moves.size(), 2u);
    std::filesystem::remove(pgn);
}

TEST(Cli, Analyse)
{
    auto run = RunWith({"analyse", "-engine", kFake, "arg=--name", "arg=Analyst", "-fen", "startpos", "-moves", "e2e4",
                        "-time", "0.3", "-multipv", "2"});
    EXPECT_EQ(run.code, 0) << run.err;
    EXPECT_NE(run.out.find("Engine: Analyst by The Tests"), std::string::npos) << run.out;
    EXPECT_NE(run.out.find("depth 1/2 line 1 score"), std::string::npos) << run.out;
    EXPECT_NE(run.out.find("info string thinking"), std::string::npos);
    EXPECT_NE(run.out.find("bestmove "), std::string::npos);
    run =
        RunWith({"analyze", "-engine", kFake, "-fen", "4k3/8/8/8/8/8/8/R3K3", "w", "-", "-", "0", "1", "-depth", "1"});
    EXPECT_EQ(run.code, 0) << run.err;
    EXPECT_NE(run.out.find("Position: 4k3/8/8/8/8/8/8/R3K3 w - - 0 1"), std::string::npos) << run.out;
}

TEST(Cli, AnalysesScoresAreFromWhitesSide)
{
    // The engine says +0.40 for the side to move: with Black to move (after 1.e4) that is White's -0.40, and a mate
    // in 2 for Black is #-2. The -debug lines stay the engine's own words.
    auto run = RunWith({"analyse", "-engine", kFake, "arg=--score", "arg=40", "-moves", "e2e4", "-depth", "1"});
    EXPECT_EQ(run.code, 0) << run.err;
    EXPECT_NE(run.out.find(" score -0.40 "), std::string::npos) << run.out;
    run = RunWith({"analyse", "-engine", kFake, "arg=--score", "arg=40", "-depth", "1"});
    EXPECT_NE(run.out.find(" score +0.40 "), std::string::npos) << run.out;
    run = RunWith({"analyse", "-engine", kFake, "arg=--mate", "arg=2", "-moves", "e2e4", "-depth", "1"});
    EXPECT_NE(run.out.find(" score #-2 "), std::string::npos) << run.out;
}

TEST(Cli, AnalyseReportsWhatGoesWrong)
{
    EXPECT_EQ(RunWith({"analyse", "-fen", "startpos"}).code, 2);
    EXPECT_EQ(RunWith({"analyse", "-engine", kFake, "-fen", "bad"}).code, 2);
    EXPECT_EQ(RunWith({"analyse", "-engine", kFake, "-moves", "e2e5"}).code, 2);
    EXPECT_EQ(RunWith({"analyse", "-engine", kFake, "-time"}).code, 2);
    EXPECT_EQ(RunWith({"analyse", "-engine", kFake, "-time", "x"}).code, 2);
    EXPECT_EQ(RunWith({"analyse", "-engine", kFake, "-depth", "0"}).code, 2);
    EXPECT_EQ(RunWith({"analyse", "-engine", kFake, "-depth"}).code, 2);
    EXPECT_EQ(RunWith({"analyse", "-engine", "novalue"}).code, 2);
    EXPECT_EQ(RunWith({"analyse", "-engine", "name=x"}).code, 2);
    EXPECT_EQ(RunWith({"analyse", "-engine", kFake, "-what"}).code, 2);
    EXPECT_EQ(RunWith({"analyse", "stray"}).code, 2);
    EXPECT_EQ(RunWith({"analyse", "-engine", "cmd=no-such-engine-here"}).code, 1);
    const auto crashed = RunWith({"analyse", "-engine", kFake, "arg=--crash-at", "arg=1", "-time", "0.2"});
    EXPECT_EQ(crashed.code, 1);
    EXPECT_NE(crashed.err.find("exited"), std::string::npos);
    const auto hung = RunWith({"analyse", "-engine", kFake, "arg=--hang-at", "arg=1", "-time", "0.1"});
    EXPECT_EQ(hung.code, 1);
    EXPECT_NE(hung.err.find("did not answer"), std::string::npos);
}

// A Polyglot book of one line, 1. e4 c5 2. Nf3, written to a temporary file.
std::string WriteLineBook(const char *name)
{
    std::vector<chess::PolyglotEntry> entries;
    chess::Position                   position = chess::Position::Start();
    for (const char *text : {"e2e4", "c7c5", "g1f3"})
    {
        const auto move = *chess::ParseUciMove(position, text);
        entries.push_back({chess::PolyglotKey(position), chess::PolyglotBook::EncodeMove(move), 1, 0});
        position = position.MakeMove(move);
    }
    const auto    bytes = chess::PolyglotBook::Encode(entries);
    const auto    path  = TempFile(name);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path.string();
}

TEST(Cli, ReadsBooksAndPonderingForEachEngine)
{
    const auto book = WriteLineBook("amphitheater_cli_book.bin");
    const auto options =
        ParseTournamentArguments({"-engine", kFake, "book=" + book, "bookdepth=4", "ponder", "-engine", kFake,
                                  "ponder=false", "-each", "book=" + book, "-st", "1", "-srand", "9"},
                                 true);
    ASSERT_TRUE(options) << options.ErrorMessage();
    const auto &engines = options->settings.engines;
    ASSERT_EQ(engines.size(), 2u);
    EXPECT_TRUE(engines[0].config.does_ponder);
    EXPECT_FALSE(engines[1].config.does_ponder);
    ASSERT_TRUE(engines[0].book);
    EXPECT_EQ(engines[0].book, engines[1].book) << "one copy of a book both name";
    EXPECT_EQ(engines[0].book_depth, 4);
    EXPECT_EQ(engines[1].book_depth, 0);
    EXPECT_EQ(options->settings.seed, 9u);
    std::filesystem::remove(book);
}

TEST(Cli, ReadsAPolyglotBookAsAnOpeningSource)
{
    const auto book = WriteLineBook("amphitheater_cli_openings.bin");
    auto       options =
        ParseTournamentArguments({"-engine", kFake, "-engine", kFake, "-st", "1", "-openings", "file=" + book}, true);
    ASSERT_TRUE(options) << options.ErrorMessage();
    EXPECT_TRUE(options->settings.opening_book) << "a .bin file is a Polyglot book";
    EXPECT_EQ(options->settings.opening_book_plies, 8);
    EXPECT_TRUE(options->settings.openings.empty());
    options = ParseTournamentArguments(
        {"-engine", kFake, "-engine", kFake, "-st", "1", "-openings", "file=" + book, "format=polyglot", "plies=2"},
        true);
    ASSERT_TRUE(options);
    EXPECT_EQ(options->settings.opening_book_plies, 2);

    const auto run =
        RunWith({"match", "-engine", kFake, "arg=--ponder-move", "ponder", "-engine", kFake, "-each", "tc=10", "-games",
                 "2", "-repeat", "-maxmoves", "4", "-openings", "file=" + book, "format=polyglot", "plies=2"});
    EXPECT_EQ(run.code, 0) << run.err;
    EXPECT_NE(run.out.find("Finished match"), std::string::npos);
    std::filesystem::remove(book);
}

TEST(Cli, RefusesBadBookAndPonderKeys)
{
    const struct
    {
        std::vector<std::string> arguments;
        const char              *reason;
    } cases[] = {
        {{"-engine", kFake, "bookdepth=0", "-engine", kFake, "-st", "1"}, "at least 1"},
        {{"-engine", kFake, "book=/no/such/book.bin", "-engine", kFake, "-st", "1"}, "cannot open"},
        {{"-engine", kFake, "ponder=maybe", "-engine", kFake, "-st", "1"}, "ponder is a flag"},
        {{"-engine", kFake, "-engine", kFake, "-st", "1", "-openings", "file=/no/such/book.bin", "format=polyglot"},
         "cannot open"},
    };

    for (const auto &entry : cases)
    {
        std::vector<std::string> arguments{"match"};
        arguments.insert(arguments.end(), entry.arguments.begin(), entry.arguments.end());
        const auto run = RunWith(arguments);
        EXPECT_EQ(run.code, 2) << entry.reason;
        EXPECT_NE(run.err.find(entry.reason), std::string::npos) << "wanted '" << entry.reason << "', got " << run.err;
    }
    EXPECT_NE(RunWith({"--help"}).out.find("bookdepth=<n>"), std::string::npos);
}

TEST(Cli, AnEpdSuiteIsRunAgainstAnEngineAndScored)
{
    // The fake engine takes the most valuable piece it can: here White's queen takes Black's.
    const auto suite = TempFile("amphitheater_cli_suite.epd");
    {
        std::ofstream file(suite);
        file << "3q3k/8/8/8/8/8/8/3Q3K w - - bm Qxd8+; id \"trade\";\n"
                "3q3k/8/8/8/8/8/8/3Q3K w - - am Qxd8+; id \"no trade\";\n"
                "3q3k/8/8/8/8/8/8/3Q3K w - - bm Qd9;\n";
    }
    const auto run = RunWith({"epd", "-engine", kFake, "-file", suite.string(), "-time", "0.05"});
    EXPECT_EQ(run.code, 0) << run.err;
    EXPECT_NE(run.out.find("Suite: "), std::string::npos) << run.out;
    EXPECT_NE(run.out.find("trade            solved  Qxd8+     bm Qxd8+"), std::string::npos) << run.out;
    EXPECT_NE(run.out.find("no trade         failed  Qxd8+     am Qxd8+"), std::string::npos) << run.out;
    EXPECT_NE(run.out.find("Solved 1 of 2 (50.0 %)"), std::string::npos) << run.out;
    EXPECT_NE(run.err.find("Warning: skipped line 3"), std::string::npos) << run.err;

    EXPECT_EQ(RunWith({"epd", "-file", suite.string()}).code, 2) << "no engine";
    EXPECT_EQ(RunWith({"epd", "-engine", kFake}).code, 2) << "no file";
    EXPECT_EQ(RunWith({"epd", "-engine", kFake, "-file", suite.string(), "-depth", "0"}).code, 2);
    EXPECT_EQ(RunWith({"epd", "-engine", kFake, "-file", "no-such-suite.epd"}).code, 1);
    EXPECT_NE(RunWith({"--help"}).out.find("amphitheater-cli epd"), std::string::npos);
}

TEST(Cli, MakeBookWritesABookOfALibrarysOpeningsAndSaysWhatItMade)
{
    const std::string library = (std::filesystem::path(AMPHITHEATER_TEST_DATA_DIR) / "library" / "folder").string();
    const auto        book    = TempFile("amphitheater_cli_made.bin");
    std::filesystem::remove(book);
    const auto run = RunWith({"makebook", "-games", library, "-out", book.string(), "-moves", "1"});
    EXPECT_EQ(run.code, 0) << run.err;
    EXPECT_NE(run.out.find("[1/4] "), std::string::npos) << run.out;
    EXPECT_NE(run.out.find(": 7 moves in 4 positions from 5 games in 4 files"), std::string::npos) << run.out;
    const auto read = chess::PolyglotBook::Load(book.string());
    ASSERT_TRUE(read) << read.ErrorMessage();
    EXPECT_EQ(read->Moves(chess::Position::Start()).size(), 3u);

    const auto least = ParseMakeBookArguments({"-games", library, "-out", "x.bin", "-least", "3"});
    ASSERT_TRUE(least);
    EXPECT_EQ(least->least_games, 3);
    EXPECT_EQ(least->moves_per_side, 7) << "seven moves of each side by default";
    EXPECT_EQ(RunWith({"makebook", "-out", book.string()}).code, 2) << "no games";
    EXPECT_EQ(RunWith({"makebook", "-games", library}).code, 2) << "no file to write";
    EXPECT_EQ(RunWith({"makebook", "-games", library, "-out", book.string(), "-moves", "0"}).code, 2);
    EXPECT_EQ(RunWith({"makebook", "-games", "no-such-folder", "-out", book.string()}).code, 1);
    EXPECT_NE(RunWith({"--help"}).out.find("amphitheater-cli makebook"), std::string::npos);
}

TEST(Cli, ScreenBookMarksTheMovesTheEngineRatesFarBelowItsOwnChoice)
{
    // Two book moves, so two positions and six searches: per position the engine's own choice, that choice re-scored
    // restricted, then the book move itself. The first position's book move comes back 500 below its reference and
    // must be marked; the second's comes back level and must not be. Scripting the scores is what makes the verdict
    // knowable rather than merely plausible - the fake engine answers every `go` with the next number.
    const auto    book = TempFile("amphitheater_cli_screen.book");
    const auto    out  = TempFile("amphitheater_cli_screen_out.book");
    std::ofstream written{book, std::ios::binary};
    written << "# a book\ne2e4 e7e5\n";
    written.close();
    std::filesystem::remove(out);

    const std::vector<std::string> engine{"-engine", kFake, "arg=--scores", "arg=20,20,-480,30,30,30"};
    std::vector<std::string>       arguments{"screenbook"};
    arguments.insert(arguments.end(), engine.begin(), engine.end());
    arguments.insert(arguments.end(), {"-book", book.string(), "-out", out.string(), "-depth", "4", "-margin", "100"});

    const auto run = RunWith(arguments);
    EXPECT_EQ(run.code, 0) << run.err;
    EXPECT_NE(run.out.find("1 move marked in 1 opening"), std::string::npos) << run.out;
    EXPECT_NE(run.out.find("6 searches"), std::string::npos) << run.out;

    // The mark lands on the move the scores condemned, and on nothing else. Reading the written file rather than the
    // summary is the point: the summary is what the screener believed, the file is what the engines will load.
    //
    // The third line is the refill guarantee showing itself. `e2e4` was the only move the start position had, so
    // marking it left the engine with nothing it could play from the very first move - out of book immediately. The
    // engine's own choice, `d2d3` here because that is what the fake engine returns, is added as a line of its own.
    std::ifstream     back{out, std::ios::binary};
    const std::string screened{std::istreambuf_iterator<char>(back), std::istreambuf_iterator<char>()};
    EXPECT_EQ(screened, "# a book\ne2e4? e7e5\nd2d3\n") << screened;
    EXPECT_NE(run.out.find("1 position given the engine's own move"), std::string::npos) << run.out;

    // -out is optional, and defaults to screening the book in place, which is how the engines' own books are kept.
    const auto in_place = ParseScreenBookArguments({"-engine", kFake, "-book", book.string()});
    ASSERT_TRUE(in_place) << in_place.ErrorMessage();
    EXPECT_EQ(in_place->settings.out, book.string());
    EXPECT_EQ(in_place->settings.depth, 12) << "depth 12 by default";
    EXPECT_EQ(in_place->settings.margin, 100) << "100 centipawns by default";

    EXPECT_EQ(RunWith({"screenbook", "-book", book.string()}).code, 2) << "no engine";
    EXPECT_EQ(RunWith({"screenbook", "-engine", kFake}).code, 2) << "no book";
    EXPECT_EQ(RunWith({"screenbook", "-engine", kFake, "-book", book.string(), "-depth", "0"}).code, 2);
    EXPECT_EQ(RunWith({"screenbook", "-engine", kFake, "-book", book.string(), "-margin", "0"}).code, 2);
    EXPECT_EQ(RunWith({"screenbook", "-engine", kFake, "-book", "no-such.book"}).code, 1);
    EXPECT_NE(RunWith({"--help"}).out.find("amphitheater-cli screenbook"), std::string::npos);
}

} // namespace
} // namespace amphitheater::cli
