// Pondering and opening books in the game runner, against the fake engine: a ponder hit, a miss, an engine with no
// ponder move, one that will not stop, the clocks never charged for pondering, pondering against a person, games that
// end while an engine ponders; each side's own Polyglot book and its depth; openings walked from a book.

#include "chess/Notation.h"
#include "match/EnginePlayer.h"
#include "match/GameRunner.h"
#include "match/Openings.h"
#include "match/Tournament.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <thread>

namespace uci_host::match
{
namespace
{

using namespace std::chrono_literals;

uci::EngineConfig Fake(std::vector<std::string> arguments, bool does_ponder = false)
{
    uci::EngineConfig config;
    config.executable  = UCI_HOST_FAKE_ENGINE;
    config.arguments   = std::move(arguments);
    config.does_ponder = does_ponder;
    return config;
}

chess::TimeControl Control(const char *text)
{
    return *chess::TimeControl::Parse(text);
}

GameSettings Settings(const char *time_control, int max_moves)
{
    GameSettings settings;
    settings.white_time_control     = Control(time_control);
    settings.black_time_control     = Control(time_control);
    settings.adjudication.max_moves = max_moves;
    return settings;
}

// The commands a player sent its engine, in order.
struct SentLines
{
    std::vector<std::string> lines;

    void Attach(EnginePlayer &player)
    {
        player.SetLogger([this](uci::LineDirection direction, std::string_view line) {
            if (direction == uci::LineDirection::kToEngine)
            {
                lines.emplace_back(line);
            }
        });
    }

    int Exactly(const std::string &line) const
    {
        return static_cast<int>(std::count(lines.begin(), lines.end(), line));
    }

    int Count(const std::string &prefix) const
    {
        return static_cast<int>(std::count_if(lines.begin(), lines.end(),
                                              [&](const std::string &line) { return line.rfind(prefix, 0) == 0; }));
    }
};

TEST(Pondering, AHitTurnsThePonderSearchIntoTheMove)
{
    // Both fake engines choose alike with one seed, so the white engine always guesses Black's reply.
    EnginePlayer white(Fake({"--ponder-move", "--seed", "5"}, true), Control("10"));
    EnginePlayer black(Fake({"--seed", "5"}), Control("10"));
    SentLines    sent;
    sent.Attach(white);
    const auto record = GameRunner(white, black, Settings("10", 6)).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kAdjudication);
    EXPECT_EQ(sent.Count("setoption name Ponder value true"), 1);
    EXPECT_GE(sent.Count("go ponder"), 5);
    EXPECT_GE(sent.Count("ponderhit"), 5);
    EXPECT_LE(sent.Count("stop"), 1) << "only the game's end stops a ponder search";
    EXPECT_FALSE(white.IsPondering());
}

TEST(Pondering, AMissStopsThePonderSearchAndSearchesAfresh)
{
    EnginePlayer white(Fake({"--ponder-move", "--seed", "5"}, true), Control("10"));
    EnginePlayer black(Fake({"--seed", "6"}), Control("10"));
    SentLines    sent;
    sent.Attach(white);
    const auto record = GameRunner(white, black, Settings("10", 8)).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kAdjudication);
    EXPECT_GE(sent.Count("stop"), 1);
    // Every stop of a ponder search is followed by the real position and search.
    for (std::size_t index = 0; index + 2 < sent.lines.size(); ++index)
    {
        if (sent.lines[index] == "stop" && sent.lines[index + 1].rfind("position", 0) == 0)
        {
            EXPECT_EQ(sent.lines[index + 2].rfind("go wtime", 0), 0u) << sent.lines[index + 2];
        }
    }
}

TEST(Pondering, AnEngineThatNamesNoPonderMoveDoesNotPonder)
{
    EnginePlayer white(Fake({"--seed", "5"}, true), Control("10"));
    EnginePlayer black(Fake({"--seed", "5"}), Control("10"));
    SentLines    sent;
    sent.Attach(white);
    GameRunner(white, black, Settings("10", 4)).Play();
    EXPECT_EQ(sent.Count("go ponder"), 0);
    EXPECT_EQ(sent.Count("setoption name Ponder value true"), 1);
}

