// The protocol's parsers and formatters: every info field, every option type, and what malformed lines leave.

#include "uci/UciProtocol.h"

#include <gtest/gtest.h>

namespace uci_host::uci
{
namespace
{

TEST(UciProtocol, SplitsWordsOnAnyWhitespace)
{
    const auto words = SplitWords("  info\tdepth  3 \r");
    ASSERT_EQ(words.size(), 3u);
    EXPECT_EQ(words[0], "info");
    EXPECT_EQ(words[2], "3");
    EXPECT_TRUE(SplitWords("   ").empty());
}

TEST(UciProtocol, ParsesEveryInfoField)
{
    const auto info = ParseInfoLine("info depth 18 seldepth 27 multipv 2 score cp -35 lowerbound nodes 123456 nps "
                                    "987654 hashfull 512 tbhits 7 time 1250 cpuload 950 currmove e2e4 "
                                    "currmovenumber 3 pv e2e4 e7e5 g1f3");
    EXPECT_EQ(info.depth, 18);
    EXPECT_EQ(info.selective_depth, 27);
    EXPECT_EQ(info.multipv, 2);
    ASSERT_TRUE(info.score);
    EXPECT_EQ(info.score->kind, Score::Kind::kCentipawns);
    EXPECT_EQ(info.score->value, -35);
    EXPECT_TRUE(info.score->is_lower_bound);
    EXPECT_FALSE(info.score->is_upper_bound);
    EXPECT_EQ(info.nodes, 123456);
    EXPECT_EQ(info.nodes_per_second, 987654);
    EXPECT_EQ(info.hash_full, 512);
    EXPECT_EQ(info.tablebase_hits, 7);
    EXPECT_EQ(info.time, 1250);
    EXPECT_EQ(info.cpu_load, 950);
    EXPECT_EQ(info.current_move, "e2e4");
    EXPECT_EQ(info.current_move_number, 3);
    EXPECT_EQ(info.pv, (std::vector<std::string>{"e2e4", "e7e5", "g1f3"}));
    EXPECT_FALSE(info.text);
}

TEST(UciProtocol, ParsesMateScoresAndBounds)
{
    const auto mate = ParseInfoLine("info score mate -3 upperbound");
    ASSERT_TRUE(mate.score);
    EXPECT_EQ(mate.score->kind, Score::Kind::kMate);
    EXPECT_EQ(mate.score->value, -3);
    EXPECT_TRUE(mate.score->is_upper_bound);
    EXPECT_EQ(mate.score->ToString(), "#-3");
    Score score;
    score.value = 35;
    EXPECT_EQ(score.ToString(), "+0.35");
    score.value = -120;
    EXPECT_EQ(score.ToString(), "-1.20");
}

TEST(UciProtocol, ScoresTurnToWhitesSide)
{
    const Score plus{Score::Kind::kCentipawns, 35, false, false};
    EXPECT_EQ(WhitesView(plus, chess::kWhite), plus) << "White to move: White's side already";
    EXPECT_EQ(WhitesView(plus, chess::kBlack).value, -35) << "Black to move and better: Black is ahead";
    EXPECT_EQ(WhiteScoreWords(plus, chess::kBlack), "-0.35");
    EXPECT_EQ(WhiteScoreWords({Score::Kind::kCentipawns, -120, false, false}, chess::kBlack), "+1.20");

    // Mates: the side to move mating is White mating with White to move and Black mating with Black to move.
    const Score mates{Score::Kind::kMate, 4, false, false};
    const Score mated{Score::Kind::kMate, -2, false, false};
    EXPECT_EQ(WhiteScoreWords(mates, chess::kWhite), "#4");
    EXPECT_EQ(WhiteScoreWords(mates, chess::kBlack), "#-4");
    EXPECT_EQ(WhiteScoreWords(mated, chess::kWhite), "#-2");
    EXPECT_EQ(WhiteScoreWords(mated, chess::kBlack), "#2");
    EXPECT_EQ(WhitesView(mates, chess::kBlack).kind, Score::Kind::kMate);

    // Bounds swap with the sign for Black, and stay for White.
    const Score lower{Score::Kind::kCentipawns, 50, true, false};
    const Score upper{Score::Kind::kMate, -5, false, true};
    EXPECT_EQ(WhiteScoreWords(lower, chess::kWhite), "+0.50 lowerbound");
    EXPECT_EQ(WhiteScoreWords(lower, chess::kBlack), "-0.50 upperbound") << "Black at least +0.50: White at most -0.50";
    EXPECT_EQ(WhiteScoreWords(upper, chess::kBlack), "#5 lowerbound");
    const Score turned = WhitesView(lower, chess::kBlack);
    EXPECT_FALSE(turned.is_lower_bound);
    EXPECT_TRUE(turned.is_upper_bound);
    EXPECT_EQ(WhitesView(turned, chess::kBlack), lower) << "the turn is its own inverse";
}

TEST(UciProtocol, PvEndsAtTheNextKeyword)
{
    const auto info = ParseInfoLine("info pv e2e4 e7e5 depth 5 string hello   world");
    EXPECT_EQ(info.pv, (std::vector<std::string>{"e2e4", "e7e5"}));
    EXPECT_EQ(info.depth, 5);
    EXPECT_EQ(info.text, "hello   world");
}

TEST(UciProtocol, StringTakesTheRestOfTheLineVerbatim)
{
    const auto info = ParseInfoLine("info string depth 3 is not a field here");
    EXPECT_FALSE(info.depth);
    EXPECT_EQ(info.text, "depth 3 is not a field here");
    EXPECT_EQ(ParseInfoLine("info string").text, "");
}

TEST(UciProtocol, MalformedInfoLeavesFieldsUnset)
{
    const auto info = ParseInfoLine("info depth banana score cp nodes 12x time -5 score weird 3 multipv");
    EXPECT_FALSE(info.depth);
    EXPECT_FALSE(info.score);
    EXPECT_FALSE(info.nodes);
    EXPECT_EQ(info.time, -5);
    EXPECT_FALSE(info.multipv);
    EXPECT_EQ(ParseInfoLine("info depth +7").depth, 7);
    EXPECT_FALSE(ParseInfoLine("info currmove").current_move);
}

TEST(UciProtocol, ParsesBestMove)
{
    auto best = ParseBestMoveLine("bestmove e2e4 ponder e7e5");
    ASSERT_TRUE(best);
    EXPECT_EQ(best->move, "e2e4");
    EXPECT_EQ(best->ponder, "e7e5");
    best = ParseBestMoveLine("bestmove e7e8q");
    ASSERT_TRUE(best);
    EXPECT_FALSE(best->ponder);
    best = ParseBestMoveLine("bestmove");
    ASSERT_TRUE(best);
    EXPECT_EQ(best->move, "");
    EXPECT_FALSE(ParseBestMoveLine("info depth 1"));
    EXPECT_FALSE(ParseBestMoveLine(""));
}

TEST(UciProtocol, ParsesEveryOptionType)
{
    auto spin = ParseOptionLine("option name Hash type spin default 16 min 1 max 33554432");
    ASSERT_TRUE(spin) << spin.ErrorMessage();
    EXPECT_EQ(spin->name, "Hash");
    EXPECT_EQ(spin->type, OptionType::kSpin);
    EXPECT_EQ(spin->default_value, "16");
    EXPECT_EQ(spin->minimum, 1);
    EXPECT_EQ(spin->maximum, 33554432);

    auto check = ParseOptionLine("option name Use NNUE type check default true");
    ASSERT_TRUE(check);
    EXPECT_EQ(check->name, "Use NNUE");
    EXPECT_EQ(check->type, OptionType::kCheck);
    EXPECT_EQ(check->default_value, "true");

    auto combo = ParseOptionLine("option name Analysis Contempt type combo default Both var Off var White var Black "
                                 "var Both");
    ASSERT_TRUE(combo);
    EXPECT_EQ(combo->type, OptionType::kCombo);
    EXPECT_EQ(combo->choices, (std::vector<std::string>{"Off", "White", "Black", "Both"}));
    EXPECT_EQ(combo->default_value, "Both");

    auto button = ParseOptionLine("option name Clear Hash type button");
    ASSERT_TRUE(button);
    EXPECT_EQ(button->type, OptionType::kButton);
    EXPECT_TRUE(button->default_value.empty());

    auto text = ParseOptionLine("option name SyzygyPath type string default <empty>");
    ASSERT_TRUE(text);
    EXPECT_EQ(text->type, OptionType::kString);
    EXPECT_EQ(text->default_value, "");
    auto spaced = ParseOptionLine("option name Book type string default C:/My Books/var book.bin");
    ASSERT_TRUE(spaced);
    EXPECT_EQ(spaced->default_value, "C:/My Books/var book.bin");
    EXPECT_EQ(OptionTypeName(OptionType::kCheck), "check");
    EXPECT_EQ(OptionTypeName(OptionType::kSpin), "spin");
    EXPECT_EQ(OptionTypeName(OptionType::kCombo), "combo");
    EXPECT_EQ(OptionTypeName(OptionType::kButton), "button");
    EXPECT_EQ(OptionTypeName(OptionType::kString), "string");
}

TEST(UciProtocol, RefusesMalformedOptionLines)
{
    EXPECT_FALSE(ParseOptionLine("option type spin"));
    EXPECT_FALSE(ParseOptionLine("option name Hash"));
    EXPECT_FALSE(ParseOptionLine("option name type spin"));
    EXPECT_FALSE(ParseOptionLine("option name Hash type float default 1"));
    EXPECT_FALSE(ParseOptionLine("id name X"));
}

TEST(UciProtocol, ValidatesOptionValues)
{
    const auto spin = *ParseOptionLine("option name Hash type spin default 16 min 1 max 64");
    EXPECT_FALSE(spin.Validate("32"));
    EXPECT_TRUE(spin.Validate("0"));
    EXPECT_TRUE(spin.Validate("65"));
    EXPECT_TRUE(spin.Validate("big"));
    const auto check = *ParseOptionLine("option name Ponder type check default false");
    EXPECT_FALSE(check.Validate("true"));
    EXPECT_TRUE(check.Validate("yes"));
    const auto combo = *ParseOptionLine("option name Style type combo default Normal var Solid var Normal");
    EXPECT_FALSE(combo.Validate("solid"));
    EXPECT_TRUE(combo.Validate("Wild"));
    const auto button = *ParseOptionLine("option name Clear Hash type button");
    EXPECT_FALSE(button.Validate(""));
    EXPECT_TRUE(button.Validate("now"));
    const auto text = *ParseOptionLine("option name Path type string default x");
    EXPECT_FALSE(text.Validate("anything at all"));
}

TEST(UciProtocol, FormatsGoWithEveryParameter)
{
    GoParameters parameters;
    EXPECT_EQ(FormatGo(parameters), "go");
    parameters.white_time      = chess::Milliseconds{60000};
    parameters.black_time      = chess::Milliseconds{-5};
    parameters.white_increment = chess::Milliseconds{600};
    parameters.black_increment = chess::Milliseconds{700};
    parameters.moves_to_go     = 40;
    parameters.depth           = 12;
    parameters.nodes           = 100000;
    parameters.mate            = 3;
    parameters.move_time       = chess::Milliseconds{1500};
    parameters.is_infinite     = true;
    parameters.is_ponder       = true;
    parameters.search_moves    = {"e2e4", "d2d4"};
    EXPECT_EQ(FormatGo(parameters), "go ponder wtime 60000 btime 0 winc 600 binc 700 movestogo 40 depth 12 nodes "
                                    "100000 mate 3 movetime 1500 infinite searchmoves e2e4 d2d4");
}

TEST(UciProtocol, FormatsPositionAndSetOption)
{
    EXPECT_EQ(FormatPosition("", {}), "position startpos");
    EXPECT_EQ(FormatPosition("", {"e2e4", "e7e5"}), "position startpos moves e2e4 e7e5");
    EXPECT_EQ(FormatPosition("8/8/8/8/8/8/8/K6k w - - 0 1", {"a1a2"}),
              "position fen 8/8/8/8/8/8/8/K6k w - - 0 1 moves a1a2");
    EXPECT_EQ(FormatSetOption("Hash", "64"), "setoption name Hash value 64");
    EXPECT_EQ(FormatSetOption("Clear Hash", ""), "setoption name Clear Hash");
}

} // namespace
} // namespace uci_host::uci
