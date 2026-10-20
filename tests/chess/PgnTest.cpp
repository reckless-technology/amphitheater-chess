// PGN: reading what real files hold (tests/data/assorted.pgn), writing the Seven Tag Roster, escapes and 80-column
// lines, and the round trip from text to games and back.

#include "chess/Pgn.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

namespace amphitheater::chess
{
namespace
{

std::string ReadData(const char *name)
{
    std::ifstream      file(std::string(AMPHITHEATER_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

TEST(Pgn, ReadsAssortedGamesAndSkipsTheBadOne)
{
    std::vector<std::string> errors;
    const auto               games = ReadPgn(ReadData("assorted.pgn"), &errors);
    for (const auto &error : errors)
    {
        std::printf("skipped: %s\n", error.c_str());
    }
    ASSERT_EQ(games.size(), 5u);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("game 5"), std::string::npos) << errors[0];
    EXPECT_NE(errors[0].find("Ke3"), std::string::npos) << errors[0];

    const PgnGame &first = games[0];
    EXPECT_EQ(first.Tag("White"), "White Player");
    EXPECT_EQ(first.Tag("ECO"), "C33");
    EXPECT_EQ(first.Tag("Nothing"), "");
    EXPECT_EQ(first.result, GameResult::kWhiteWins);
    ASSERT_EQ(first.moves.size(), 45u);
    EXPECT_EQ(first.moves.front().comment_before, "Both rooks and the queen are given up for mate.");
    EXPECT_EQ(first.moves[7].nags, (std::vector<int>{6}));  // 4...b5?!
    EXPECT_EQ(first.moves[20].nags, (std::vector<int>{1})); // 11.Rg1!
    EXPECT_EQ(first.moves[34].nags, (std::vector<int>{3})); // 18.Bd6!!
    EXPECT_NE(first.moves[35].comment.find("24.Be7#"), std::string::npos);
    EXPECT_EQ(first.moves.back().san, "Be7#");
    const Game game = first.ToGame();
    EXPECT_EQ(game.GetOutcome().termination, Termination::kCheckmate);

    const PgnGame &second = games[1];
    EXPECT_EQ(second.result, GameResult::kDraw);
    ASSERT_EQ(second.moves.size(), 9u);
    EXPECT_EQ(second.moves[0].nags, (std::vector<int>{1}));
    ASSERT_EQ(second.moves[1].variations.size(), 1u);
    const PgnLine &variation = second.moves[1].variations[0];
    ASSERT_EQ(variation.size(), 3u);
    EXPECT_EQ(variation[0].san, "Nf6");
    EXPECT_EQ(variation[2].nags, (std::vector<int>{2}));
    ASSERT_EQ(variation[1].variations.size(), 1u);
    EXPECT_EQ(variation[1].variations[0][1].san, "g6");
    EXPECT_EQ(second.moves[1].comment, "a rest-of-line comment");
    EXPECT_EQ(second.moves[3].comment, "a stray number before a comment");
    ASSERT_EQ(second.moves[4].variations.size(), 1u);
    EXPECT_EQ(second.moves[4].variations[0].size(), 3u);

    const PgnGame &third = games[2];
    EXPECT_EQ(third.start.ToFen(), "8/8/8/8/8/5k2/3q4/7K b - - 0 50");
    EXPECT_EQ(third.result, GameResult::kBlackWins);
    EXPECT_EQ(third.moves.size(), 3u);
    EXPECT_EQ(third.ToGame().GetOutcome().termination, Termination::kCheckmate);

    const PgnGame &fourth = games[3];
    EXPECT_EQ(fourth.result, GameResult::kNone);
    EXPECT_EQ(fourth.moves.size(), 17u);
    EXPECT_EQ(fourth.moves[8].san, "O-O");

    // The movetext's result wins over a Result tag that disagrees with it.
    EXPECT_EQ(games[4].result, GameResult::kBlackWins);
    EXPECT_EQ(games[4].Tag("Event"), "After the bad one");
}

TEST(Pgn, WritesTheSevenTagRosterFirstAndFillsWhatIsMissing)
{
    PgnGame game;
    game.SetTag("Annotator", "Someone");
    game.SetTag("White", "Quote \" and back\\slash");
    game.SetTag("White", "Replaced \"name\"");
    const std::string text = WritePgn(game);
    EXPECT_EQ(text,
              "[Event \"?\"]\n[Site \"?\"]\n[Date \"????.??.??\"]\n[Round \"?\"]\n[White \"Replaced \\\"name\\\"\"]\n"
              "[Black \"?\"]\n[Result \"*\"]\n[Annotator \"Someone\"]\n\n*\n\n");
    EXPECT_EQ(EscapeTagValue("a\\b\"c\nd"), "a\\\\b\\\"c d");
}

TEST(Pgn, WrapsMovetextAt80Columns)
{
    Game game;
    for (int round = 0; round < 12; ++round)
    {
        for (const char *move : {"Nf3", "Nf6", "Ng1", "Ng8"})
        {
            if (!game.IsOver())
            {
                game.PlaySan(move);
            }
        }
    }
    PgnGame pgn             = PgnGame::FromGame(game);
    pgn.moves[2].comment    = std::string(30, 'x') + " a long comment that must wrap across the line limit somewhere "
                                                     "in the middle, with words";
    const std::string  text = WritePgn(pgn);
    std::istringstream lines(text);
    std::string        line;
    int                count = 0;
    while (std::getline(lines, line))
    {
        EXPECT_LE(line.size(), 80u) << line;
        ++count;
    }
    EXPECT_GT(count, 9);
    EXPECT_NE(text.find("2... Ng8"), std::string::npos) << "a black move after a comment is numbered";
    EXPECT_NE(text.find("1/2-1/2"), std::string::npos);
}

TEST(Pgn, RoundTripsThroughText)
{
    const auto games = ReadPgn(ReadData("assorted.pgn"));
    for (const PgnGame &game : games)
    {
        const std::string        text = WritePgn(game);
        std::vector<std::string> errors;
        const auto               again = ReadPgn(text, &errors);
        ASSERT_TRUE(errors.empty()) << errors.front() << "\n" << text;
        ASSERT_EQ(again.size(), 1u);
        EXPECT_EQ(WritePgn(again[0]), text);
        EXPECT_EQ(again[0].moves.size(), game.moves.size());
        EXPECT_EQ(again[0].result, game.result);
        EXPECT_EQ(again[0].start, game.start);
    }
    // A game from another position carries its FEN and SetUp tags; one from the start does not.
    EXPECT_NE(WritePgn(games[2]).find("[FEN \"8/8/8/8/8/5k2/3q4/7K b - - 0 50\"]"), std::string::npos);
    EXPECT_EQ(WritePgn(games[0]).find("[FEN"), std::string::npos);
}

TEST(Pgn, FromGameAddsAPositionsTagsAndToGameKeepsTheResult)
{
    Game game(*Position::FromFen("8/8/8/8/8/5k2/3q4/7K b - - 0 50"));
    game.PlaySan("Kg3");
    PgnGame pgn = PgnGame::FromGame(game);
    EXPECT_EQ(pgn.Tag("SetUp"), "1");
    EXPECT_EQ(pgn.Tag("FEN"), "8/8/8/8/8/5k2/3q4/7K b - - 0 50");
    EXPECT_NE(WritePgn(pgn).find("50... Kg3 *"), std::string::npos) << WritePgn(pgn);
    pgn.result = GameResult::kBlackWins;
    for (const auto &[tag, reason] :
         {std::pair{"time forfeit", Termination::kTimeForfeit},
          std::pair{"rules infraction", Termination::kIllegalMove},
          std::pair{"adjudication", Termination::kAdjudication}, std::pair{"abandoned", Termination::kAbandoned},
          std::pair{"normal", Termination::kRecorded}})
    {
        pgn.SetTag("Termination", tag);
        const Game back = pgn.ToGame();
        EXPECT_EQ(back.GetOutcome().result, GameResult::kBlackWins);
        EXPECT_EQ(back.GetOutcome().termination, reason) << tag;
    }
}

TEST(Pgn, ReportsEveryKindOfBrokenGame)
{
    const struct
    {
        const char *text;
        const char *reason;
    } cases[] = {
        {"[Event \"x\" \n1. e4 *", "closing bracket"},
        {"[Event \"x]\n1. e4 *", "closing quote"},
        {"[\"x\"]\n1. e4 *", "name and a quoted value"},
        {"[Event x]\n1. e4 *", "name and a quoted value"},
        {"[FEN \"bad\"]\n1. e4 *", "FEN tag"},
        {"1. e4 {never closed", "not closed"},
        {"1. e4 (1. d4 e5", "not closed"},
        {"(1. d4) *", "no move before it"},
        {"1. e4 ) *", "closes no variation"},
        {"$1 1. e4 *", "NAG"},
        {"1. e4 $ *", "NAG"},
        {"!? 1. e4 *", "annotation"},
        {"1. e4 } *", "stray"},
        {"1. e4 -- *", "null moves"},
        {"1. e4 (1. d4 1-0) *", "inside a variation"},
        {"1. e4 (1. d4\n[Event \"next\"]\n1. e4 *", "not closed"},
    };

    for (const auto &entry : cases)
    {
        std::vector<std::string> errors;
        const auto               games = ReadPgn(entry.text, &errors);
        ASSERT_EQ(errors.size(), 1u) << entry.text;
        EXPECT_NE(errors[0].find(entry.reason), std::string::npos) << entry.text << " said: " << errors[0];
    }
}

TEST(Pgn, ATagErrorSkipsTheWholeHeaderAndTheNextGameStillReads)
{
    const std::string        text = "[Event \"broken]\n[White \"A\"]\n[Black \"B\"]\n\n1. e4 e5 *\n\n[Event \"good\"]\n"
                                    "\n1. d4 d5 1/2-1/2\n";
    std::vector<std::string> errors;
    const auto               games = ReadPgn(text, &errors);
    ASSERT_EQ(games.size(), 1u);
    EXPECT_EQ(games[0].Tag("Event"), "good");
    EXPECT_EQ(errors.size(), 1u);
}

TEST(Pgn, ReadsAByteOrderMarkAnEmptyGameAndAComment)
{
    std::vector<std::string> errors;
    const auto               games = ReadPgn("\xEF\xBB\xBF[Event \"e\"]\n\n{just a comment} *\n", &errors);
    ASSERT_EQ(games.size(), 1u) << (errors.empty() ? std::string{} : errors[0]);
    EXPECT_EQ(games[0].comment, "just a comment");
    EXPECT_TRUE(games[0].moves.empty());
    EXPECT_NE(WritePgn(games[0]).find("{just a comment} *"), std::string::npos);
    EXPECT_TRUE(ReadPgn("").empty());
    EXPECT_TRUE(ReadPgn("  \n% only an escape\n").empty());
}

TEST(Pgn, ReaderReportsTheEnd)
{
    PgnReader   reader("1. e4 *");
    PgnGame     game;
    std::string error;
    EXPECT_TRUE(reader.Next(game, error));
    EXPECT_TRUE(error.empty());
    EXPECT_FALSE(reader.Next(game, error));
}

} // namespace
} // namespace amphitheater::chess