TEST(Pondering, PonderingOffSendsNoPonderSearch)
{
    EnginePlayer white(Fake({"--ponder-move", "--seed", "5"}), Control("10"));
    EnginePlayer black(Fake({"--seed", "5"}), Control("10"));
    SentLines    sent;
    sent.Attach(white);
    GameRunner(white, black, Settings("10", 4)).Play();
    EXPECT_EQ(sent.Count("go ponder"), 0);
    EXPECT_EQ(sent.Count("setoption name Ponder"), 0);
}

TEST(Pondering, AnEngineThatWillNotStopLosesOnTime)
{
    EnginePlayer white(Fake({"--ponder-move", "--ignore-stop", "--seed", "5"}, true), Control("1"));
    EnginePlayer black(Fake({"--seed", "6"}), Control("1"));
    const auto   start  = std::chrono::steady_clock::now();
    const auto   record = GameRunner(white, black, Settings("1", 40)).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kTimeForfeit);
    EXPECT_EQ(record.game.GetOutcome().result, chess::GameResult::kBlackWins);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 15s);
    // The engine was written off; the next game starts it afresh and plays.
    EnginePlayer other(Fake({"--seed", "6"}), Control("10"));
    SentLines    sent;
    sent.Attach(white);
    const auto again = GameRunner(white, other, Settings("10", 1)).Play();
    EXPECT_EQ(again.game.GetOutcome().termination, chess::Termination::kAdjudication);
    EXPECT_EQ(sent.Exactly("uci"), 1) << "a fresh engine";
}

TEST(Pondering, PonderingIsNeverChargedToTheClock)
{
    // White guesses right every time and thinks 150 ms after each ponderhit; Black thinks 400 ms a move, all of it
    // time White ponders. White's charged time a move must be its own 150 ms, not 550.
    EnginePlayer white(Fake({"--ponder-move", "--seed", "5", "--think", "150"}, true), Control("60"));
    EnginePlayer black(Fake({"--seed", "5", "--think", "400"}), Control("60"));
    std::vector<chess::Milliseconds> white_remaining;
    const auto record = GameRunner(white, black, Settings("60", 4)).Play([&](const GameProgress &step) {
        if (step.game.CurrentPosition().SideToMove() == chess::kBlack)
        {
            white_remaining.push_back(step.white_clock.Remaining());
        }
    });
    ASSERT_EQ(record.game.PlyCount(), 8);
    chess::Milliseconds white_total{0};
    for (int ply = 0; ply < 8; ply += 2)
    {
        EXPECT_GE(record.notes[ply].elapsed.count(), 130) << "ply " << ply;
        EXPECT_LT(record.notes[ply].elapsed.count(), 350) << "ply " << ply << ": the pondering was charged";
        EXPECT_GE(record.notes[ply + 1].elapsed.count(), 380) << "ply " << ply + 1;
        white_total += record.notes[ply].elapsed;
    }
    ASSERT_EQ(white_remaining.size(), 4u);
    EXPECT_EQ(white_remaining.back(), chess::Milliseconds{60000} - white_total);
}

TEST(Pondering, AnEnginePondersWhileAPersonThinks)
{
    EnginePlayer   white(Fake({"--ponder-move", "--seed", "5"}, true), Control("60"));
    int            moves_while_pondering = 0;
    CallbackPlayer person("Person", [&](const MoveRequest &request) {
        moves_while_pondering += white.IsPondering() ? 1 : 0;
        std::this_thread::sleep_for(100ms);
        MoveReply  reply;
        const auto legal = request.game.CurrentPosition().GenerateLegalMoves();
        reply.move       = legal[legal.size() - 1].ToUci();
        reply.elapsed    = 100ms;
        return reply;
    });
    SentLines      sent;
    sent.Attach(white);
    const auto record = GameRunner(white, person, Settings("60", 5)).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kAdjudication);
    EXPECT_EQ(moves_while_pondering, 5) << "the engine pondered through every one of the person's moves";
    EXPECT_EQ(sent.Count("go ponder"), 5);
    EXPECT_EQ(sent.Count("ponderhit") + sent.Count("stop"), 5);
}

