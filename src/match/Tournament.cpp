#include "Tournament.h"

#include "EnginePlayer.h"
#include "Openings.h"

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
    const auto                                   player_for = [&](int engine, int game_number, chess::Color side) {
        auto &player = players[engine];
        if (!player)
        {
            const auto &entry = settings_.engines[engine];
            player = std::make_unique<EnginePlayer>(entry.config, entry.time_control.value_or(settings_.time_control));
        }
        if (settings_.engine_logger)
        {
            EnginePlayer *self = player.get();
            player->SetLogger([this, game_number, side, self](uci::LineDirection direction, std::string_view line) {
                settings_.engine_logger(game_number, side, self->Name(), direction, line);
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
        EnginePlayer        &white     = *player_for(scheduled.white, scheduled.number, chess::kWhite);
        EnginePlayer        &black     = *player_for(scheduled.black, scheduled.number, chess::kBlack);

        GameSettings game;
        if (settings_.opening_book)
        {
            // One line per opening number, so the two games of a repeated opening play the same one.
            game.opening =
                PolyglotOpening(*settings_.opening_book, settings_.opening_book_plies,
                                settings_.seed * 0x9E3779B97F4A7C15ULL + static_cast<std::uint64_t>(scheduled.opening));
        }
        else if (!settings_.openings.empty())
        {
            game.opening = settings_.openings[static_cast<std::size_t>(scheduled.opening) % settings_.openings.size()];
        }
        for (const auto &[engine, book] :
             {std::pair{scheduled.white, &game.white_book}, std::pair{scheduled.black, &game.black_book}})
        {
            book->book  = settings_.engines[engine].book;
            book->depth = settings_.engines[engine].book_depth;
            book->seed  = settings_.seed * 0x9E3779B97F4A7C15ULL + static_cast<std::uint64_t>(scheduled.number) * 2 +
                          (book == &game.white_book ? 0 : 1);
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
        const auto on_move = [&](const GameProgress &progress) {
            TournamentEvent moved = started;
            moved.kind            = TournamentEvent::Kind::kMoveMade;
            moved.game            = std::make_shared<const chess::Game>(progress.game);
            moved.note            = std::make_shared<const MoveNote>(progress.note);
            moved.white_remaining = progress.white_clock.Remaining();
            moved.black_remaining = progress.black_clock.Remaining();
            events_.Push(std::move(moved));
        };
        auto record = std::make_shared<GameRecord>(runner.Play(settings_.does_report_moves
                                                                   ? std::function<void(const GameProgress &)>(on_move)
                                                                   : std::function<void(const GameProgress &)>(),
                                                               &is_stopping_));

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
