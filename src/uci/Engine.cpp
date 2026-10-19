#include "Engine.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace uci_host::uci
{

namespace
{

using Clock = std::chrono::steady_clock;

/**
 * UTF-8 text as a path, whatever the platform's narrow encoding.
 * @param text UTF-8.
 * @return The path.
 */
std::filesystem::path PathFromUtf8(const std::string &text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

/**
 * A path as UTF-8 text.
 * @param path The path.
 * @return UTF-8.
 */
std::string Utf8FromPath(const std::filesystem::path &path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

/**
 * Two option names compared as UCI compares them, without regard to case.
 * @param left One name.
 * @param right The other.
 * @return Whether they name the same option.
 */
bool SameOptionName(std::string_view left, std::string_view right)
{
    return std::equal(left.begin(), left.end(), right.begin(), right.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

/**
 * Fold an info line's fields into a search's running summary.
 * @param summary The summary, updated.
 * @param info The new line.
 */
void MergeInfo(SearchInfo &summary, const SearchInfo &info)
{
    if (info.multipv && *info.multipv != 1)
    {
        return; // The summary follows the first line of a multi-line search.
    }
    const auto take = [](auto &into, const auto &from) {
        if (from)
        {
            into = from;
        }
    };
    take(summary.depth, info.depth);
    take(summary.selective_depth, info.selective_depth);
    take(summary.multipv, info.multipv);
    take(summary.score, info.score);
    take(summary.nodes, info.nodes);
    take(summary.nodes_per_second, info.nodes_per_second);
    take(summary.hash_full, info.hash_full);
    take(summary.tablebase_hits, info.tablebase_hits);
    take(summary.time, info.time);
    take(summary.cpu_load, info.cpu_load);
    if (!info.pv.empty())
    {
        summary.pv = info.pv;
    }
}

} // namespace

Engine::Engine(EngineConfig config) : config_(std::move(config))
{
}

Engine::~Engine()
{
    Quit();
}

Status Engine::Start()
{
    if (process_)
    {
        return Error{"the engine is already started"};
    }
    ProcessOptions options;
    options.executable        = config_.executable;
    options.arguments         = config_.arguments;
    options.working_directory = config_.working_directory;
    // A relative path names a file from the host's directory on every platform (POSIX would otherwise resolve it
    // from the working directory the child changes to).
    const std::filesystem::path executable = PathFromUtf8(config_.executable);
    if (executable.is_relative() && executable.has_parent_path())
    {
        std::error_code error;
        const auto      absolute = std::filesystem::absolute(executable, error);
        if (!error)
        {
            options.executable = Utf8FromPath(absolute);
        }
    }
    auto started = ChildProcess::Start(options);
    if (!started)
    {
        return Error{started.ErrorMessage()};
    }
    process_       = std::move(*started);
    has_told_exit_ = false;
    name_          = config_.name;
    author_.clear();
    options_.clear();
    warnings_.clear();
    kept_events_.clear();

    std::string engine_name;
    const auto  deadline = Clock::now() + config_.timeouts.startup;
    Send("uci");
    bool is_ok = false;
    while (!is_ok)
    {
        auto event = NextEventUntil(deadline);
        if (!event)
        {
            Kill();
            return Error{"'" + config_.executable + "' did not answer 'uci' with 'uciok' within " +
                         chess::SecondsText(config_.timeouts.startup) + " s"};
        }
        if (event->kind == EngineEventKind::kExited)
        {
            const auto code = ExitCode();
            return Error{"'" + config_.executable + "' exited during the UCI handshake" +
                         (code ? " (exit code " + std::to_string(*code) + ")" : std::string{})};
        }
        if (event->kind != EngineEventKind::kOther)
        {
            continue;
        }
        const auto words = SplitWords(event->line);
        if (words.empty())
        {
            continue;
        }
        if (words[0] == "uciok")
        {
            is_ok = true;
        }
        else if (words[0] == "id" && words.size() >= 2)
        {
            const std::size_t value_start =
                words.size() > 2 ? static_cast<std::size_t>(words[2].data() - event->line.data()) : event->line.size();
            const std::string value = event->line.substr(value_start);
            if (words[1] == "name")
            {
                engine_name = value;
            }
            else if (words[1] == "author")
            {
                author_ = value;
            }
        }
        else if (words[0] == "option")
        {
            auto option = ParseOptionLine(event->line);
            if (option)
            {
                options_.push_back(std::move(*option));
            }
            else
            {
                warnings_.push_back("ignored the engine's option line '" + event->line + "': " + option.ErrorMessage());
            }
        }
    }
    if (name_.empty())
    {
        name_ = !engine_name.empty() ? engine_name : Utf8FromPath(executable.stem());
    }

    // Pondering on: an engine that has the Ponder option is told so (UCI's convention for a host that will ponder).
    if (config_.does_ponder && FindOption("Ponder") != nullptr && FindOption("Ponder")->type == OptionType::kCheck)
    {
        SetOption("Ponder", "true");
    }
    for (const auto &[option_name, value] : config_.options)
    {
        const auto status = SetOption(option_name, value);
        if (!status)
        {
            warnings_.push_back("skipped: " + status.ErrorMessage());
        }
    }
    return WaitReady(config_.timeouts.ready);
}

const EngineOption *Engine::FindOption(std::string_view name) const
{
    for (const auto &option : options_)
    {
        if (SameOptionName(option.name, name))
        {
            return &option;
        }
    }
    return nullptr;
}

Status Engine::SetOption(std::string_view name, std::string_view value)
{
    const EngineOption *option = FindOption(name);
    if (option == nullptr)
    {
        return Error{"the engine has no option '" + std::string(name) + "'"};
    }
    if (const auto problem = option->Validate(value))
    {
        return Error{*problem};
    }
    if (!Send(FormatSetOption(option->name, value)))
    {
        return Error{"the engine is not running"};
    }
    return Ok{};
}

Status Engine::WaitReady(std::optional<chess::Milliseconds> timeout)
{
    const auto wait = timeout.value_or(config_.timeouts.ready);
    if (!Send("isready"))
    {
        return Error{"the engine is not running"};
    }
    const auto              deadline = Clock::now() + wait;
    std::deque<EngineEvent> meanwhile;
    while (true)
    {
        auto event = NextEventUntil(deadline);
        if (!event)
        {
            kept_events_.insert(kept_events_.begin(), meanwhile.begin(), meanwhile.end());
            return Error{"'" + name_ + "' did not answer 'isready' within " + chess::SecondsText(wait) + " s"};
        }
        if (event->kind == EngineEventKind::kReadyOk)
        {
            kept_events_.insert(kept_events_.begin(), meanwhile.begin(), meanwhile.end());
            return Ok{};
        }
        const bool has_exited = event->kind == EngineEventKind::kExited;
        meanwhile.push_back(std::move(*event));
        if (has_exited)
        {
            kept_events_.insert(kept_events_.begin(), meanwhile.begin(), meanwhile.end());
            return Error{"'" + name_ + "' exited"};
        }
    }
}

Status Engine::NewGame()
{
    if (!Send("ucinewgame"))
    {
        return Error{"the engine is not running"};
    }
    return WaitReady();
}

bool Engine::SetPosition(std::string_view fen, const std::vector<std::string> &moves)
{
    return Send(FormatPosition(fen, moves));
}

bool Engine::Go(const GoParameters &parameters)
{
    const bool is_sent = Send(FormatGo(parameters));
    is_searching_      = is_sent;
    return is_sent;
}

bool Engine::Stop()
{
    return Send("stop");
}

bool Engine::PonderHit()
{
    return Send("ponderhit");
}

SearchResult Engine::Search(const GoParameters &parameters, Clock::time_point deadline,
                            const std::function<void(const SearchInfo &)> &on_info)
{
    SearchResult result;
    // Anything left from before belongs to no search of this one's.
    while (PollEvent())
    {
    }
    const auto start = Clock::now();
    if (!Go(parameters))
    {
        result.ending  = SearchResult::Ending::kExited;
        result.elapsed = chess::Milliseconds{0};
        return result;
    }
    return AwaitBestMove(start, deadline, on_info);
}

SearchResult Engine::AwaitBestMove(Clock::time_point start, Clock::time_point deadline,
                                   const std::function<void(const SearchInfo &)> &on_info)
{
    SearchResult result;
    // A "bestmove" read before the clock started (one sent early while pondering) took no time of the engine's.
    const auto since_start = [&](Clock::time_point when) {
        return std::max(chess::Milliseconds{0}, std::chrono::duration_cast<chess::Milliseconds>(when - start));
    };
    while (true)
    {
        auto event = NextEventUntil(deadline);
        if (!event)
        {
            result.ending  = IsRunning() ? SearchResult::Ending::kTimeout : SearchResult::Ending::kExited;
            result.elapsed = since_start(Clock::now());
            return result;
        }
        if (event->kind == EngineEventKind::kInfo)
        {
            MergeInfo(result.info, event->info);
            if (on_info)
            {
                on_info(event->info);
            }
        }
        else if (event->kind == EngineEventKind::kBestMove)
        {
            result.ending    = SearchResult::Ending::kBestMove;
            result.best_move = event->best_move;
            result.elapsed   = since_start(event->received);
            return result;
        }
        else if (event->kind == EngineEventKind::kExited)
        {
            result.ending  = SearchResult::Ending::kExited;
            result.elapsed = since_start(Clock::now());
            return result;
        }
    }
}

std::optional<EngineEvent> Engine::NextEvent(chess::Milliseconds timeout)
{
    return NextEventUntil(Clock::now() + timeout);
}

std::optional<EngineEvent> Engine::PollEvent()
{
    return NextEventUntil(Clock::now());
}

std::optional<EngineEvent> Engine::NextEventUntil(Clock::time_point deadline)
{
    if (!kept_events_.empty())
    {
        EngineEvent event = std::move(kept_events_.front());
        kept_events_.pop_front();
        return event;
    }
    if (!process_ || has_told_exit_)
    {
        return std::nullopt;
    }
    auto line = process_->Output().PopUntil(deadline);
    if (line)
    {
        return ToEvent(std::move(*line));
    }
    if (process_->Output().IsDrained())
    {
        // The output ends as the process exits; give the system a moment to finish the exit, so ExitCode has it.
        process_->WaitForExit(chess::Milliseconds{1000});
        has_told_exit_ = true;
        is_searching_  = false;
        EngineEvent event;
        event.kind     = EngineEventKind::kExited;
        event.received = Clock::now();
        return event;
    }
    return std::nullopt;
}

EngineEvent Engine::ToEvent(ProcessLine line)
{
    EngineEvent event;
    event.received = line.received;
    if (line.stream == ProcessStream::kError)
    {
        if (logger_)
        {
            logger_(LineDirection::kFromEngineError, line.text);
        }
        event.kind = EngineEventKind::kErrorOutput;
        event.line = std::move(line.text);
        return event;
    }
    if (logger_)
    {
        logger_(LineDirection::kFromEngine, line.text);
    }
    const auto words = SplitWords(line.text);
    if (!words.empty() && words[0] == "info")
    {
        event.kind = EngineEventKind::kInfo;
        event.info = ParseInfoLine(line.text);
    }
    else if (!words.empty() && words[0] == "bestmove")
    {
        const auto best = ParseBestMoveLine(line.text);
        if (best)
        {
            event.kind      = EngineEventKind::kBestMove;
            event.best_move = *best;
            is_searching_   = false;
        }
    }
    else if (!words.empty() && words[0] == "readyok")
    {
        event.kind = EngineEventKind::kReadyOk;
    }
    event.line = std::move(line.text);
    return event;
}

bool Engine::Send(std::string_view line)
{
    if (!process_)
    {
        return false;
    }
    if (logger_)
    {
        logger_(LineDirection::kToEngine, line);
    }
    return process_->WriteLine(line);
}

bool Engine::IsRunning() const
{
    return process_ && process_->IsRunning();
}

std::optional<int> Engine::ExitCode() const
{
    return process_ ? process_->ExitCode() : std::nullopt;
}

void Engine::Quit()
{
    if (!process_)
    {
        return;
    }
    if (process_->IsRunning())
    {
        if (is_searching_)
        {
            Send("stop");
        }
        Send("quit");
        process_->CloseInput();
        if (!process_->WaitForExit(config_.timeouts.quit))
        {
            process_->Kill();
        }
    }
    process_.reset();
    is_searching_ = false;
}

void Engine::Kill()
{
    if (process_)
    {
        process_->Kill();
    }
}

} // namespace uci_host::uci
