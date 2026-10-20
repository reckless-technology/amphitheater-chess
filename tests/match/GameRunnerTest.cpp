// The game runner against the fake engine: games that end by the rules, by a flag, an illegal move, a crash, a stall
// or an adjudication; openings; the record and its PGN; and players that are not engines.

#include "match/GameRunner.h"
#include "chess/Notation.h"
#include "match/EnginePlayer.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace uci_host::match
{
namespace
{

using namespace std::chrono_literals;

uci::EngineConfig Fake(std::vector<std::string> arguments = {}, std::string name = "")
{
    uci::EngineConfig config;
    config.executable = UCI_HOST_FAKE_ENGINE;
    config.arguments  = std::move(arguments);
    config.name       = std::move(name);
    return config;
}

chess::TimeControl Control(const char *text)
{
    return *chess::TimeControl::Parse(text);
}

GameSettings Settings(const char *time_control = "10+0.1")
{
    GameSettings settings;
    settings.white_time_control = Control(time_control);
    settings.black_time_control = Control(time_control);
    return settings;
}

TEST(GameRunner, PlaysAGameToItsEndAndRecordsIt)
{
    EnginePlayer white(Fake({}, "Alpha"), Control("10+0.1"));
    EnginePlayer black(Fake({"--seed", "7"}, "Beta"), Control("10+0.1"));
    auto         settings = Settings();
    settings.event        = "Test Event";
    int        progress   = 0;
    GameRunner runner(white, black, settings);
    const auto record = runner.Play([&](const GameProgress &step) {
        ++progress;
        EXPECT_EQ(step.game.PlyCount(), progress);
        EXPECT_GT(step.white_clock.Remaining().count(), 0);
    });
    ASSERT_TRUE(record.game.IsOver());
    EXPECT_NE(record.game.GetOutcome().termination, chess::Termination::kAbandoned);
    EXPECT_EQ(progress, record.game.PlyCount());
    EXPECT_EQ(static_cast<int>(record.notes.size()), record.game.PlyCount());
    EXPECT_EQ(record.white_name, "Alpha");
    EXPECT_EQ(record.black_name, "Beta");

    const chess::PgnGame pgn  = record.ToPgn();
    const std::string    text = chess::WritePgn(pgn);
    EXPECT_EQ(pgn.Tag("Event"), "Test Event");
    EXPECT_EQ(pgn.Tag("White"), "Alpha");
    EXPECT_EQ(pgn.Tag("TimeControl"), "10+0.1");
    EXPECT_EQ(pgn.Tag("PlyCount"), std::to_string(record.game.PlyCount()));
    EXPECT_EQ(pgn.Tag("Date").size(), 10u);
    // The writer wraps at 80 columns, so the closing comment may be split across lines.
    std::string unwrapped = text;
    std::replace(unwrapped.begin(), unwrapped.end(), '\n', ' ');
    EXPECT_NE(unwrapped.find(record.game.GetOutcome().description), std::string::npos) << text;
    // It reads back as the same game.
    std::vector<std::string> errors;
    const auto               back = chess::ReadPgn(text, &errors);
    ASSERT_EQ(back.size(), 1u) << (errors.empty() ? "" : errors[0]);
    EXPECT_EQ(back[0].moves.size(), static_cast<std::size_t>(record.game.PlyCount()));
    EXPECT_EQ(back[0].result, record.game.GetOutcome().result);
}

TEST(GameRunner, StartsFromAnOpeningAndMarksItsMovesAsBook)
{
    EnginePlayer white(Fake(), Control("10+0.1"));
    EnginePlayer black(Fake(), Control("10+0.1"));
    auto         settings           = Settings();
    settings.opening.fen            = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    settings.opening.moves          = {"e2e4", "e7e5"};
    settings.opening.name           = "Open game";
    settings.adjudication.max_moves = 6;
    GameRunner runner(white, black, settings);
    const auto record = runner.Play();
    EXPECT_EQ(record.game.UciMoves()[0], "e2e4");
    EXPECT_TRUE(record.notes[0].is_book);
    EXPECT_TRUE(record.notes[1].is_book);
    EXPECT_FALSE(record.notes[2].is_book);
    EXPECT_EQ(record.notes[0].Comment(), "book");
    EXPECT_EQ(record.ToPgn().Tag("Opening"), "Open game");
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kAdjudication);
    EXPECT_EQ(record.game.PlyCount(), 12);
}

TEST(GameRunner, AnUnplayableOpeningAbandonsTheGame)
{
    EnginePlayer white(Fake(), Control("1"));
    EnginePlayer black(Fake(), Control("1"));
    auto         settings  = Settings();
    settings.opening.moves = {"e2e5"};
    auto record            = GameRunner(white, black, settings).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kAbandoned);
    EXPECT_NE(record.game.GetOutcome().description.find("e2e5"), std::string::npos);
    settings.opening = {"not a fen", {}, ""};
    record           = GameRunner(white, black, settings).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kAbandoned);
    EXPECT_NE(chess::WritePgn(record.ToPgn()).find("not a position"), std::string::npos);
}