TEST(Pondering, AGameThatEndsWhileTheEnginePondersStopsIt)
{
    // The game ends on Black's first move (the length limit) while White ponders.
    EnginePlayer white(Fake({"--ponder-move", "--seed", "5"}, true), Control("10"));
    EnginePlayer black(Fake({"--seed", "5"}), Control("10"));
    SentLines    sent;
    sent.Attach(white);
    auto record = GameRunner(white, black, Settings("10", 1)).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kAdjudication);
    EXPECT_EQ(sent.Count("go ponder"), 1);
    EXPECT_EQ(sent.Count("stop"), 1);
    EXPECT_FALSE(white.IsPondering());
    // The engine answered its stop, so the next game goes on with it, and its stopped answer muddles nothing.
    record = GameRunner(white, black, Settings("10", 2)).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kAdjudication);
    EXPECT_EQ(record.game.PlyCount(), 4);
    EXPECT_EQ(sent.Exactly("uci"), 1) << "the same engine";
}

TEST(Pondering, AGameThatEndsOnTheOpponentsFlagStopsThePonderSearch)
{
    EnginePlayer   white(Fake({"--ponder-move", "--seed", "5"}, true), Control("10"));
    CallbackPlayer flagging("Flag", [](const MoveRequest &) {
        MoveReply reply;
        reply.status  = MoveReply::Status::kTimeout;
        reply.elapsed = 11000ms;
        return reply;
    });
    SentLines      sent;
    sent.Attach(white);
    const auto record = GameRunner(white, flagging, Settings("10", 0)).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kTimeForfeit);
    EXPECT_EQ(record.game.GetOutcome().result, chess::GameResult::kWhiteWins);
    EXPECT_EQ(sent.Count("stop"), 1);
    EXPECT_FALSE(white.IsPondering());
}

TEST(Pondering, AnEngineThatWillNotStopAtTheEndIsStartedAfresh)
{
    EnginePlayer white(Fake({"--ponder-move", "--ignore-stop", "--seed", "5"}, true), Control("10"));
    EnginePlayer black(Fake({"--seed", "5"}), Control("10"));
    SentLines    sent;
    sent.Attach(white);
    GameRunner(white, black, Settings("10", 1)).Play();
    const auto record = GameRunner(white, black, Settings("10", 1)).Play();
    EXPECT_EQ(record.game.GetOutcome().termination, chess::Termination::kAdjudication);
    EXPECT_EQ(sent.Exactly("uci"), 2) << "written off after the first game, started again for the second";
}

// A book of one line, each move of weight 1: 1. e4 c5 2. Nf3 d6.
std::shared_ptr<const chess::PolyglotBook> LineBook()
{
    std::vector<chess::PolyglotEntry> entries;
    chess::Position                   position = chess::Position::Start();
    for (const char *text : {"e2e4", "c7c5", "g1f3", "d7d6"})
    {
        const auto move = *chess::ParseUciMove(position, text);
        entries.push_back({chess::PolyglotKey(position), chess::PolyglotBook::EncodeMove(move), 1, 0});
        position = position.MakeMove(move);
    }
    return std::make_shared<const chess::PolyglotBook>(
        *chess::PolyglotBook::FromBytes(chess::PolyglotBook::Encode(entries)));
}

TEST(Books, EachSidePlaysItsBookThenItsEngine)
{
    EnginePlayer white(Fake({"--seed", "3"}), Control("10"));
    EnginePlayer black(Fake({"--seed", "4"}), Control("10"));
    auto         settings    = Settings("10", 4);
    settings.white_book.book = LineBook();
    settings.black_book.book = LineBook();
    SentLines sent;
    sent.Attach(white);
    const auto record = GameRunner(white, black, settings).Play();
    const auto moves  = record.game.UciMoves();
    ASSERT_GE(moves.size(), 5u);
    EXPECT_EQ(std::vector<std::string>(moves.begin(), moves.begin() + 4),
              (std::vector<std::string>{"e2e4", "c7c5", "g1f3", "d7d6"}));
    for (int ply = 0; ply < 4; ++ply)
    {
        EXPECT_TRUE(record.notes[ply].is_book) << ply;
    }
    EXPECT_FALSE(record.notes[4].is_book);
    EXPECT_NE(chess::WritePgn(record.ToPgn()).find("1. e4 {book} 1... c5 {book}"), std::string::npos);
    // Out of the book, the engine is told the whole game, the book's moves among it.
    EXPECT_EQ(sent.lines[sent.lines.size() - 2].find("position startpos moves e2e4 c7c5 g1f3 d7d6"), 0u);
}

