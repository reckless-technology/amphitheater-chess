// Tournaments: the circle method's schedule and its colour balance, openings from EPD and PGN, the statistics (Elo,
// standings with Sonneborn-Berger, the crosstable), and whole round robins between fake engines run in parallel.

#include "match/Tournament.h"
#include "TestSupport.h"
#include "match/Openings.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace uci_host::match
{
namespace
{

using namespace std::chrono_literals;

TEST(Schedule, CirclePairingsMeetEveryOpponentOnceAndBalanceColours)
{
    for (int engines = 2; engines <= 9; ++engines)
    {
        const auto                    rounds = CirclePairings(engines);
        std::set<std::pair<int, int>> met;
        std::vector<int>              whites(engines), blacks(engines);
        EXPECT_EQ(static_cast<int>(rounds.size()), engines % 2 == 0 ? engines - 1 : engines);
        for (const auto &round : rounds)
        {
            std::set<int> playing;
            for (const auto &[white, black] : round)
            {
                EXPECT_TRUE(playing.insert(white).second) << engines << " engines: " << white << " plays twice";
                EXPECT_TRUE(playing.insert(black).second);
                EXPECT_TRUE(met.insert({std::min(white, black), std::max(white, black)}).second)
                    << engines << " engines: " << white << " meets " << black << " twice";
                ++whites[white];
                ++blacks[black];
            }
            EXPECT_GE(static_cast<int>(playing.size()), engines - 1);
        }
        EXPECT_EQ(static_cast<int>(met.size()), engines * (engines - 1) / 2);
        for (int engine = 0; engine < engines; ++engine)
        {
            EXPECT_LE(std::abs(whites[engine] - blacks[engine]), 1) << engines << " engines, engine " << engine;
        }
    }
    EXPECT_TRUE(CirclePairings(1).empty());
}

TEST(Schedule, EncountersAlternateColoursAndDoubleReversesThem)
{
    ScheduleSettings settings;
    settings.engines             = 2;
    settings.games_per_encounter = 4;
    auto games                   = MakeSchedule(settings);
    ASSERT_EQ(games.size(), 4u);
    EXPECT_EQ(games[0].white, 0);
    EXPECT_EQ(games[1].white, 1);
    EXPECT_EQ(games[2].white, 0);
    EXPECT_EQ(games[3].number, 4);
    // A match of single-game rounds alternates by round.
    settings.games_per_encounter = 1;
    settings.rounds              = 4;
    games                        = MakeSchedule(settings);
    ASSERT_EQ(games.size(), 4u);
    EXPECT_EQ(games[0].white, 0);
    EXPECT_EQ(games[1].white, 1);
    EXPECT_EQ(games[3].round, 4);
    // A double round robin of three: each pair twice, each engine white and black once against each opponent.
    settings.engines   = 3;
    settings.rounds    = 1;
    settings.is_double = true;
    games              = MakeSchedule(settings);
    ASSERT_EQ(games.size(), 6u);
    std::map<std::pair<int, int>, int> colours;
    for (const auto &game : games)
    {
        ++colours[{game.white, game.black}];
    }
    for (const auto &[pairing, count] : colours)
    {
        EXPECT_EQ(count, 1) << pairing.first << " white against " << pairing.second;
    }
}

TEST(Schedule, OpeningsAdvanceByGameOrByPairWithRepeat)
{
    ScheduleSettings settings;
    settings.engines             = 2;
    settings.games_per_encounter = 6;
    auto games                   = MakeSchedule(settings);
    EXPECT_EQ(games[3].opening, 3);
    settings.is_repeat = true;
    games              = MakeSchedule(settings);
    EXPECT_EQ(games[0].opening, 0);
    EXPECT_EQ(games[1].opening, 0);
    EXPECT_EQ(games[2].opening, 1);
    EXPECT_EQ(games[1].white, games[0].black) << "the repeat swaps colours";
}

TEST(Statistics, EloFromScores)
{
    ScoreLine even{10, 20, 10};
    auto      estimate = EstimateElo(even);
    ASSERT_TRUE(estimate.difference);
    EXPECT_EQ(*estimate.difference, 0.0);
    EXPECT_FALSE(std::signbit(*estimate.difference));
    EXPECT_DOUBLE_EQ(estimate.likelihood_of_superiority, 0.5);
    EXPECT_DOUBLE_EQ(estimate.draw_ratio, 0.5);
    ASSERT_TRUE(estimate.error_margin);

    ScoreLine strong{60, 20, 20};
    estimate = EstimateElo(strong);
    EXPECT_NEAR(*estimate.difference, 147.19, 0.01);
    // The trinomial variance of one game's score, independently: mean 0.7.
    const double variance  = (60 * 0.09 + 20 * 0.04 + 20 * 0.49) / 100.0;
    const double deviation = std::sqrt(variance / 100.0);
    const double margin =
        (EloFromFraction(0.7 + 1.959963984540054 * deviation) - EloFromFraction(0.7 - 1.959963984540054 * deviation)) /
        2.0;
    EXPECT_NEAR(*estimate.error_margin, margin, 1e-9);
    EXPECT_GT(estimate.likelihood_of_superiority, 0.999);

    EXPECT_FALSE(EstimateElo({5, 0, 0}).difference);
    EXPECT_FALSE(EstimateElo({0, 0, 5}).difference);
    EXPECT_FALSE(EstimateElo({}).difference);
    EXPECT_FALSE(EstimateElo({2, 0, 1}).error_margin) << "the interval reaches past certainty";
}

TEST(Statistics, StandingsWithSonnebornBergerAndTheCrosstable)
{
    // A beats B twice, B beats C twice, C and A draw twice; D loses everything.
    const std::vector<std::string> names{"A", "B", "C", "D"};
    std::vector<GameResultEntry>   results;
    const auto add = [&](int white, int black, chess::GameResult result) { results.push_back({white, black, result}); };
    add(0, 1, chess::GameResult::kWhiteWins);
    add(1, 0, chess::GameResult::kBlackWins);
    add(1, 2, chess::GameResult::kWhiteWins);
    add(2, 1, chess::GameResult::kBlackWins);
    add(2, 0, chess::GameResult::kDraw);
    add(0, 2, chess::GameResult::kDraw);
    for (int engine = 0; engine < 3; ++engine)
    {
        add(engine, 3, chess::GameResult::kWhiteWins);
        add(3, engine, chess::GameResult::kBlackWins);
    }
    add(3, 0, chess::GameResult::kNone); // An abandoned game counts for no one.
    const auto standings = ComputeStandings(names, results);
    ASSERT_EQ(standings.size(), 4u);
    EXPECT_EQ(standings[0].name, "A");
    EXPECT_DOUBLE_EQ(standings[0].score.Points(), 5.0);
    EXPECT_EQ(standings[0].score.Games(), 6);
    EXPECT_EQ(standings[1].name, "B");
    EXPECT_DOUBLE_EQ(standings[1].score.Points(), 4.0);
    EXPECT_EQ(standings[2].name, "C");
    EXPECT_DOUBLE_EQ(standings[2].score.Points(), 3.0);
    EXPECT_EQ(standings[3].name, "D");
    EXPECT_EQ(standings[3].rank, 4);
    // A: 2 x B's 4 + 1 x C's 3 + 2 x D's 0 = 11.
    EXPECT_DOUBLE_EQ(standings[0].sonneborn_berger, 11.0);
    // B: 2 x C's 3 = 6; C: 1 x A's 5 = 5.
    EXPECT_DOUBLE_EQ(standings[1].sonneborn_berger, 6.0);
    EXPECT_DOUBLE_EQ(standings[2].sonneborn_berger, 5.0);

    const Crosstable table = Crosstable::Build(4, results);
    EXPECT_EQ(table.cells[0][1].wins, 2);
    EXPECT_DOUBLE_EQ(table.cells[0][2].Points(), 1.0);
    EXPECT_EQ(table.cells[3][0].losses, 2);
    const std::string crosstable = FormatCrosstable(names, standings, table);
    EXPECT_NE(crosstable.find("2/2"), std::string::npos) << crosstable;
    EXPECT_NE(crosstable.find("1/2"), std::string::npos);
    EXPECT_NE(crosstable.find("0/2"), std::string::npos);
    const std::string table_text = FormatStandings(standings);
    EXPECT_NE(table_text.find("83.3%"), std::string::npos) << table_text;
    EXPECT_NE(table_text.find("11.00"), std::string::npos);
}

TEST(Statistics, TiesShareARankAndSortByName)
{
    const std::vector<std::string> names{"Zed", "Amy"};
    const auto                     standings = ComputeStandings(names, {{0, 1, chess::GameResult::kDraw}});
    EXPECT_EQ(standings[0].name, "Amy");
    EXPECT_EQ(standings[0].rank, 1);
    EXPECT_EQ(standings[1].rank, 1);
    const auto crosstable = FormatCrosstable(names, ComputeStandings(names, {}), Crosstable::Build(2, {}));
    EXPECT_NE(crosstable.find('-'), std::string::npos);
}

TEST(Statistics, MatchScoreText)
{
    const auto text = FormatMatchScore("A", "B", {3, 5, 2});
    EXPECT_NE(text.find("Score of A vs B: 3 - 2 - 5  [0.550] 10"), std::string::npos) << text;
    EXPECT_NE(text.find("Elo difference: 34.9 +/- "), std::string::npos) << text;
    EXPECT_NE(text.find("DrawRatio: 50.0 %"), std::string::npos);
    EXPECT_NE(FormatMatchScore("A", "B", {2, 0, 0}).find("Elo difference: inf"), std::string::npos);
    EXPECT_NE(FormatMatchScore("A", "B", {0, 0, 2}).find("Elo difference: -inf"), std::string::npos);
    EXPECT_NE(FormatMatchScore("A", "B", {}).find("Elo difference: nan"), std::string::npos);
    EXPECT_NE(FormatMatchScore("A", "B", {2, 0, 1}).find("+/- nan"), std::string::npos);
}

TEST(Openings, ReadsEpdLines)
{
    auto opening = ParseEpdLine("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 hmvc 0; fmvn 1; id \"King's "
                                "pawn\";");
    ASSERT_TRUE(opening) << opening.ErrorMessage();
    EXPECT_EQ(opening->fen, "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    EXPECT_EQ(opening->name, "King's pawn");
    EXPECT_TRUE(opening->moves.empty());
    opening = ParseEpdLine("4k3/8/8/8/8/8/8/4K3 w - - 12 34");
    ASSERT_TRUE(opening);
    EXPECT_EQ(opening->fen, "4k3/8/8/8/8/8/8/4K3 w - - 12 34");
    opening = ParseEpdLine("4k3/8/8/8/8/8/8/4K3 w - - id plain; c0 \"x\"");
    ASSERT_TRUE(opening);
    EXPECT_EQ(opening->name, "plain");
    EXPECT_FALSE(ParseEpdLine("4k3/8/8/8"));
    EXPECT_FALSE(ParseEpdLine("4k3/8/8/8/8/8/8/8 w - -"));
}

TEST(Openings, ParsesFilesOfEitherFormat)
{
    std::vector<std::string> errors;
    const auto epd = ParseOpenings("# a comment\n\n4k3/8/8/8/8/8/8/4K3 w - -\r\nnonsense\n  \n8/8/8/8/8/8/8/K6k b - -",
                                   OpeningFormat::kEpd, 0, &errors);
    EXPECT_EQ(epd.size(), 2u);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("line 4"), std::string::npos) << errors[0];

    const auto pgn = ParseOpenings("[Opening \"Italian\"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bc4 *\n\n[ECO \"A00\"]\n[FEN "
                                   "\"4k3/8/8/8/8/8/8/4K3 w - - 0 1\"]\n\n1. Kd1 *\n\n[Event \"?\"]\n\n1. d4 *\n",
                                   OpeningFormat::kPgn, 4);
    ASSERT_EQ(pgn.size(), 3u);
    EXPECT_EQ(pgn[0].name, "Italian");
    EXPECT_EQ(pgn[0].moves, (std::vector<std::string>{"e2e4", "e7e5", "g1f3", "b8c6"}));
    EXPECT_TRUE(pgn[0].fen.empty());
    EXPECT_EQ(pgn[1].name, "A00");
    EXPECT_EQ(pgn[1].fen, "4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    EXPECT_EQ(pgn[2].name, "");
}

TEST(Openings, OrdersAndLoads)
{
    std::vector<Opening> openings;
    for (int index = 0; index < 6; ++index)
    {
        openings.push_back({"", {}, std::to_string(index)});
    }
    auto rotated = openings;
    OrderOpenings(rotated, OpeningOrder::kSequential, 0, 2);
    EXPECT_EQ(rotated[0].name, "2");
    EXPECT_EQ(rotated[5].name, "1");
    auto first  = openings;
    auto second = openings;
    OrderOpenings(first, OpeningOrder::kRandom, 42);
    OrderOpenings(second, OpeningOrder::kRandom, 42);
    for (std::size_t index = 0; index < openings.size(); ++index)
    {
        EXPECT_EQ(first[index].name, second[index].name) << "one seed, one order";
    }
    std::vector<Opening> none;
    OrderOpenings(none, OpeningOrder::kRandom, 1);

    EXPECT_FALSE(LoadOpenings("/no/such/file.epd", OpeningFormat::kEpd, 0));
    EXPECT_FALSE(ReadTextFile("/no/such/file.epd"));
    const auto path = (std::filesystem::temp_directory_path() / "uci_host_openings_test.epd").string();
    {
        std::ofstream file(path);
        file << "# nothing but a comment\n";
    }
    const auto empty = LoadOpenings(path, OpeningFormat::kEpd, 0);
    ASSERT_FALSE(empty);
    EXPECT_NE(empty.ErrorMessage().find("no openings"), std::string::npos);
    {
        std::ofstream file(path);
        file << "4k3/8/8/8/8/8/8/4K3 w - -\n";
    }
    const auto loaded = LoadOpenings(path, OpeningFormat::kEpd, 0);
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded->size(), 1u);
    std::filesystem::remove(path);
}

TournamentEngine FakeEngine(const std::string &name, std::vector<std::string> arguments = {})
{
    TournamentEngine engine;
    engine.config.executable = UCI_HOST_FAKE_ENGINE;
    engine.config.name       = name;
    engine.config.arguments  = std::move(arguments);
    return engine;
}

TEST(Tournament, ARoundRobinPlaysEveryGameInParallelAndReportsEach)
{
    TournamentSettings settings;
    settings.engines = {FakeEngine("One"), FakeEngine("Two", {"--seed", "5"}), FakeEngine("Three", {"--seed", "9"})};
    settings.engines[2].config.options = {{"Hash", "999999"}}; // Out of range: a warning, not a failure.
    settings.time_control              = *chess::TimeControl::Parse("10+0.1");
    settings.schedule.is_double        = true;
    settings.concurrency               = 3;
    settings.adjudication.max_moves    = 20;
    settings.openings                  = {{"", {"e2e4"}, "e4"}, {"", {"d2d4"}, "d4"}};
    settings.event                     = "Round Robin";
    settings.pgn_path = (std::filesystem::temp_directory_path() / "uci_host_tournament_test.pgn").string();
    std::filesystem::remove(settings.pgn_path);
    std::mutex logged_mutex;
    int        logged      = 0;
    settings.engine_logger = [&](int, const std::string &, uci::LineDirection, std::string_view) {
        std::lock_guard lock(logged_mutex);
        ++logged;
    };

    Tournament tournament(settings);
    ASSERT_TRUE(tournament.Start());
    EXPECT_FALSE(tournament.Start()) << "starting twice";
    EXPECT_EQ(tournament.TotalGames(), 6);
    int  started = 0, finished = 0, warnings = 0;
    bool has_ended = false;
    while (auto event = tournament.Events().PopFor(30s))
    {
        EXPECT_FALSE(has_ended) << "nothing after kFinished";
        switch (event->kind)
        {
        case TournamentEvent::Kind::kGameStarted:
            ++started;
            EXPECT_EQ(event->total_games, 6);
            break;
        case TournamentEvent::Kind::kGameFinished:
            ++finished;
            ASSERT_TRUE(event->record);
            EXPECT_TRUE(event->outcome.IsOver());
            EXPECT_NE(event->outcome.termination, chess::Termination::kDisconnection);
            break;
        case TournamentEvent::Kind::kWarning:
            ++warnings;
            EXPECT_NE(event->message.find("Hash"), std::string::npos) << event->message;
            break;
        case TournamentEvent::Kind::kFinished:
            has_ended = true;
            break;
        }
    }
    tournament.Wait();
    EXPECT_TRUE(tournament.Events().IsDrained());
    EXPECT_TRUE(has_ended);
    EXPECT_EQ(started, 6);
    EXPECT_EQ(finished, 6);
    EXPECT_GE(warnings, 1);
    EXPECT_GT(logged, 0);
    EXPECT_EQ(tournament.Results().size(), 6u);
    EXPECT_EQ(tournament.EngineNames(), (std::vector<std::string>{"One", "Two", "Three"}));
    EXPECT_EQ(tournament.Schedule().size(), 6u);

    const auto text = ReadTextFile(settings.pgn_path);
    ASSERT_TRUE(text);
    std::vector<std::string> errors;
    const auto               games = chess::ReadPgn(*text, &errors);
    EXPECT_TRUE(errors.empty());
    ASSERT_EQ(games.size(), 6u);
    std::set<std::string> openings;
    for (const auto &game : games)
    {
        EXPECT_EQ(game.Tag("Event"), "Round Robin");
        openings.insert(game.moves.front().san);
    }
    EXPECT_EQ(openings, (std::set<std::string>{"e4", "d4"}));
    std::filesystem::remove(settings.pgn_path);
}

TEST(Tournament, StopAbandonsTheGamesInProgress)
{
    TournamentSettings settings;
    settings.engines         = {FakeEngine("Slow", {"--think", "300"}), FakeEngine("Fast")};
    settings.time_control    = *chess::TimeControl::Parse("60");
    settings.schedule.rounds = 10;
    settings.concurrency     = 2;
    Tournament tournament(settings);
    ASSERT_TRUE(tournament.Start());
    std::this_thread::sleep_for(500ms);
    const auto start = std::chrono::steady_clock::now();
    tournament.RequestStop();
    while (tournament.Events().PopFor(30s))
    {
    }
    tournament.Wait();
    EXPECT_LT(std::chrono::steady_clock::now() - start, 15s);
    EXPECT_LT(tournament.Results().size(), 10u);
    for (const auto &result : tournament.Results())
    {
        EXPECT_EQ(result.result, chess::GameResult::kNone) << "every game was abandoned";
    }
}

TEST(Tournament, RefusesWhatCannotBePlayed)
{
    TournamentSettings settings;
    settings.engines = {FakeEngine("Only")};
    EXPECT_FALSE(Tournament(settings).Start());
    settings.engines.push_back(FakeEngine("Other"));
    settings.schedule.rounds = 0;
    EXPECT_FALSE(Tournament(settings).Start());
}

TEST(Tournament, AnUnwritablePgnFileIsAWarning)
{
    TournamentSettings settings;
    settings.engines                = {FakeEngine("A"), FakeEngine("B")};
    settings.time_control           = *chess::TimeControl::Parse("10");
    settings.adjudication.max_moves = 2;
    settings.pgn_path               = "/no/such/directory/games.pgn";
    Tournament tournament(settings);
    ASSERT_TRUE(tournament.Start());
    bool has_warning = false;
    while (auto event = tournament.Events().PopFor(30s))
    {
        has_warning |=
            event->kind == TournamentEvent::Kind::kWarning && event->message.find("cannot write") != std::string::npos;
    }
    EXPECT_TRUE(has_warning);
}

} // namespace
} // namespace uci_host::match