TEST(GameRunner, AGameOverAfterItsOpeningAsksNoEngine)
{
    EnginePlayer white(Fake(), Control("1"));
    EnginePlayer black(Fake(), Control("1"));
    auto         settings  = Settings();
    settings.opening.moves = {"f2f3", "e7e5", "g2g4", "d8h4"};
    const auto record      = GameRunner(white, black, settings).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kCheckmate);
    EXPECT_EQ(record.notes.size(), 4u);
}

TEST(GameRunner, ASlowEngineLosesOnTime)
{
    EnginePlayer white(Fake({"--think", "2000"}), Control("0.3"));
    EnginePlayer black(Fake(), Control("0.3"));
    auto         settings = Settings("0.3");
    const auto   record   = GameRunner(white, black, settings).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kTimeForfeit);
    EXPECT_EQ(record.game.GetOutcome().result, chess::GameResult::kBlackWins);
    EXPECT_EQ(record.game.GetOutcome().description, "White loses on time");
    EXPECT_EQ(record.ToPgn().Tag("Termination"), "time forfeit");
}

TEST(GameRunner, AFlagFallsAsADrawWhenTheOpponentCannotMate)
{
    // White, with a rook, lets its flag fall; Black has a lone king.
    EnginePlayer white(Fake({"--think", "2000"}), Control("0.2"));
    EnginePlayer black(Fake(), Control("0.2"));
    auto         settings = Settings("0.2");
    settings.opening.fen  = "4k3/8/8/8/8/8/8/R3K3 w - - 0 1";
    const auto record     = GameRunner(white, black, settings).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kTimeForfeit);
    EXPECT_EQ(record.game.GetOutcome().result, chess::GameResult::kDraw);
    EXPECT_NE(record.game.GetOutcome().description.find("cannot mate"), std::string::npos);
}

TEST(GameRunner, AHangingEngineLosesOnTimeAndAStalledOneWithNoClockStalls)
{
    {
        EnginePlayer white(Fake({"--hang-at", "1"}), Control("0.3"));
        EnginePlayer black(Fake(), Control("0.3"));
        const auto   record = GameRunner(white, black, Settings("0.3")).Play();
        EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kTimeForfeit);
    }
    chess::TimeControl depth;
    depth.depth = 1;
    EnginePlayer white(Fake(), depth);
    EnginePlayer black(Fake({"--hang-at", "1"}), depth);
    GameSettings settings;
    settings.white_time_control = depth;
    settings.black_time_control = depth;
    settings.stall_timeout      = 300ms;
    const auto record           = GameRunner(white, black, settings).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kStalled);
    EXPECT_EQ(record.game.GetOutcome().result, chess::GameResult::kWhiteWins);
    EXPECT_EQ(record.game.GetOutcome().description, "Black's engine stalls");
    EXPECT_EQ(record.ToPgn().Tag("TimeControl"), "-");
}

TEST(GameRunner, AnIllegalMoveLoses)
{
    EnginePlayer white(Fake(), Control("10"));
    EnginePlayer black(Fake({"--illegal-at", "2"}), Control("10"));
    const auto   record = GameRunner(white, black, Settings("10")).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kIllegalMove);
    EXPECT_EQ(record.game.GetOutcome().result, chess::GameResult::kWhiteWins);
    EXPECT_NE(record.game.GetOutcome().description.find("Black makes an illegal move"), std::string::npos);
    EXPECT_EQ(record.game.PlyCount(), 3);
}

TEST(GameRunner, NamingNoMoveLoses)
{
    EnginePlayer white(Fake({"--empty-bestmove"}), Control("10"));
    EnginePlayer black(Fake(), Control("10"));
    const auto   record = GameRunner(white, black, Settings("10")).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kIllegalMove);
    EXPECT_EQ(record.game.GetOutcome().description, "White's engine names no move");
}