TEST(Books, ABooksDepthLimitsIt)
{
    EnginePlayer white(Fake({"--seed", "3"}), Control("10"));
    EnginePlayer black(Fake({"--seed", "4"}), Control("10"));
    auto         settings         = Settings("10", 3);
    settings.white_book.book      = LineBook();
    settings.white_book.depth     = 1;
    settings.black_book.book      = LineBook();
    settings.black_book.depth     = 2;
    settings.white_book.selection = chess::BookSelection::kBestWeight;
    const auto record             = GameRunner(white, black, settings).Play();
    EXPECT_TRUE(record.notes[0].is_book);
    EXPECT_TRUE(record.notes[1].is_book);
    EXPECT_FALSE(record.notes[2].is_book) << "White's depth of 1 is used up";
    EXPECT_FALSE(record.notes[3].is_book) << "out of Black's depth of 2";
}

TEST(Books, OpeningsWalkedFromABook)
{
    const auto book = LineBook();
    const auto line = PolyglotOpening(*book, 8, 7);
    EXPECT_EQ(line.moves, (std::vector<std::string>{"e2e4", "c7c5", "g1f3", "d7d6"})) << "to the book's end";
    EXPECT_TRUE(line.fen.empty());
    EXPECT_EQ(PolyglotOpening(*book, 3, 7).moves.size(), 3u);
    EXPECT_EQ(PolyglotOpening(*book, 3, 7).moves, PolyglotOpening(*book, 3, 7).moves);

    TournamentSettings settings;
    for (const char *name : {"A", "B"})
    {
        TournamentEngine engine;
        engine.config      = Fake({});
        engine.config.name = name;
        settings.engines.push_back(engine);
    }
    settings.time_control                 = Control("10");
    settings.schedule.games_per_encounter = 2;
    settings.schedule.is_repeat           = true;
    settings.adjudication.max_moves       = 3;
    settings.opening_book                 = book;
    settings.opening_book_plies           = 2;
    Tournament tournament(settings);
    ASSERT_TRUE(tournament.Start());
    std::vector<std::vector<std::string>> openings;
    while (auto event = tournament.Events().PopFor(30s))
    {
        if (event->kind == TournamentEvent::Kind::kGameFinished)
        {
            const auto moves = event->record->game.UciMoves();
            openings.emplace_back(moves.begin(), moves.begin() + 2);
            EXPECT_TRUE(event->record->notes[0].is_book);
        }
    }
    ASSERT_EQ(openings.size(), 2u);
    EXPECT_EQ(openings[0], openings[1]) << "both games of the pair play the same line";
    EXPECT_EQ(openings[0], (std::vector<std::string>{"e2e4", "c7c5"}));
}

TEST(Books, ATournamentEnginesOwnBook)
{
    TournamentSettings settings;
    for (const char *name : {"Booked", "Plain"})
    {
        TournamentEngine engine;
        engine.config      = Fake({});
        engine.config.name = name;
        settings.engines.push_back(engine);
    }
    settings.engines[0].book        = LineBook();
    settings.engines[0].book_depth  = 10;
    settings.time_control           = Control("10");
    settings.adjudication.max_moves = 3;
    Tournament tournament(settings);
    ASSERT_TRUE(tournament.Start());
    while (auto event = tournament.Events().PopFor(30s))
    {
        if (event->kind == TournamentEvent::Kind::kGameFinished)
        {
            EXPECT_EQ(event->record->game.UciMoves()[0], "e2e4");
            EXPECT_TRUE(event->record->notes[0].is_book);
            EXPECT_FALSE(event->record->notes[1].is_book) << "Black has no book";
        }
    }
}

} // namespace
} // namespace uci_host::match
