#include "CompetitionSession.h"

#include <cmath>
#include <cstdio>

namespace amphitheater::app
{

namespace
{

using Clock = std::chrono::steady_clock;

} // namespace

std::string PointsText(double points)
{
    const int  whole = static_cast<int>(std::floor(points + 1e-9));
    const bool half  = points - whole > 0.25;
    if (whole == 0 && half)
    {
        return "\xc2\xbd";
    }
    return std::to_string(whole) + (half ? "\xc2\xbd" : "");
}

CompetitionSession::CompetitionSession(match::TournamentSettings settings, bool is_match)
    : settings_(std::move(settings)), is_match_(is_match), log_lock_(std::make_shared<std::mutex>()),
      log_(std::make_shared<std::vector<CompetitionLogLine>>())
{
    settings_.does_report_moves = true;
    auto lock                   = log_lock_;
    auto log                    = log_;
    auto previous               = settings_.engine_logger;
    settings_.engine_logger     = [lock, log, previous](int game, chess::Color side, const std::string &engine,
                                                        uci::LineDirection direction, std::string_view line) {
        if (previous)
        {
            previous(game, side, engine, direction, line);
        }
        const std::lock_guard<std::mutex> guard(*lock);
        log->push_back({game, engine, side, direction, std::string(line), Clock::now()});
    };
    tournament_         = std::make_unique<match::Tournament>(settings_);
    const Status status = tournament_->Start();
    if (!status)
    {
        start_error_ = status.ErrorMessage();
        is_finished_ = true;
    }
}

CompetitionSession::~CompetitionSession()
{
    tournament_.reset(); // stops play and waits for the workers
}

int CompetitionSession::total_games() const
{
    return tournament_->TotalGames();
}

bool CompetitionSession::Poll()
{
    if (!start_error_.empty())
    {
        return false;
    }
    bool is_changed = false;
    while (auto event = tournament_->Events().TryPop())
    {
        is_changed = true;
        switch (event->kind)
        {
        case match::TournamentEvent::Kind::kGameStarted: {
            LiveGame &live    = live_[event->game_number];
            live.number       = event->game_number;
            live.white_name   = event->white_name;
            live.black_name   = event->black_name;
            live.turn_started = Clock::now();
            break;
        }
        case match::TournamentEvent::Kind::kMoveMade: {
            LiveGame &live = live_[event->game_number];
            live.number    = event->game_number;
            if (live.white_name.empty())
            {
                live.white_name = event->white_name;
                live.black_name = event->black_name;
            }
            live.game = event->game;
            if (event->note)
            {
                live.notes.push_back(*event->note);
            }
            live.white_remaining = event->white_remaining;
            live.black_remaining = event->black_remaining;
            live.turn_started    = Clock::now();
            break;
        }
        case match::TournamentEvent::Kind::kGameFinished:
            live_.erase(event->game_number);
            if (event->record)
            {
                finished_.push_back(event->record);
            }
            break;
        case match::TournamentEvent::Kind::kWarning: {
            warnings_.push_back(event->message);
            const std::lock_guard<std::mutex> guard(*log_lock_);
            log_->push_back(
                {0, "Tournament", chess::kWhite, uci::LineDirection::kFromEngineError, event->message, Clock::now()});
            break;
        }
        case match::TournamentEvent::Kind::kFinished:
            is_finished_ = true;
            live_.clear();
            break;
        }
    }
    return is_changed;
}

void CompetitionSession::Stop()
{
    if (tournament_)
    {
        tournament_->RequestStop();
    }
}

std::vector<std::string> CompetitionSession::EngineNames() const
{
    return tournament_->EngineNames();
}

std::vector<match::GameResultEntry> CompetitionSession::Results() const
{
    return tournament_->Results();
}

match::ScoreLine CompetitionSession::FirstEngineScore() const
{
    match::ScoreLine score;
    for (const match::GameResultEntry &result : Results())
    {
        const bool first_is_white = result.white == 0;
        switch (result.result)
        {
        case chess::GameResult::kWhiteWins:
            ++(first_is_white ? score.wins : score.losses);
            break;
        case chess::GameResult::kBlackWins:
            ++(first_is_white ? score.losses : score.wins);
            break;
        case chess::GameResult::kDraw:
            ++score.draws;
            break;
        case chess::GameResult::kNone:
            break;
        }
    }
    return score;
}

std::vector<CompetitionLogLine> CompetitionSession::DrainLog()
{
    const std::lock_guard<std::mutex> guard(*log_lock_);
    std::vector<CompetitionLogLine>   lines;
    lines.swap(*log_);
    return lines;
}

std::string CompetitionSession::ScoreText() const
{
    const auto names   = EngineNames();
    const auto results = Results();
    if (results.empty() || names.size() < 2)
    {
        return {};
    }
    if (is_match_)
    {
        const match::ScoreLine score = FirstEngineScore();
        const auto             elo   = match::EstimateElo(score);
        std::string            text  = names[0] + " " + PointsText(score.Points()) + " - " +
                                       PointsText(score.Games() - score.Points()) + " " + names[1];
        char                   detail[96];
        if (elo.difference && elo.error_margin)
        {
            std::snprintf(detail, sizeof detail, " (Elo %+.0f \xc2\xb1 %.0f, LOS %.0f %%)", *elo.difference,
                          *elo.error_margin, 100.0 * elo.likelihood_of_superiority);
        }
        else
        {
            std::snprintf(detail, sizeof detail, " (LOS %.0f %%)", 100.0 * elo.likelihood_of_superiority);
        }
        return text + detail;
    }
    const auto standings = match::ComputeStandings(names, results);
    return standings.front().name + " leads with " + PointsText(standings.front().score.Points()) + " of " +
           std::to_string(standings.front().score.Games());
}

} // namespace amphitheater::app
