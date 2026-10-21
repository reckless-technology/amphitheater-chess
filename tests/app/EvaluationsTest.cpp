// Scores from White's side everywhere the window shows one: the graph's points from an engine's own score with either
// side to move, mates both ways, bounds, a ponder search's side to move, a game's two series from its notes, a PGN's
// series from its comments (the host's own and [%eval]), and a PGN with scores opened on the window's graph.

#include "app/Evaluations.h"
#include "app/EvalGraph.h"
#include "app/Faces.h"
#include "app/MainController.h"
#include "app/MainView.h"
#include "app/PieceSet.h"
#include "app/SearchTracker.h"
#include "chess/Pgn.h"

#include <gtest/gtest.h>

#include <chrono>

namespace amphitheater::app
{
namespace
{

using Score = uci::Score;

/** The pieces, read once for every test. */
const PieceSet &Pieces()
{
    static const PieceSet pieces = [] {
        InstallFaces();
        return PieceSet::FromEmbedded();
    }();
    return pieces;
}

/**
 * A note of a move.
 * @param mover Who moved.
 * @param score Its engine's own score, if any.
 * @return The note.
 */
match::MoveNote Note(chess::Color mover, std::optional<Score> score)
{
    match::MoveNote note;
    note.mover = mover;
    note.score = score;
    note.depth = 12;
    return note;
}

} // namespace

TEST(Evaluations, AScoreTurnsToWhitesSideWithEitherSideToMove)
{
    const Score plus{Score::Kind::kCentipawns, 100, false, false};
    EXPECT_EQ(EvalPointOf(plus, chess::kWhite), (EvalPoint{1.0, std::nullopt}));
    EXPECT_EQ(EvalPointOf(plus, chess::kBlack), (EvalPoint{-1.0, std::nullopt}));
    EXPECT_EQ(EvalText(EvalPointOf(plus, chess::kBlack)), "-1.00");
    EXPECT_EQ(WhiteScoreText(plus, chess::kBlack), "-1.00");
    EXPECT_DOUBLE_EQ(WhitePawns(plus, chess::kBlack), -1.0);

    // Mates both ways, held at the graph's mate value on the mating side.
    const Score mates{Score::Kind::kMate, 3, false, false};
    const Score mated{Score::Kind::kMate, -3, false, false};
    EXPECT_EQ(EvalPointOf(mates, chess::kWhite), (EvalPoint{kMatePawns, 3}));
    EXPECT_EQ(EvalPointOf(mates, chess::kBlack), (EvalPoint{-kMatePawns, -3}));
    EXPECT_EQ(EvalPointOf(mated, chess::kWhite), (EvalPoint{-kMatePawns, -3}));
    EXPECT_EQ(EvalPointOf(mated, chess::kBlack), (EvalPoint{kMatePawns, 3}));
    EXPECT_EQ(WhiteScoreText(mates, chess::kBlack), "#-3");
    EXPECT_EQ(WhiteScoreText(mated, chess::kBlack), "#3");

    // A mate in 0 is against the side to move.
    const Score over{Score::Kind::kMate, 0, false, false};
    EXPECT_LT(EvalPointOf(over, chess::kWhite).pawns, 0.0);
    EXPECT_GT(EvalPointOf(over, chess::kBlack).pawns, 0.0);

    // Bounds swap with the sign: Black's "at least +0.50" is White's "at most -0.50".
    const Score lower{Score::Kind::kCentipawns, 50, true, false};
    const Score upper{Score::Kind::kCentipawns, 50, false, true};
    EXPECT_EQ(WhiteScoreText(lower, chess::kWhite), ">+0.50");
    EXPECT_EQ(WhiteScoreText(lower, chess::kBlack), "<-0.50");
    EXPECT_EQ(WhiteScoreText(upper, chess::kWhite), "<+0.50");
    EXPECT_EQ(WhiteScoreText(upper, chess::kBlack), ">-0.50");
}

TEST(Evaluations, APonderSearchsScoreIsFromThePositionAfterTheExpectedReply)
{
    const auto now = std::chrono::steady_clock::now();

    // White's engine has played 1.e4 and ponders on 1...e5: it searches with White to move, its +0.30 White's.
    SearchTracker white("White's engine");
    white.Feed(true, "position startpos moves e2e4 e7e5", now);
    white.Feed(true, "go ponder wtime 60000 btime 60000", now);
    white.Feed(false, "info depth 9 score cp 30 pv g1f3 b8c6", now);
    EXPECT_TRUE(white.IsPondering());
    EXPECT_EQ(white.Block(now).state, "pondering 1...e5");
    EXPECT_EQ(white.BestScoreText(), std::optional<std::string>("+0.30"));
    EXPECT_EQ(white.BestEvalPoint(), std::optional<EvalPoint>(EvalPoint{0.30, std::nullopt}));
    white.Feed(true, "ponderhit", now);
    white.Feed(false, "info depth 10 score cp 32 pv g1f3 b8c6", now);
    EXPECT_EQ(white.BestScoreText(), std::optional<std::string>("+0.32")) << "the same position after ponderhit";

    // Black's engine has played 1...e5 and ponders on 2.Nf3: Black to move, its +0.30 Black's, -0.30 from White's side.
    SearchTracker black("Black's engine");
    black.Feed(true, "position startpos moves e2e4 e7e5 g1f3", now);
    black.Feed(true, "go ponder wtime 60000 btime 60000", now);
    black.Feed(false, "info depth 9 score cp 30 pv b8c6", now);
    EXPECT_EQ(black.BestScoreText(), std::optional<std::string>("-0.30"));
    ASSERT_EQ(black.Block(now).lines.size(), 1u);
    EXPECT_EQ(black.Block(now).lines[0].sign, -1);
    black.Feed(false, "info depth 9 score mate 2 pv b8c6", now);
    EXPECT_EQ(black.BestScoreText(), std::optional<std::string>("#-2")) << "Black mates";
}

TEST(Evaluations, AGamesNotesMakeASeriesForEachEngineAtItsOwnPlies)
{
    const Score                  own{Score::Kind::kCentipawns, 100, false, false};
    std::vector<match::MoveNote> notes;
    notes.push_back(Note(chess::kWhite, std::nullopt)); // a book move
    notes.push_back(Note(chess::kBlack, std::nullopt));
    for (int ply = 2; ply < 8; ++ply)
    {
        notes.push_back(Note(ply % 2 == 0 ? chess::kWhite : chess::kBlack, own));
    }
    const auto series = SeriesOfNotes(notes, 9, {"Alpha", "Beta"}, {true, true});
    ASSERT_EQ(series.size(), 2u);
    EXPECT_EQ(series[0].name, "Alpha");
    EXPECT_EQ(series[1].name, "Beta");
    ASSERT_EQ(series[0].points.size(), 9u) << "as long as the game, its last move not yet noted";
    for (std::size_t ply = 0; ply < 9; ++ply)
    {
        const bool has_score = ply >= 2 && ply < 8;
        const bool is_white  = ply % 2 == 0;
        EXPECT_EQ(series[0].points[ply].has_value(), has_score && is_white) << ply;
        EXPECT_EQ(series[1].points[ply].has_value(), has_score && !is_white) << ply;
        if (has_score)
        {
            EXPECT_DOUBLE_EQ(series[is_white ? 0 : 1].points[ply]->pawns, is_white ? 1.0 : -1.0) << ply;
        }
    }

    // A person's side has no series.
    const auto alone = SeriesOfNotes(notes, 8, {"You", "Beta"}, {false, true});
    ASSERT_EQ(alone.size(), 1u);
    EXPECT_EQ(alone[0].name, "Beta");
}

TEST(Evaluations, ACommentsScoreIsReadFromWhitesSide)
{
    EXPECT_EQ(EvalPointOfComment("+0.35/18 1.2s"), std::optional<EvalPoint>(EvalPoint{0.35, std::nullopt}));
    EXPECT_EQ(EvalPointOfComment("-1.20/9 0.4s"), std::optional<EvalPoint>(EvalPoint{-1.2, std::nullopt}));
    EXPECT_EQ(EvalPointOfComment("+M5/20 0.3s"), std::optional<EvalPoint>(EvalPoint{kMatePawns, 5}));
    EXPECT_EQ(EvalPointOfComment("-M3 0.1s, Black mates"), std::optional<EvalPoint>(EvalPoint{-kMatePawns, -3}));
    EXPECT_EQ(EvalPointOfComment("[%eval 0.17] [%clk 0:05:00]"),
              std::optional<EvalPoint>(EvalPoint{0.17, std::nullopt}));
    EXPECT_EQ(EvalPointOfComment("[%eval #-4]"), std::optional<EvalPoint>(EvalPoint{-kMatePawns, -4}));
    EXPECT_FALSE(EvalPointOfComment("book"));
    EXPECT_FALSE(EvalPointOfComment("1.2s"));
    EXPECT_FALSE(EvalPointOfComment("A fine move"));
    EXPECT_FALSE(EvalPointOfComment(""));
    EXPECT_FALSE(EvalPointOfComment("+"));
}

TEST(Evaluations, APgnWrittenFromAGameReadsBackAsTheSameSeries)
{
    // A game record's PGN, written and read back, gives the series its notes gave.
    match::GameRecord record;
    for (const char *san : {"e4", "e5", "Nf3", "Nc6"})
    {
        ASSERT_TRUE(record.game.PlaySan(san));
    }
    record.white_name       = "Alpha";
    record.black_name       = "Beta";
    record.notes            = {Note(chess::kWhite, Score{Score::Kind::kCentipawns, 30, false, false}),
                               Note(chess::kBlack, Score{Score::Kind::kCentipawns, 20, false, false}),
                               Note(chess::kWhite, Score{Score::Kind::kMate, 7, false, false}),
                               Note(chess::kBlack, Score{Score::Kind::kMate, -6, false, false})};
    const std::string text  = chess::WritePgn(record.ToPgn());
    const auto        games = chess::ReadPgn(text);
    ASSERT_EQ(games.size(), 1u);
    const auto read = SeriesOfPgn(games.front());
    const auto made = SeriesOfNotes(record.notes, 4, {"Alpha", "Beta"}, {true, true});
    EXPECT_EQ(read, made) << text;
    ASSERT_EQ(read.size(), 2u);
    EXPECT_DOUBLE_EQ(read[1].points[1]->pawns, -0.20) << "Black's +0.20, written and read as White's -0.20";
    EXPECT_EQ(read[1].points[3]->mate, std::optional<int>(6)) << "Black mated in 6: White mates";

    // [%eval] comments are one analysis of the whole game.
    chess::PgnGame analysed   = games.front();
    analysed.moves[0].comment = "[%eval 0.25]";
    analysed.moves[1].comment = "[%eval 0.31]";
    const auto evaluation     = SeriesOfPgn(analysed);
    ASSERT_EQ(evaluation.size(), 1u);
    EXPECT_EQ(evaluation[0].name, "Evaluation");
    EXPECT_DOUBLE_EQ(evaluation[0].points[1]->pawns, 0.31);
    EXPECT_FALSE(evaluation[0].points[2]);
}

TEST(Evaluations, APgnWithScoresOpensWithItsSeriesOnTheGraph)
{
    MainView       view{Pieces()};
    MainController controller{view, ControllerFiles::None()};
    const auto     games = chess::ReadPgn(R"([White "Alpha"]
[Black "Beta"]

1. e4 {book} e5 {book} 2. Nf3 {+0.40/14 1.0s} Nc6 {-0.25/13 0.9s} 3. Bb5 {+0.55/15 1.1s} *
)");
    ASSERT_EQ(games.size(), 1u);
    controller.ShowPgnGame(games.front());
    const EvalGraph &graph = view.graph();
    ASSERT_EQ(graph.series().size(), 2u);
    EXPECT_EQ(graph.ply_count(), 5);
    EXPECT_EQ(graph.series()[0].name, "Alpha");
    EXPECT_EQ(graph.series()[1].name, "Beta");
    EXPECT_FALSE(graph.series()[0].points[0]) << "a book move has no score";
    EXPECT_DOUBLE_EQ(graph.series()[0].points[4]->pawns, 0.55);
    EXPECT_DOUBLE_EQ(graph.series()[1].points[3]->pawns, -0.25);
    EXPECT_EQ(graph.caption(), "3.Bb5  +0.55");
    controller.ShowPly(4);
    EXPECT_EQ(graph.caption(), "2...Nc6  -0.25");
}

} // namespace amphitheater::app
