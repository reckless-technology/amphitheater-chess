// Games: the rules that end them (mate, stalemate, insufficient material, threefold repetition with its en passant
// subtlety, fifty moves), undo, and the move lists.

#include "chess/Game.h"

#include <gtest/gtest.h>

namespace uci_host::chess
{
namespace
{

Game FromFen(const char *fen)
{
    const auto position = Position::FromFen(fen, FenMode::kTolerant);
    EXPECT_TRUE(position) << position.ErrorMessage();
    return Game(*position);
}

void PlaySan(Game &game, std::initializer_list<const char *> moves)
{
    for (const char *move : moves)
    {
        const auto played = game.PlaySan(move);
        ASSERT_TRUE(played) << move << ": " << played.ErrorMessage();
    }
}

TEST(Game, FoolsMateIsCheckmate)
{
    Game game;
    PlaySan(game, {"f3", "e5", "g4", "Qh4#"});
    EXPECT_TRUE(game.IsOver());
    EXPECT_EQ(game.GetOutcome().result, GameResult::kBlackWins);
    EXPECT_EQ(game.GetOutcome().termination, Termination::kCheckmate);
    EXPECT_EQ(game.GetOutcome().description, "Black mates");
    EXPECT_TRUE(game.CurrentPosition().IsCheckmate());
    EXPECT_FALSE(game.PlayUci("e2e4"));
    EXPECT_FALSE(game.PlaySan("e4"));
    EXPECT_FALSE(game.Play(Move::None()));
    EXPECT_EQ(game.SanMoves(), (std::vector<std::string>{"f3", "e5", "g4", "Qh4#"}));
    EXPECT_EQ(game.UciMoves(), (std::vector<std::string>{"f2f3", "e7e5", "g2g4", "d8h4"}));
    EXPECT_EQ(game.PlyCount(), 4);
    EXPECT_TRUE(game.StartsFromStandardPosition());
    // Undo reopens it.
    EXPECT_TRUE(game.Undo());
    EXPECT_FALSE(game.IsOver());
    EXPECT_EQ(game.PlyCount(), 3);
}

TEST(Game, WhiteMatesToo)
{
    Game game = FromFen("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1");
    PlaySan(game, {"Ra8#"});
    EXPECT_EQ(game.GetOutcome().result, GameResult::kWhiteWins);
    EXPECT_EQ(game.GetOutcome().description, "White mates");
}

TEST(Game, Stalemate)
{
    Game game = FromFen("7k/5Q2/6K1/8/8/8/8/8 w - - 0 1");
    EXPECT_FALSE(game.IsOver());
    PlaySan(game, {"Qf6"});
    EXPECT_FALSE(game.IsOver()) << "Kg8 is still there";
    game = FromFen("k7/2Q5/1K6/8/8/8/8/8 b - - 0 1");
    EXPECT_TRUE(game.IsOver());
    EXPECT_EQ(game.GetOutcome().termination, Termination::kStalemate);
    EXPECT_EQ(game.GetOutcome().result, GameResult::kDraw);
    EXPECT_TRUE(game.CurrentPosition().IsStalemate());
}

TEST(Game, InsufficientMaterial)
{
    const struct
    {
        const char *fen;
        bool        is_insufficient;
    } cases[] = {
        {"8/8/4k3/8/8/4K3/8/8 w - - 0 1", true},     // King against king.
        {"8/8/4k3/8/8/4KN2/8/8 w - - 0 1", true},    // A knight.
        {"8/8/4k3/8/8/4KB2/8/8 w - - 0 1", true},    // A bishop.
        {"8/8/2b1k3/8/8/4KB2/8/8 w - - 0 1", true},  // Bishops on squares of one colour (c6 and f3 are light).
        {"8/8/3bk3/8/8/4KB2/8/8 w - - 0 1", false},  // Bishops on squares of both colours.
        {"8/8/4k3/8/8/2B1KB2/8/8 w - - 0 1", false}, // Two bishops of one side, both colours.
        {"8/8/4k3/8/8/4KNN1/8/8 w - - 0 1", false},  // Two knights.
        {"8/8/4kn2/8/8/4KB2/8/8 w - - 0 1", false},  // A knight and a bishop.
        {"8/8/4k3/8/8/4K3/4P3/8 w - - 0 1", false},  // A pawn.
        {"8/8/4k3/8/8/4K3/4R3/8 w - - 0 1", false},  // A rook.
    };

    for (const auto &entry : cases)
    {
        const Game game = FromFen(entry.fen);
        EXPECT_EQ(game.CurrentPosition().HasInsufficientMaterial(), entry.is_insufficient) << entry.fen;
        EXPECT_EQ(game.GetOutcome().termination == Termination::kInsufficientMaterial, entry.is_insufficient)
            << entry.fen;
    }
    // A capture that leaves too little ends the game.
    Game game = FromFen("8/8/4k3/8/8/4K3/4r3/8 w - - 0 1");
    PlaySan(game, {"Kxe2"});
    EXPECT_EQ(game.GetOutcome().termination, Termination::kInsufficientMaterial);
    EXPECT_EQ(game.GetOutcome().description, "Draw by insufficient mating material");
}

TEST(Game, ThreefoldRepetition)
{
    Game game;
    PlaySan(game, {"Nf3", "Nf6", "Ng1", "Ng8", "Nf3", "Nf6", "Ng1"});
    EXPECT_FALSE(game.IsOver());
    EXPECT_EQ(game.RepetitionCount(), 2);
    PlaySan(game, {"Ng8"});
    EXPECT_TRUE(game.IsOver());
    EXPECT_EQ(game.RepetitionCount(), 3);
    EXPECT_EQ(game.GetOutcome().termination, Termination::kThreefoldRepetition);
    EXPECT_EQ(game.GetOutcome().result, GameResult::kDraw);
}

TEST(Game, RepetitionNeedsTheSameCastlingRights)
{
    // The first position had castling rights; after the rooks have moved and come back, it is not the same.
    Game game = FromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    PlaySan(game, {"Rb1", "Rb8", "Ra1", "Ra8", "Rb1", "Rb8", "Ra1", "Ra8"});
    EXPECT_FALSE(game.IsOver()) << "the start had rights the later positions lack";
    // The position after 1. Rb1 Rb8 (both queenside rights gone) comes a third time two plies on.
    PlaySan(game, {"Rb1", "Rb8"});
    EXPECT_TRUE(game.IsOver());
}

TEST(Game, AnEnPassantSquareCountsOnlyWhenTheCaptureIsLegal)
{
    // After 1. e4 the en passant square is e3, but no black pawn can take: the position after 1. e4 Nf6 2. Nf3 Ng8
    // 3. Ng1 Nf6 ... repeats with the first position after 1. e4 Nf6.
    Game game;
    PlaySan(game, {"e4", "Nf6", "Nf3", "Ng8", "Ng1", "Nf6", "Nf3", "Ng8", "Ng1"});
    EXPECT_EQ(game.RepetitionCount(), 3);
    EXPECT_TRUE(game.IsOver());
    // Here the capture is legal at first, so the first position differs from its repetitions.
    game = FromFen("4k3/8/8/8/5p2/8/4P3/R3K3 w - - 0 1");
    PlaySan(game, {"e4", "Kd7", "Rb1", "Ke8", "Ra1", "Kd7", "Rb1", "Ke8", "Ra1"});
    EXPECT_EQ(game.RepetitionCount(), 2);
    EXPECT_FALSE(game.IsOver());
}

TEST(Game, FiftyMoveRule)
{
    Game game = FromFen("4k3/8/8/8/8/8/8/R3K3 w - - 98 80");
    PlaySan(game, {"Rb1"});
    EXPECT_FALSE(game.IsOver());
    PlaySan(game, {"Kd7"});
    EXPECT_TRUE(game.IsOver());
    EXPECT_EQ(game.GetOutcome().termination, Termination::kFiftyMoves);
    EXPECT_EQ(game.GetOutcome().description, "Draw by fifty moves rule");
    // A mate on the hundredth half-move is a mate.
    game = FromFen("6k1/5ppp/8/8/8/8/8/R5K1 w - - 99 80");
    PlaySan(game, {"Ra8#"});
    EXPECT_EQ(game.GetOutcome().termination, Termination::kCheckmate);
}

TEST(Game, CastlingAdvancesTheHalfmoveClockAndPawnMovesReset)
{
    Game game = FromFen("r3k2r/8/8/8/8/8/4P3/R3K2R w KQkq - 5 10");
    PlaySan(game, {"O-O"});
    EXPECT_EQ(game.CurrentPosition().HalfmoveClock(), 6);
    EXPECT_EQ(game.CurrentPosition().Castling().ToFenString(), "kq");
    PlaySan(game, {"O-O-O"});
    EXPECT_EQ(game.CurrentPosition().HalfmoveClock(), 7);
    EXPECT_EQ(game.CurrentPosition().FullmoveNumber(), 11);
    PlaySan(game, {"e4"});
    EXPECT_EQ(game.CurrentPosition().HalfmoveClock(), 0);
    EXPECT_EQ(game.CurrentPosition().ToFen(), "2kr3r/8/8/8/4P3/8/8/R4RK1 b - e3 0 11");
}

TEST(Game, PlaysOnlyLegalMovesAndUndoesToTheStart)
{
    Game game;
    EXPECT_FALSE(game.PlayUci("e2e5"));
    EXPECT_FALSE(game.PlaySan("Ke2"));
    EXPECT_FALSE(game.Undo());
    const auto move = game.PlayUci("e2e4");
    ASSERT_TRUE(move);
    EXPECT_EQ(game.San(0), "e4");
    EXPECT_FALSE(game.Play(*move)) << "not legal for black";
    EXPECT_EQ(game.PositionAt(0), Position::Start());
    EXPECT_TRUE(game.Undo());
    EXPECT_EQ(game.CurrentPosition(), Position::Start());
}

TEST(Game, SetOutcomeEndsAGame)
{
    Game game;
    game.SetOutcome({GameResult::kWhiteWins, Termination::kTimeForfeit, "Black loses on time"});
    EXPECT_TRUE(game.IsOver());
    EXPECT_FALSE(game.PlayUci("e2e4"));
}

TEST(Game, ResultAndTerminationText)
{
    EXPECT_EQ(ResultText(GameResult::kWhiteWins), "1-0");
    EXPECT_EQ(ResultText(GameResult::kBlackWins), "0-1");
    EXPECT_EQ(ResultText(GameResult::kDraw), "1/2-1/2");
    EXPECT_EQ(ResultText(GameResult::kNone), "*");
    for (const char *text : {"1-0", "0-1", "1/2-1/2", "*"})
    {
        EXPECT_EQ(ResultText(ParseResultText(text)), text);
    }
    EXPECT_EQ(ParseResultText("2-0"), GameResult::kNone);
    EXPECT_EQ(TerminationTag(Termination::kNone), "unterminated");
    EXPECT_EQ(TerminationTag(Termination::kCheckmate), "normal");
    EXPECT_EQ(TerminationTag(Termination::kTimeForfeit), "time forfeit");
    EXPECT_EQ(TerminationTag(Termination::kIllegalMove), "rules infraction");
    EXPECT_EQ(TerminationTag(Termination::kResignation), "adjudication");
    EXPECT_EQ(TerminationTag(Termination::kAdjudication), "adjudication");
    EXPECT_EQ(TerminationTag(Termination::kDisconnection), "abandoned");
    EXPECT_EQ(TerminationTag(Termination::kStalled), "abandoned");
    EXPECT_EQ(TerminationTag(Termination::kAbandoned), "abandoned");
    EXPECT_EQ(TerminationTag(Termination::kRecorded), "normal");
}

} // namespace
} // namespace uci_host::chess
