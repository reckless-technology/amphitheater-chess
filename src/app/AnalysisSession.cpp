#include "AnalysisSession.h"

#include <algorithm>
#include <cctype>

namespace uci_host::app
{

namespace
{

using Clock = std::chrono::steady_clock;

/**
 * Whether an engine's configuration sets an option already.
 * @param entry The engine.
 * @param name The option's name.
 * @return Whether it is configured, compared without regard to case.
 */
bool IsConfigured(const EngineEntry &entry, const std::string &name)
{
    return std::any_of(entry.config.options.begin(), entry.config.options.end(), [&name](const auto &option) {
        return std::equal(option.first.begin(), option.first.end(), name.begin(), name.end(), [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        });
    });
}

} // namespace

EngineReaper::~EngineReaper()
{
    for (std::future<void> &quit : quitting_)
    {
        quit.wait();
    }
}

void EngineReaper::Quit(std::unique_ptr<uci::Engine> engine)
{
    if (!engine)
    {
        return;
    }
    quitting_.push_back(std::async(std::launch::async, [engine = std::move(engine)]() mutable {
        engine->Quit();
        engine.reset();
    }));
}

void EngineReaper::QuitWhenStarted(std::future<std::unique_ptr<uci::Engine>> starting)
{
    if (!starting.valid())
    {
        return;
    }
    quitting_.push_back(std::async(std::launch::async, [starting = std::move(starting)]() mutable {
        if (auto engine = starting.get())
        {
            engine->Quit();
        }
    }));
}

void EngineReaper::Later(std::function<void()> work)
{
    quitting_.push_back(std::async(std::launch::async, std::move(work)));
}

void EngineReaper::Collect()
{
    quitting_.erase(std::remove_if(quitting_.begin(), quitting_.end(),
                                   [](std::future<void> &quit) {
                                       return quit.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                                   }),
                    quitting_.end());
}

std::unique_ptr<uci::Engine> AnalysisSession::StartEngine(EngineEntry entry, int multipv,
                                                          std::shared_ptr<std::mutex>                   lock,
                                                          std::shared_ptr<std::vector<AnalysisLogLine>> log)
{
    const std::string name   = entry.DisplayName();
    auto              engine = std::make_unique<uci::Engine>(entry.config);
    engine->SetLogger([lock, log, name](uci::LineDirection direction, std::string_view line) {
        const std::lock_guard<std::mutex> guard(*lock);
        log->push_back({name, direction, std::string(line), Clock::now()});
    });
    const Status status = engine->Start();
    if (!status)
    {
        const std::lock_guard<std::mutex> guard(*lock);
        log->push_back({name, uci::LineDirection::kFromEngineError, "could not be started: " + status.ErrorMessage(),
                        Clock::now()});
        return nullptr;
    }
    if (engine->FindOption("MultiPV") != nullptr && !IsConfigured(entry, "MultiPV"))
    {
        static_cast<void>(engine->SetOption("MultiPV", std::to_string(multipv)));
    }
    if (engine->FindOption("UCI_AnalyseMode") != nullptr && !IsConfigured(entry, "UCI_AnalyseMode"))
    {
        static_cast<void>(engine->SetOption("UCI_AnalyseMode", "true"));
    }
    if (!engine->WaitReady())
    {
        return nullptr;
    }
    return engine;
}

AnalysisSession::AnalysisSession(const std::vector<EngineEntry> &engines, int multipv, EngineReaper &reaper)
    : reaper_(reaper), log_lock_(std::make_shared<std::mutex>()), log_(std::make_shared<std::vector<AnalysisLogLine>>())
{
    for (const EngineEntry &entry : engines)
    {
        auto analyser = std::make_unique<Analyser>(Analyser{entry, {}, nullptr, SearchTracker(entry.DisplayName())});
        analyser->starting =
            std::async(std::launch::async, &AnalysisSession::StartEngine, entry, multipv, log_lock_, log_);
        analysers_.push_back(std::move(analyser));
    }
}

AnalysisSession::~AnalysisSession()
{
    for (auto &analyser : analysers_)
    {
        if (analyser->engine)
        {
            reaper_.Quit(std::move(analyser->engine));
        }
        else if (analyser->starting.valid())
        {
            reaper_.QuitWhenStarted(std::move(analyser->starting));
        }
    }
}

void AnalysisSession::SetPosition(const chess::Game &game, int ply)
{
    std::string fen;
    if (!game.StartsFromStandardPosition())
    {
        fen = game.StartPosition().ToFen();
    }
    const auto               all = game.UciMoves();
    std::vector<std::string> moves(all.begin(), all.begin() + std::clamp(ply, 0, static_cast<int>(all.size())));
    if (fen == fen_ && moves == moves_ && version_ > 0)
    {
        return;
    }
    fen_   = std::move(fen);
    moves_ = std::move(moves);
    ++version_;
}

void AnalysisSession::Poll(TimePoint now)
{
    static_cast<void>(now);
    for (auto &analyser : analysers_)
    {
        Analyser &a = *analyser;
        if (a.starting.valid() && a.starting.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            a.engine = a.starting.get();
            if (!a.engine)
            {
                a.failure = "it could not be started";
            }
        }
        if (!a.engine || !a.failure.empty())
        {
            continue;
        }
        while (const auto event = a.engine->PollEvent())
        {
            if (event->kind == uci::EngineEventKind::kBestMove)
            {
                a.is_searching = false;
                a.is_stopping  = false;
            }
            else if (event->kind == uci::EngineEventKind::kExited)
            {
                a.failure      = "it quit";
                a.is_searching = false;
                break;
            }
        }
        if (!a.failure.empty() || version_ == 0 || a.searched == version_)
        {
            continue;
        }
        if (a.is_searching)
        {
            if (!a.is_stopping)
            {
                a.engine->Stop();
                a.is_stopping = true;
            }
            continue;
        }
        uci::GoParameters go;
        go.is_infinite = true;
        if (a.engine->SetPosition(fen_, moves_) && a.engine->Go(go))
        {
            a.is_searching = true;
            a.searched     = version_;
        }
        else
        {
            a.failure = "it stopped taking commands";
        }
    }

    // The lines into each engine's tracker, kept for the log.
    std::vector<AnalysisLogLine> lines;
    {
        const std::lock_guard<std::mutex> guard(*log_lock_);
        lines.swap(*log_);
    }
    for (const AnalysisLogLine &line : lines)
    {
        for (auto &analyser : analysers_)
        {
            if (analyser->tracker.name() == line.engine)
            {
                analyser->tracker.Feed(line.direction == uci::LineDirection::kToEngine, line.text, line.at);
            }
        }
        drained_.push_back(line);
    }
}

std::vector<AnalysisLogLine> AnalysisSession::DrainLog()
{
    std::vector<AnalysisLogLine> lines;
    lines.swap(drained_);
    return lines;
}

std::vector<const SearchTracker *> AnalysisSession::Trackers() const
{
    std::vector<const SearchTracker *> trackers;
    for (const auto &analyser : analysers_)
    {
        if (analyser->failure.empty())
        {
            trackers.push_back(&analyser->tracker);
        }
    }
    return trackers;
}

std::vector<std::string> AnalysisSession::Failures() const
{
    std::vector<std::string> failures;
    for (const auto &analyser : analysers_)
    {
        if (!analyser->failure.empty())
        {
            failures.push_back(analyser->entry.DisplayName() + ": " + analyser->failure);
        }
    }
    return failures;
}

std::vector<std::string> AnalysisSession::Names() const
{
    std::vector<std::string> names;
    for (const auto &analyser : analysers_)
    {
        names.push_back(analyser->entry.DisplayName());
    }
    return names;
}

} // namespace uci_host::app
