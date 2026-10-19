#include "Tournament.h"

#include "EnginePlayer.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>

namespace uci_host::match
{

Tournament::Tournament(TournamentSettings settings) : settings_(std::move(settings))
{
    for (const auto &engine : settings_.engines)
    {
        names_.push_back(engine.config.name.empty() ? engine.config.executable : engine.config.name);
    }
}

Tournament::~Tournament()
{
    RequestStop();
    Wait();
}

Status Tournament::Start()
{
    if (!workers_.empty())
    {
        return Error{"the tournament has already started"};
    }
    if (settings_.engines.size() < 2)
    {
        return Error{"a tournament needs at least two engines"};
    }
    settings_.schedule.engines = static_cast<int>(settings_.engines.size());
    schedule_                  = MakeSchedule(settings_.schedule);
    if (schedule_.empty())
    {
        return Error{"the schedule holds no games (rounds and games must be at least 1)"};
    }
    const int workers = std::max(1, std::min(settings_.concurrency, static_cast<int>(schedule_.size())));
    working_          = workers;
    for (int index = 0; index < workers; ++index)
    {
        workers_.emplace_back([this] { Work(); });
    }
    return Ok{};
}

void Tournament::RequestStop()
{
    is_stopping_ = true;
}

void Tournament::Wait()
{
    for (auto &worker : workers_)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}

std::vector<std::string> Tournament::EngineNames() const
{
    std::lock_guard lock(mutex_);
    return names_;
}

std::vector<GameResultEntry> Tournament::Results() const
{
    std::lock_guard lock(mutex_);
    return results_;
}

void Tournament::Work()
{
    std::map<int, std::unique_ptr<EnginePlayer>> players;
    std::map<int, bool>                          has_warned;
    const auto                                   player_for = [&](int engine, int game_number) {
        auto &player = players[engine];
        if (!player)
        {
            const auto &entry = settings_.engines[engine];
            player = std::make_unique<EnginePlayer>(entry.config, entry.time_control.value_or(settings_.time_control));
        }
        if (settings_.engine_logger)
        {
            EnginePlayer *self = player.get();
            player->SetLogger([this, game_number, self](uci::LineDirection direction, std::string_view line) {
                settings_.engine_logger(game_number, self->Name(), direction, line);
            });
        }
        return player.get();
    };

    while (!is_stopping_)
    {
        const int index = next_game_++;
        if (index >= static_cast<int>(schedule_.size()))
        {
            break;
        }
        const ScheduledGame &scheduled = schedule_[index];
        EnginePlayer        &white     = *player_for(scheduled.white, scheduled.number);
        EnginePlayer        &black     = *player_for(scheduled.black, scheduled.number);

        GameSettings game;
        if (!settings_.openings.empty())
        {
            game.opening = settings_.openings[static_cast<std::size_t>(scheduled.opening) % settings_.openings.size()];
        }
        game.white_time_control = settings_.engines[scheduled.white].time_control.value_or(settings_.time_control);
        game.black_time_control = settings_.engines[scheduled.black].time_control.value_or(settings_.time_control);
        game.time_margin        = settings_.time_margin;
        game.stall_timeout      = settings_.stall_timeout;
        game.adjudication       = settings_.adjudication;
        game.event              = settings_.event;
        game.site               = settings_.site;
        game.round              = std::to_string(scheduled.number);

        TournamentEvent started;
        started.kind        = TournamentEvent::Kind::kGameStarted;
        started.game_number = scheduled.number;
        started.total_games = TotalGames();
        started.white       = scheduled.white;
        started.black       = scheduled.black;
        {
            std::lock_guard lock(mutex_);
            started.white_name = names_[scheduled.white];
            started.black_name = names_[scheduled.black];
        }
        events_.Push(started);

        GameRunner runner(white, black, game);
        auto       record = std::make_shared<GameRecord>(runner.Play({}, &is_stopping_));

        for (const auto &[engine, player] : {std::pair{scheduled.white, &white}, std::pair{scheduled.black, &black}})
        {
            const auto warnings = player->Warnings();
            if (!warnings.empty() && !has_warned[engine])
            {
                has_warned[engine] = true;
                for (const auto &warning : warnings)
                {
                    TournamentEvent event;
                    event.kind    = TournamentEvent::Kind::kWarning;
                    event.message = player->Name() + ": " + warning;
                    events_.Push(event);
                }
            }
        }

        TournamentEvent finished = started;
        finished.kind            = TournamentEvent::Kind::kGameFinished;
        finished.white_name      = record->white_name;
        finished.black_name      = record->black_name;
        finished.outcome         = record->game.GetOutcome();
        finished.record          = record;
        std::string pgn_error;
        {
            std::lock_guard lock(mutex_);
            names_[scheduled.white] = record->white_name;
            names_[scheduled.black] = record->black_name;
            results_.push_back({scheduled.white, scheduled.black, finished.outcome.result});
            if (!settings_.pgn_path.empty())
            {
                const std::filesystem::path path(std::u8string(settings_.pgn_path.begin(), settings_.pgn_path.end()));
                std::ofstream               file(path, std::ios::app | std::ios::binary);
                file << chess::WritePgn(record->ToPgn());
                if (!file)
                {
                    pgn_error = "cannot write to '" + settings_.pgn_path + "'";
                }
            }
        }
        events_.Push(finished);
        if (!pgn_error.empty())
        {
            TournamentEvent warning;
            warning.kind    = TournamentEvent::Kind::kWarning;
            warning.message = pgn_error;
            events_.Push(warning);
        }
    }
    players.clear(); // Quits this worker's engines.
    if (--working_ == 0)
    {
        TournamentEvent finished;
        finished.kind        = TournamentEvent::Kind::kFinished;
        finished.total_games = TotalGames();
        events_.Push(finished);
        events_.Close();
    }
}

} // namespace uci_host::match