TEST(GameRunner, ACrashLosesByDisconnection)
{
    EnginePlayer white(Fake({"--crash-at", "2"}), Control("10"));
    EnginePlayer black(Fake(), Control("10"));
    const auto   record = GameRunner(white, black, Settings("10")).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kDisconnection);
    EXPECT_EQ(record.game.GetOutcome().result, chess::GameResult::kBlackWins);
    EXPECT_NE(record.game.GetOutcome().description.find("exit code 3"), std::string::npos)
        << record.game.GetOutcome().description;
    // The player starts the engine afresh for the next game.
    EnginePlayer other(Fake(), Control("10"));
    auto         settings           = Settings("10");
    settings.adjudication.max_moves = 1;
    const auto again                = GameRunner(white, other, settings).Play();
    EXPECT_NE(again.game.GetOutcome().termination, chess::Termination::kDisconnection);
}

TEST(GameRunner, AnEngineThatWillNotStartLoses)
{
    uci::EngineConfig missing;
    missing.executable = "no-such-engine-anywhere";
    EnginePlayer white(Fake(), Control("1"));
    EnginePlayer black(missing, Control("1"));
    const auto   record = GameRunner(white, black, Settings("1")).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kDisconnection);
    EXPECT_EQ(record.game.GetOutcome().result, chess::GameResult::kWhiteWins);
    EXPECT_NE(record.game.GetOutcome().description.find("failed to start"), std::string::npos);
    EXPECT_EQ(black.Name(), "no-such-engine-anywhere");
    EXPECT_TRUE(black.Warnings().empty());
}

TEST(GameRunner, ResignationAdjudication)
{
    EnginePlayer white(Fake({"--score", "1000"}), Control("10"));
    EnginePlayer black(Fake({"--score", "-1000"}), Control("10"));
    auto         settings                     = Settings("10");
    settings.adjudication.resign.is_enabled   = true;
    settings.adjudication.resign.move_count   = 3;
    settings.adjudication.resign.score        = 600;
    settings.adjudication.resign.is_two_sided = true;
    const auto record                         = GameRunner(white, black, settings).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kResignation);
    EXPECT_EQ(record.game.GetOutcome().result, chess::GameResult::kWhiteWins);
    EXPECT_EQ(record.game.GetOutcome().description, "Black resigns");
    EXPECT_EQ(record.game.PlyCount(), 6);
    EXPECT_EQ(record.ToPgn().Tag("Termination"), "adjudication");
}

TEST(GameRunner, ResignationGoesByEachEnginesOwnView)
{
    // Black's engine is winning by its own word (+10.00 for Black), White's agrees (-10.00 for White): White resigns,
    // and the comments read -10.00 throughout, from White's side.
    EnginePlayer white(Fake({"--score", "-1000"}), Control("10"));
    EnginePlayer black(Fake({"--score", "1000"}), Control("10"));
    auto         settings                     = Settings("10");
    settings.adjudication.resign.is_enabled   = true;
    settings.adjudication.resign.move_count   = 3;
    settings.adjudication.resign.score        = 600;
    settings.adjudication.resign.is_two_sided = true;
    const auto record                         = GameRunner(white, black, settings).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kResignation);
    EXPECT_EQ(record.game.GetOutcome().result, chess::GameResult::kBlackWins);
    EXPECT_EQ(record.game.GetOutcome().description, "White resigns");
    EXPECT_EQ(record.game.PlyCount(), 7) << "White's fourth losing score, after Black's third winning one";
    for (const MoveNote &note : record.notes)
    {
        EXPECT_EQ(note.Comment().substr(0, 6), "-10.00");
    }
}

TEST(GameRunner, TwoSidedResignationNeedsTheOpponentToAgree)
{
    EnginePlayer white(Fake({"--score", "0"}), Control("10"));
    EnginePlayer black(Fake({"--score", "-1000"}), Control("10"));
    auto         settings                     = Settings("10");
    settings.adjudication.resign.is_enabled   = true;
    settings.adjudication.resign.is_two_sided = true;
    settings.adjudication.max_moves           = 8;
    const auto record                         = GameRunner(white, black, settings).Play();
    EXPECT_NE(record.game.GetOutcome().termination, chess::Termination::kResignation);
}

