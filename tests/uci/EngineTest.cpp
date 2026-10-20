// The child process and the engine session, against the fake engine (tests/fake_engine): the handshake, options,
// searches, pondering, and every way an engine misbehaves - crashing, hanging, garbage, silence, ignoring quit.

#include "uci/Engine.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <thread>

namespace amphitheater::uci
{
namespace
{

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

EngineConfig FakeEngine(std::vector<std::string> arguments = {})
{
    EngineConfig config;
    config.executable = AMPHITHEATER_FAKE_ENGINE;
    config.arguments  = std::move(arguments);
    return config;
}

std::vector<std::string> DrainLines(ChildProcess &child, chess::Milliseconds timeout = 5000ms)
{
    std::vector<std::string> lines;
    const auto               deadline = Clock::now() + timeout;
    while (auto line = child.Output().PopUntil(deadline))
    {
        lines.push_back(line->text);
    }
    return lines;
}

TEST(ChildProcess, PassesArgumentsWholeIncludingSpacesQuotesAndUnicode)
{
    ProcessOptions options;
    options.executable = AMPHITHEATER_FAKE_ENGINE;
    options.arguments = {"--print-arguments", "two words", "", "a\"quote", "back\\slash\\", "caf\xC3\xA9 \xE2\x99\x9E"};
    auto child        = ChildProcess::Start(options);
    ASSERT_TRUE(child) << child.ErrorMessage();
    const auto lines = DrainLines(**child);
    EXPECT_EQ(lines, options.arguments);
    EXPECT_TRUE((*child)->WaitForExit(5000ms));
    EXPECT_EQ((*child)->ExitCode(), 7);
    EXPECT_FALSE((*child)->IsRunning());
    EXPECT_GT((*child)->Id(), 0);
}

TEST(ChildProcess, RunsInItsWorkingDirectory)
{
    const auto     directory = std::filesystem::temp_directory_path();
    ProcessOptions options;
    options.executable        = AMPHITHEATER_FAKE_ENGINE;
    options.arguments         = {"--print-directory"};
    const std::u8string text  = directory.u8string();
    options.working_directory = std::string(text.begin(), text.end());
    auto child                = ChildProcess::Start(options);
    ASSERT_TRUE(child) << child.ErrorMessage();
    const auto lines = DrainLines(**child);
    ASSERT_EQ(lines.size(), 1u);
    const auto printed = std::filesystem::path(std::u8string(lines[0].begin(), lines[0].end()));
    EXPECT_TRUE(std::filesystem::equivalent(printed, directory)) << lines[0];
}

TEST(ChildProcess, ReportsAProgramThatCannotStart)
{
    ProcessOptions options;
    options.executable = "/no/such/engine/anywhere";
    EXPECT_FALSE(ChildProcess::Start(options));
    options.executable = "";
    EXPECT_FALSE(ChildProcess::Start(options));
}

TEST(ChildProcess, CapturesErrorOutputSeparately)
{
    ProcessOptions options;
    options.executable = AMPHITHEATER_FAKE_ENGINE;
    options.arguments  = {"--stderr"};
    auto child         = ChildProcess::Start(options);
    ASSERT_TRUE(child);
    ASSERT_TRUE((*child)->WriteLine("isready"));
    bool       has_output = false;
    bool       has_error  = false;
    const auto deadline   = Clock::now() + 5000ms;
    while (!(has_output && has_error))
    {
        auto line = (*child)->Output().PopUntil(deadline);
        ASSERT_TRUE(line);
        has_output |= line->stream == ProcessStream::kOutput && line->text == "readyok";
        has_error |= line->stream == ProcessStream::kError && line->text == "fake engine read: isready";
    }
    (*child)->CloseInput();
    EXPECT_TRUE((*child)->WaitForExit(5000ms));
    EXPECT_FALSE((*child)->WriteLine("isready"));
}

TEST(ChildProcess, DiscardsErrorOutputWhenAsked)
{
    ProcessOptions options;
    options.executable                = AMPHITHEATER_FAKE_ENGINE;
    options.arguments                 = {"--stderr"};
    options.does_capture_error_output = false;
    auto child                        = ChildProcess::Start(options);
    ASSERT_TRUE(child);
    (*child)->WriteLine("isready");
    (*child)->CloseInput();
    for (const auto &line : DrainLines(**child))
    {
        EXPECT_EQ(line, "readyok");
    }
}

TEST(ChildProcess, KillsAProcessThatWillNotExit)
{
    ProcessOptions options;
    options.executable = AMPHITHEATER_FAKE_ENGINE;
    options.arguments  = {"--hang-on-quit"};
    auto child         = ChildProcess::Start(options);
    ASSERT_TRUE(child);
    (*child)->WriteLine("quit");
    (*child)->CloseInput();
    EXPECT_FALSE((*child)->WaitForExit(300ms));
    (*child)->Kill();
    EXPECT_FALSE((*child)->IsRunning());
    EXPECT_TRUE((*child)->ExitCode().has_value());
    EXPECT_FALSE((*child)->WriteLine("isready"));
}

TEST(ChildProcess, DestroyingARunningProcessKillsIt)
{
    ProcessOptions options;
    options.executable = AMPHITHEATER_FAKE_ENGINE;
    options.arguments  = {"--hang-on-quit"};
    auto child         = ChildProcess::Start(options);
    ASSERT_TRUE(child);
    const auto start = Clock::now();
    child->reset();
    EXPECT_LT(Clock::now() - start, 5s);
}

TEST(Engine, HandshakeReadsNameAuthorAndEveryOption)
{
    Engine     engine(FakeEngine({"--name", "Fake One"}));
    const auto status = engine.Start();
    ASSERT_TRUE(status) << status.ErrorMessage();
    EXPECT_EQ(engine.Name(), "Fake One");
    EXPECT_EQ(engine.Author(), "The Tests");
    ASSERT_EQ(engine.Options().size(), 7u);
    const EngineOption *hash = engine.FindOption("hash");
    ASSERT_NE(hash, nullptr);
    EXPECT_EQ(hash->maximum, 1024);
    const EngineOption *spaced = engine.FindOption("Name With Spaces");
    ASSERT_NE(spaced, nullptr);
    EXPECT_EQ(spaced->default_value, "a b c");
    EXPECT_EQ(engine.FindOption("Nonsense"), nullptr);
    EXPECT_TRUE(engine.Warnings().empty());
    EXPECT_TRUE(engine.IsRunning());
    EXPECT_FALSE(engine.Start()) << "starting twice";
}

TEST(Engine, NameFallsBackToTheConfiguredThenTheEngines)
{
    auto config = FakeEngine({"--name", "Own Name"});
    config.name = "Configured";
    Engine configured(config);
    ASSERT_TRUE(configured.Start());
    EXPECT_EQ(configured.Name(), "Configured");
    EXPECT_EQ(configured.Config().name, "Configured");
}

TEST(Engine, SetsConfiguredOptionsAndWarnsAboutBadOnes)
{
    auto config    = FakeEngine();
    config.options = {{"Hash", "64"}, {"No Such Option", "1"}, {"Threads", "999"}, {"clear hash", ""}};
    // Declared before the engine, whose destructor still logs its quit.
    std::vector<std::string> sent;
    Engine                   engine(config);
    engine.SetLogger([&](LineDirection direction, std::string_view line) {
        if (direction == LineDirection::kToEngine)
        {
            sent.emplace_back(line);
        }
    });
    ASSERT_TRUE(engine.Start());
    EXPECT_EQ(engine.Warnings().size(), 2u);
    EXPECT_NE(std::find(sent.begin(), sent.end(), "setoption name Hash value 64"), sent.end());
    EXPECT_NE(std::find(sent.begin(), sent.end(), "setoption name Clear Hash"), sent.end());
    EXPECT_FALSE(engine.SetOption("Ponder", "maybe"));
    EXPECT_TRUE(engine.SetOption("Style", "risky"));
    // The fake engine answers each setoption; the confirmations wait in the queue.
    bool is_confirmed = false;
    while (auto event = engine.NextEvent(200ms))
    {
        is_confirmed |= event->kind == EngineEventKind::kInfo && event->info.text == "option Style = risky";
    }
    EXPECT_TRUE(is_confirmed);
}

TEST(Engine, ToleratesGarbageAndMalformedOptionLines)
{
    Engine engine(FakeEngine({"--garbage"}));
    ASSERT_TRUE(engine.Start());
    EXPECT_EQ(engine.Options().size(), 7u);
    EXPECT_EQ(engine.Warnings().size(), 2u);
    ASSERT_TRUE(engine.NewGame());
    ASSERT_TRUE(engine.SetPosition("", {"e2e4"}));
    GoParameters parameters;
    parameters.depth  = 1;
    const auto result = engine.Search(parameters, Clock::now() + 5s);
    EXPECT_EQ(result.ending, SearchResult::Ending::kBestMove);
    EXPECT_FALSE(result.best_move.move.empty());
    EXPECT_EQ(result.info.depth, 2);
}

TEST(Engine, SearchSummarisesInfoAndTimesTheAnswer)
{
    Engine engine(FakeEngine({"--think", "150", "--score", "42"}));
    ASSERT_TRUE(engine.Start());
    ASSERT_TRUE(engine.SetPosition("", {}));
    GoParameters parameters;
    parameters.white_time = chess::Milliseconds{10000};
    parameters.black_time = chess::Milliseconds{10000};
    int        info_count = 0;
    const auto result     = engine.Search(parameters, Clock::now() + 5s, [&](const SearchInfo &) { ++info_count; });
    ASSERT_EQ(result.ending, SearchResult::Ending::kBestMove);
    EXPECT_EQ(info_count, 4);
    EXPECT_EQ(result.info.depth, 2);
    EXPECT_EQ(result.info.selective_depth, 3);
    ASSERT_TRUE(result.info.score);
    EXPECT_EQ(result.info.score->value, 42);
    EXPECT_EQ(result.info.nodes, 20);
    EXPECT_EQ(result.info.pv.size(), 1u);
    EXPECT_GE(result.elapsed.count(), 140);
    EXPECT_LT(result.elapsed.count(), 2000);
    EXPECT_FALSE(engine.IsSearching());
}

TEST(Engine, InfiniteSearchStopsOnStop)
{
    Engine engine(FakeEngine());
    ASSERT_TRUE(engine.Start());
    engine.SetPosition("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", {});
    GoParameters parameters;
    parameters.is_infinite  = true;
    parameters.search_moves = {"e7e5"};
    ASSERT_TRUE(engine.Go(parameters));
    EXPECT_TRUE(engine.IsSearching());
    // Nothing but info until it is told to stop.
    while (auto event = engine.NextEvent(300ms))
    {
        EXPECT_NE(event->kind, EngineEventKind::kBestMove);
    }
    ASSERT_TRUE(engine.Stop());
    std::optional<EngineEvent> answer;
    while (auto event = engine.NextEvent(5000ms))
    {
        if (event->kind == EngineEventKind::kBestMove)
        {
            answer = event;
            break;
        }
    }
    ASSERT_TRUE(answer);
    EXPECT_EQ(answer->best_move.move, "e7e5");
    EXPECT_FALSE(engine.IsSearching());
}

TEST(Engine, PonderHitTurnsAPonderSearchIntoARealOne)
{
    Engine engine(FakeEngine({"--think", "50"}));
    ASSERT_TRUE(engine.Start());
    engine.SetPosition("", {"e2e4"});
    GoParameters parameters;
    parameters.is_ponder = true;
    ASSERT_TRUE(engine.Go(parameters));
    while (auto event = engine.NextEvent(200ms))
    {
        EXPECT_NE(event->kind, EngineEventKind::kBestMove);
    }
    ASSERT_TRUE(engine.PonderHit());
    bool has_answered = false;
    while (auto event = engine.NextEvent(5000ms))
    {
        if (event->kind == EngineEventKind::kBestMove)
        {
            has_answered = true;
            break;
        }
    }
    EXPECT_TRUE(has_answered);
}

TEST(Engine, ReportsATimeoutWhenTheEngineHangs)
{
    Engine engine(FakeEngine({"--hang-at", "1"}));
    ASSERT_TRUE(engine.Start());
    engine.SetPosition("", {});
    GoParameters parameters;
    parameters.depth  = 1;
    const auto result = engine.Search(parameters, Clock::now() + 300ms);
    EXPECT_EQ(result.ending, SearchResult::Ending::kTimeout);
    EXPECT_GE(result.elapsed.count(), 290);
    EXPECT_TRUE(engine.IsSearching());
    EXPECT_FALSE(engine.WaitReady(200ms));
    const auto start = Clock::now();
    engine.Quit(); // stop and quit go unanswered: the engine is killed after the quit timeout.
    EXPECT_LT(Clock::now() - start, 5s);
    EXPECT_FALSE(engine.IsRunning());
}

TEST(Engine, ReportsACrashDuringASearch)
{
    Engine engine(FakeEngine({"--crash-at", "2"}));
    ASSERT_TRUE(engine.Start());
    engine.SetPosition("", {});
    GoParameters parameters;
    parameters.depth = 1;
    EXPECT_EQ(engine.Search(parameters, Clock::now() + 5s).ending, SearchResult::Ending::kBestMove);
    const auto result = engine.Search(parameters, Clock::now() + 5s);
    EXPECT_EQ(result.ending, SearchResult::Ending::kExited);
    EXPECT_FALSE(engine.IsRunning());
    EXPECT_EQ(engine.ExitCode(), 3);
    EXPECT_FALSE(engine.NextEvent(10ms));
    EXPECT_EQ(engine.Search(parameters, Clock::now() + 1s).ending, SearchResult::Ending::kExited);
    EXPECT_FALSE(engine.SetOption("Hash", "1"));
    EXPECT_FALSE(engine.WaitReady(100ms));
    EXPECT_FALSE(engine.NewGame());
}

TEST(Engine, AnEmptyBestMoveIsAnAnswerWithNoMove)
{
    Engine engine(FakeEngine({"--empty-bestmove"}));
    ASSERT_TRUE(engine.Start());
    engine.SetPosition("", {});
    GoParameters parameters;
    parameters.depth  = 1;
    const auto result = engine.Search(parameters, Clock::now() + 5s);
    EXPECT_EQ(result.ending, SearchResult::Ending::kBestMove);
    EXPECT_TRUE(result.best_move.move.empty());
}

TEST(Engine, StartFailsWhenTheEngineNeverSaysUciok)
{
    auto config             = FakeEngine({"--no-uciok"});
    config.timeouts.startup = 300ms;
    Engine     engine(config);
    const auto start  = Clock::now();
    const auto status = engine.Start();
    EXPECT_FALSE(status);
    EXPECT_NE(status.ErrorMessage().find("uciok"), std::string::npos) << status.ErrorMessage();
    EXPECT_LT(Clock::now() - start, 5s);
}

TEST(Engine, StartFailsWhenTheEngineExitsDuringTheHandshake)
{
    Engine     engine(FakeEngine({"--exit-on-uci"}));
    const auto status = engine.Start();
    EXPECT_FALSE(status);
    EXPECT_NE(status.ErrorMessage().find("exited"), std::string::npos) << status.ErrorMessage();
}

TEST(Engine, StartFailsForAProgramThatIsNotThere)
{
    EngineConfig config;
    config.executable = "no-such-engine-exists-here";
    Engine engine(config);
    EXPECT_FALSE(engine.Start());
    EXPECT_FALSE(engine.IsRunning());
    EXPECT_FALSE(engine.ExitCode());
    EXPECT_FALSE(engine.SetPosition("", {}));
    EXPECT_FALSE(engine.Go({}));
    EXPECT_FALSE(engine.Stop());
    EXPECT_FALSE(engine.PonderHit());
    EXPECT_FALSE(engine.PollEvent());
    engine.Kill();
    engine.Quit();
}

TEST(Engine, ASlowStartIsWaitedFor)
{
    Engine engine(FakeEngine({"--slow-start", "200"}));
    EXPECT_TRUE(engine.Start());
}

TEST(Engine, ErrorOutputArrivesAsEventsAndInTheLog)
{
    // Declared before the engine, whose destructor still logs its quit.
    int    error_lines = 0;
    Engine engine(FakeEngine({"--stderr"}));
    engine.SetLogger([&](LineDirection direction, std::string_view) {
        error_lines += direction == LineDirection::kFromEngineError ? 1 : 0;
    });
    ASSERT_TRUE(engine.Start());
    // The engine writes a line on standard error for each command; the two streams reach the queue independently,
    // so the lines are counted once everything has been drained.
    ASSERT_TRUE(engine.SetOption("Hash", "32"));
    bool has_error_event = false;
    while (auto event = engine.NextEvent(300ms))
    {
        has_error_event |= event->kind == EngineEventKind::kErrorOutput;
    }
    EXPECT_TRUE(has_error_event);
    EXPECT_GE(error_lines, 3) << "uci, isready and setoption";
}

TEST(Engine, QuitKillsAnEngineThatIgnoresIt)
{
    auto config          = FakeEngine({"--hang-on-quit"});
    config.timeouts.quit = 200ms;
    Engine engine(config);
    ASSERT_TRUE(engine.Start());
    const auto start = Clock::now();
    engine.Quit();
    // A wait's own timeout may end a tick of the system's clock early (Windows' 15.6 ms), so the floor allows for one.
    EXPECT_GE(Clock::now() - start, 170ms);
    EXPECT_LT(Clock::now() - start, 5s);
    EXPECT_FALSE(engine.IsRunning());
}

TEST(Engine, ARelativePathIsFromTheHostsDirectory)
{
    const auto absolute = std::filesystem::path(AMPHITHEATER_FAKE_ENGINE);
    const auto relative = std::filesystem::relative(absolute);
    if (relative.empty() || !relative.has_parent_path())
    {
        GTEST_SKIP() << "the fake engine is not below the working directory";
    }
    auto                config = FakeEngine();
    const std::u8string text   = relative.u8string();
    config.executable          = std::string(text.begin(), text.end());
    config.working_directory   = std::filesystem::temp_directory_path().string();
    Engine engine(config);
    EXPECT_TRUE(engine.Start());
}

} // namespace
} // namespace amphitheater::uci
