#include "TestSuite.h"

#include "chess/Notation.h"
#include "uci/Engine.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace amphitheater::match
{

namespace
{

/** How long past a search's own time the run waits for its answer before it gives the engine up. */
constexpr std::chrono::seconds kGrace{10};

/** How long a depth-limited search is waited for. */
constexpr std::chrono::minutes kDepthWait{10};

/**
 * @param text Some text.
 * @return It with the spaces at either end taken off.
 */
std::string_view Trimmed(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
    {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

/**
 * @param text Some text.
 * @return Its words, parted by spaces.
 */
std::vector<std::string_view> Words(std::string_view text)
{
    std::vector<std::string_view> words;
    std::size_t                   at = 0;
    while (at < text.size())
    {
        const auto start = text.find_first_not_of(" \t", at);
        if (start == std::string_view::npos)
        {
            break;
        }
        const auto end = text.find_first_of(" \t", start);
        words.push_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        at = end == std::string_view::npos ? text.size() : end;
    }
    return words;
}

/**
 * The operations after an EPD line's position, each "opcode operands;", a quoted operand kept whole.
 * @param text The operations.
 * @return Each operation's opcode and the text of its operands.
 */
std::vector<std::pair<std::string, std::string>> Operations(std::string_view text)
{
    std::vector<std::pair<std::string, std::string>> operations;
    std::string                                      current;
    bool                                             is_quoted = false;
    const auto                                       finish    = [&] {
        const std::string_view operation = Trimmed(current);
        if (!operation.empty())
        {
            const auto space = operation.find_first_of(" \t");
            operations.emplace_back(std::string(operation.substr(0, space)),
                                    space == std::string_view::npos ? std::string()
                                                                    : std::string(Trimmed(operation.substr(space))));
        }
        current.clear();
    };
    for (const char c : text)
    {
        if (c == '"')
        {
            is_quoted = !is_quoted;
        }
        if (c == ';' && !is_quoted)
        {
            finish();
            continue;
        }
        current += c;
    }
    finish();
    return operations;
}

/**
 * Read an operation's moves.
 * @param position The position they are played in.
 * @param operands The moves in SAN, parted by spaces.
 * @param moves The moves read, appended.
 * @return Why one could not be read; empty when all were.
 */
std::string ReadMoves(const chess::Position &position, std::string_view operands, std::vector<chess::Move> &moves)
{
    for (const std::string_view word : Words(operands))
    {
        const auto move = chess::ParseSan(position, word);
        if (!move)
        {
            return "'" + std::string(word) + "' is not a legal move here: " + move.ErrorMessage();
        }
        moves.push_back(*move);
    }
    return {};
}

} // namespace

Result<TestPosition> ParseTestPosition(std::string_view line, int number)
{
    const std::vector<std::string_view> words = Words(line);
    if (words.size() < 4)
    {
        return Error{"an EPD line has a position's four fields, then its operations"};
    }
    TestPosition test;
    test.line                    = number;
    test.id                      = "line " + std::to_string(number);
    const std::size_t rest       = static_cast<std::size_t>(words[3].data() - line.data()) + words[3].size();
    const auto        operations = Operations(line.substr(rest));
    std::string       halfmove   = "0";
    std::string       fullmove   = "1";
    for (const auto &[opcode, operands] : operations)
    {
        if (opcode == "hmvc" && !operands.empty())
        {
            halfmove = operands;
        }
        else if (opcode == "fmvn" && !operands.empty())
        {
            fullmove = operands;
        }
    }
    const std::string fen = std::string(words[0]) + ' ' + std::string(words[1]) + ' ' + std::string(words[2]) + ' ' +
                            std::string(words[3]) + ' ' + halfmove + ' ' + fullmove;
    const auto        position = chess::Position::FromFen(fen, chess::FenMode::kTolerant);
    if (!position)
    {
        return Error{position.ErrorMessage()};
    }
    test.position = *position;
    for (const auto &[opcode, operands] : operations)
    {
        if (opcode == "bm" || opcode == "am")
        {
            std::string why = ReadMoves(test.position, operands, opcode == "bm" ? test.best_moves : test.avoid_moves);
            if (!why.empty())
            {
                return Error{opcode + ": " + why};
            }
        }
        else if (opcode == "id")
        {
            std::string id = operands;
            if (id.size() >= 2 && id.front() == '"' && id.back() == '"')
            {
                id = id.substr(1, id.size() - 2);
            }
            if (!id.empty())
            {
                test.id = id;
            }
        }
    }
    if (test.best_moves.empty() && test.avoid_moves.empty())
    {
        return Error{"the line has no bm and no am: nothing to solve"};
    }
    return test;
}

std::vector<TestPosition> ParseTestSuite(std::string_view text, std::vector<std::string> *errors)
{
    std::vector<TestPosition> positions;
    int                       number = 0;
    std::size_t               at     = 0;
    while (at <= text.size())
    {
        const auto             end = text.find('\n', at);
        const std::string_view line =
            Trimmed(text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at));
        ++number;
        at = end == std::string_view::npos ? text.size() + 1 : end + 1;
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        auto position = ParseTestPosition(line, number);
        if (position)
        {
            positions.push_back(std::move(*position));
        }
        else if (errors != nullptr)
        {
            errors->push_back("line " + std::to_string(number) + ": " + position.ErrorMessage());
        }
    }
    return positions;
}

bool Solves(const TestPosition &position, chess::Move move)
{
    const auto has = [move](const std::vector<chess::Move> &moves) {
        return std::find(moves.begin(), moves.end(), move) != moves.end();
    };
    if (has(position.avoid_moves))
    {
        return false;
    }
    return position.best_moves.empty() || has(position.best_moves);
}

TestSuiteRunner::TestSuiteRunner(TestSuiteSettings settings, std::vector<TestPosition> positions)
    : settings_(std::move(settings)), positions_(std::move(positions))
{
}

TestSuiteRunner::~TestSuiteRunner()
{
    Stop();
    if (thread_.joinable())
    {
        thread_.join();
    }
}

void TestSuiteRunner::Start()
{
    if (!thread_.joinable())
    {
        thread_ = std::thread([this] { Run(); });
    }
}

void TestSuiteRunner::Stop()
{
    is_stopping_ = true;
}

std::optional<TestOutcome> TestSuiteRunner::Next()
{
    return outcomes_.TryPop();
}

std::string TestSuiteRunner::Failure() const
{
    std::lock_guard lock(mutex_);
    return failure_;
}

void TestSuiteRunner::Fail(std::string failure)
{
    std::lock_guard lock(mutex_);
    failure_ = std::move(failure);
}

void TestSuiteRunner::Run()
{
    uci::Engine engine(settings_.engine);
    if (const auto started = engine.Start(); !started)
    {
        Fail("the engine could not be started: " + started.ErrorMessage());
        is_finished_ = true;
        return;
    }
    for (std::size_t index = 0; index < positions_.size() && !is_stopping_; ++index)
    {
        const TestPosition &test = positions_[index];
        static_cast<void>(engine.NewGame()); // each position a game of its own: no table carried over
        if (!engine.SetPosition(test.position.ToFen(), {}))
        {
            Fail("the engine stopped taking commands");
            break;
        }
        uci::GoParameters go;
        go.move_time       = settings_.move_time;
        go.depth           = settings_.depth;
        const auto waiting = settings_.move_time ? *settings_.move_time + kGrace
                                                 : std::chrono::duration_cast<chess::Milliseconds>(kDepthWait);
        const uci::SearchResult result = engine.Search(go, std::chrono::steady_clock::now() + waiting);
        if (result.ending != uci::SearchResult::Ending::kBestMove)
        {
            Fail(result.ending == uci::SearchResult::Ending::kExited ? "the engine quit"
                                                                     : "the engine did not answer in time");
            break;
        }
        TestOutcome outcome;
        outcome.index = static_cast<int>(index);
        outcome.score = result.info.score;
        outcome.depth = result.info.depth.value_or(0);
        if (const auto move = chess::ParseUciMove(test.position, result.best_move.move))
        {
            outcome.move      = chess::MoveToSan(test.position, *move);
            outcome.is_solved = Solves(test, *move);
        }
        outcomes_.Push(std::move(outcome));
    }
    engine.Quit();
    is_finished_ = true;
}

} // namespace amphitheater::match