TEST(GameRunner, DrawAdjudication)
{
    EnginePlayer white(Fake({"--score", "3"}), Control("10"));
    EnginePlayer black(Fake({"--score", "-5"}), Control("10"));
    auto         settings                  = Settings("10");
    settings.adjudication.draw.is_enabled  = true;
    settings.adjudication.draw.move_number = 6;
    settings.adjudication.draw.move_count  = 4;
    settings.adjudication.draw.score       = 10;
    const auto record                      = GameRunner(white, black, settings).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kAdjudication);
    EXPECT_EQ(record.game.GetOutcome().result, chess::GameResult::kDraw);
    EXPECT_EQ(record.game.GetOutcome().description, "Draw by adjudication");
    EXPECT_EQ(record.game.PlyCount(), 11) << "from move 6, once each side has four in a row: after White's sixth";
}

TEST(GameRunner, MateScoresInComments)
{
    EnginePlayer white(Fake({"--mate", "3"}), Control("10"));
    EnginePlayer black(Fake({"--mate", "-2"}), Control("10"));
    auto         settings           = Settings("10");
    settings.adjudication.max_moves = 1;
    const auto record               = GameRunner(white, black, settings).Play();
    ASSERT_EQ(record.notes.size(), 2u);
    EXPECT_EQ(record.notes[0].Comment().substr(0, 5), "+M3/2") << "White says it mates in 3: White mates";
    EXPECT_EQ(record.notes[1].Comment().substr(0, 5), "+M2/2") << "Black says it is mated in 2: White mates";
}

TEST(GameRunner, CommentsAreFromWhitesSideWhicheverSideMoved)
{
    // Both engines say they stand a pawn better, each from its own side: White is +1.00 at White's moves and -1.00 at
    // Black's, in the notes and in the PGN alike.
    for (const int own : {100, -100})
    {
        EnginePlayer white(Fake({"--score", std::to_string(own)}), Control("10"));
        EnginePlayer black(Fake({"--score", std::to_string(own)}), Control("10"));
        auto         settings           = Settings("10");
        settings.adjudication.max_moves = 3;
        const auto record               = GameRunner(white, black, settings).Play();
        ASSERT_EQ(record.notes.size(), 6u);
        const std::string white_text = own > 0 ? "+1.00/2" : "-1.00/2";
        const std::string black_text = own > 0 ? "-1.00/2" : "+1.00/2";
        for (std::size_t ply = 0; ply < record.notes.size(); ++ply)
        {
            const MoveNote &note = record.notes[ply];
            EXPECT_EQ(note.mover, ply % 2 == 0 ? chess::kWhite : chess::kBlack);
            ASSERT_TRUE(note.score);
            EXPECT_EQ(note.score->value, own) << "the note keeps the engine's own word, as adjudication judges it";
            EXPECT_EQ(note.WhiteScore()->value, ply % 2 == 0 ? own : -own);
            EXPECT_EQ(note.Comment().substr(0, 7), ply % 2 == 0 ? white_text : black_text) << "ply " << ply + 1;
        }
        const std::string pgn = chess::WritePgn(record.ToPgn());
        EXPECT_NE(pgn.find("{" + white_text), std::string::npos) << pgn;
        EXPECT_NE(pgn.find("{" + black_text), std::string::npos) << pgn;
    }
}

TEST(GameRunner, DifferentTimeControlsAreRecordedForEachSide)
{
    EnginePlayer white(Fake(), Control("10"));
    EnginePlayer black(Fake(), Control("5+0.1"));
    auto         settings           = Settings("10");
    settings.black_time_control     = Control("5+0.1");
    settings.adjudication.max_moves = 1;
    const auto pgn                  = GameRunner(white, black, settings).Play().ToPgn();
    EXPECT_EQ(pgn.Tag("WhiteTimeControl"), "10");
    EXPECT_EQ(pgn.Tag("BlackTimeControl"), "5+0.1");
    EXPECT_EQ(pgn.Tag("TimeControl"), "");
}

TEST(GameRunner, CallbackPlayersAndCancellation)
{
    // A "person" who always plays the first legal move, and another who cancels the game after its third move.
    std::atomic<bool> cancel = false;
    int               moves  = 0;
    CallbackPlayer    white("First Legal", [](const MoveRequest &request) {
        MoveReply reply;
        reply.move    = request.game.CurrentPosition().GenerateLegalMoves()[0].ToUci();
        reply.elapsed = 1ms;
        return reply;
    });
    CallbackPlayer    black("Canceller", [&](const MoveRequest &request) {
        MoveReply reply;
        reply.move = request.game.CurrentPosition().GenerateLegalMoves()[0].ToUci();
        if (++moves == 3)
        {
            cancel = true;
        }
        return reply;
    });
    GameSettings      settings;
    const auto        record = GameRunner(white, black, settings).Play({}, &cancel);
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kAbandoned);
    EXPECT_EQ(record.game.GetOutcome().result, chess::GameResult::kNone);
    EXPECT_EQ(record.game.PlyCount(), 6);
    EXPECT_EQ(record.white_name, "First Legal");
    EXPECT_EQ(chess::WritePgn(record.ToPgn()).find("[Result \"*\"]") != std::string::npos, true);
}

TEST(GameRunner, AWaitGivenUpWhenTheGameIsCancelledAbandonsItRatherThanLosingIt)
{
    // A person at the GUI stops the game while it is their move: the chooser gives up its wait and answers with no
    // move. The game is abandoned, not lost on time.
    std::atomic<bool> cancel = false;
    CallbackPlayer    white("Mover", [](const MoveRequest &request) {
        MoveReply reply;
        reply.move = request.game.CurrentPosition().GenerateLegalMoves()[0].ToUci();
        return reply;
    });
    CallbackPlayer    person("Person", [&](const MoveRequest &) {
        cancel = true;
        MoveReply reply;
        reply.status  = MoveReply::Status::kTimeout;
        reply.elapsed = 1ms;
        return reply;
    });
    const auto        record = GameRunner(white, person, Settings("60")).Play({}, &cancel);
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kAbandoned);
    EXPECT_EQ(record.game.GetOutcome().result, chess::GameResult::kNone);
    EXPECT_EQ(record.game.PlyCount(), 1);
}

TEST(GameRunner, CallbackTimeoutsAndDisconnections)
{
    CallbackPlayer timeout("Slow", [](const MoveRequest &) {
        MoveReply reply;
        reply.status  = MoveReply::Status::kTimeout;
        reply.elapsed = 5000ms;
        return reply;
    });
    CallbackPlayer gone("Gone", [](const MoveRequest &) {
        MoveReply reply;
        reply.status = MoveReply::Status::kDisconnected;
        return reply;
    });
    auto           settings = Settings("1");
    auto           record   = GameRunner(timeout, gone, settings).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kTimeForfeit);
    record = GameRunner(gone, timeout, settings).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kDisconnection);
    EXPECT_EQ(record.game.GetOutcome().description, "White's engine disconnects");
}

TEST(EnginePlayer, GoCarriesTheClocksAndTheLimits)
{
    chess::Game  game;
    chess::Clock white(Control("40/60+1"));
    chess::Clock black(Control("40/60+1"));
    white.Spend(2000ms);
    game.PlayUci("e2e4");
    chess::TimeControl limits = Control("40/60+1");
    limits.depth              = 9;
    limits.nodes              = 5000;
    limits.move_time          = 700ms;
    const auto go             = EnginePlayer::GoFor({game, white, black, std::chrono::steady_clock::now()}, limits);
    EXPECT_EQ(go.white_time, 59000ms);
    EXPECT_EQ(go.black_time, 60000ms);
    EXPECT_EQ(go.white_increment, 1000ms);
    EXPECT_EQ(go.black_increment, 1000ms);
    EXPECT_EQ(go.moves_to_go, 40);
    EXPECT_EQ(go.depth, 9);
    EXPECT_EQ(go.nodes, 5000);
    EXPECT_EQ(go.move_time, 700ms);
    // No sessions: no clock in "go".
    chess::Clock none;
    const auto   bare = EnginePlayer::GoFor({game, none, none, std::chrono::steady_clock::now()}, {});
    EXPECT_FALSE(bare.white_time);
    EXPECT_FALSE(bare.moves_to_go);
}

TEST(EnginePlayer, LogsTheEnginesLinesAndReportsAMissingEngine)
{
    // Declared before the player, whose engine still logs its quit as it goes.
    int          lines = 0;
    EnginePlayer player(Fake({"--name", "Logged"}), Control("1"));
    player.SetLogger([&](uci::LineDirection, std::string_view) { ++lines; });
    chess::Game game;
    ASSERT_TRUE(player.StartGame(game));
    EXPECT_EQ(player.Name(), "Logged");
    EXPECT_GT(lines, 5);
    chess::Clock clock(Control("1"));
    const auto   reply = player.RequestMove({game, clock, clock, std::chrono::steady_clock::now() + 5s});
    EXPECT_EQ(reply.status, MoveReply::Status::kMove);

    EnginePlayer never(Fake(), Control("1"));
    const auto   missing = never.RequestMove({game, clock, clock, std::chrono::steady_clock::now() + 5s});
    EXPECT_EQ(missing.status, MoveReply::Status::kDisconnected);
}

TEST(MoveNote, CommentFormats)
{
    MoveNote note;
    note.elapsed = 1200ms;
    EXPECT_EQ(note.Comment(), "1.2s");
    note.score = uci::Score{uci::Score::Kind::kCentipawns, 35, false, false};
    EXPECT_EQ(note.Comment(), "+0.35 1.2s");
    note.depth = 18;
    EXPECT_EQ(note.Comment(), "+0.35/18 1.2s");
    note.score = uci::Score{uci::Score::Kind::kMate, -3, false, false};
    EXPECT_EQ(note.Comment(), "-M3/18 1.2s");

    // Black's move: its engine's own score turned to White's side.
    note.mover = chess::kBlack;
    EXPECT_EQ(note.Comment(), "+M3/18 1.2s") << "Black mated in 3: White mates";
    note.score = uci::Score{uci::Score::Kind::kCentipawns, 35, false, false};
    EXPECT_EQ(note.Comment(), "-0.35/18 1.2s");
    note.score = uci::Score{uci::Score::Kind::kMate, 4, false, false};
    EXPECT_EQ(note.Comment(), "-M4/18 1.2s") << "Black mates in 4";
}

TEST(Adjudicator, MateScoresOutweighMaterial)
{
    EXPECT_EQ(Adjudicator::Centipawns({uci::Score::Kind::kCentipawns, -250, false, false}), -250);
    EXPECT_GT(Adjudicator::Centipawns({uci::Score::Kind::kMate, 10, false, false}), 30000);
    EXPECT_GT(Adjudicator::Centipawns({uci::Score::Kind::kMate, 1, false, false}),
              Adjudicator::Centipawns({uci::Score::Kind::kMate, 5, false, false}));
    EXPECT_LT(Adjudicator::Centipawns({uci::Score::Kind::kMate, -1, false, false}), -30000);
}

TEST(Adjudicator, AMoveWithNoScoreBreaksARun)
{
    AdjudicationSettings settings;
    settings.resign.is_enabled = true;
    settings.resign.move_count = 2;
    Adjudicator      adjudicator(settings);
    chess::Game      game;
    const uci::Score losing{uci::Score::Kind::kCentipawns, -900, false, false};
    EXPECT_FALSE(adjudicator.Judge(game, chess::kWhite, losing));
    EXPECT_FALSE(adjudicator.Judge(game, chess::kWhite, std::nullopt));
    EXPECT_FALSE(adjudicator.Judge(game, chess::kWhite, losing));
    const auto outcome = adjudicator.Judge(game, chess::kWhite, losing);
    ASSERT_TRUE(outcome);
    EXPECT_EQ(outcome->description, "White resigns");
}

TEST(Adjudicator, JudgesEachSideByItsOwnScore)
{
    // Black's engine reports -9.00 from its own side - Black is losing - which from White's side would read +9.00. The
    // adjudicator takes the engine's own word: Black resigns, White wins.
    AdjudicationSettings settings;
    settings.resign.is_enabled = true;
    settings.resign.move_count = 2;
    settings.resign.score      = 600;
    Adjudicator      adjudicator(settings);
    chess::Game      game;
    const uci::Score losing{uci::Score::Kind::kCentipawns, -900, false, false};
    const uci::Score winning{uci::Score::Kind::kCentipawns, 900, false, false};
    EXPECT_FALSE(adjudicator.Judge(game, chess::kWhite, winning));
    EXPECT_FALSE(adjudicator.Judge(game, chess::kBlack, losing));
    EXPECT_FALSE(adjudicator.Judge(game, chess::kWhite, winning)) << "White is winning: White never resigns";
    const auto outcome = adjudicator.Judge(game, chess::kBlack, losing);
    ASSERT_TRUE(outcome);
    EXPECT_EQ(outcome->result, chess::GameResult::kWhiteWins);
    EXPECT_EQ(outcome->description, "Black resigns");

    // A mate against Black, in Black's own words (mated in 3), counts as losing too.
    Adjudicator      mated(settings);
    const uci::Score mated_in_three{uci::Score::Kind::kMate, -3, false, false};
    EXPECT_FALSE(mated.Judge(game, chess::kBlack, mated_in_three));
    const auto resigned = mated.Judge(game, chess::kBlack, mated_in_three);
    ASSERT_TRUE(resigned);
    EXPECT_EQ(resigned->description, "Black resigns");
}

} // namespace
} // namespace uci_host::match
