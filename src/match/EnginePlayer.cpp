#include "EnginePlayer.h"

#include "chess/Notation.h"

namespace uci_host::match
{

EnginePlayer::EnginePlayer(uci::EngineConfig config, chess::TimeControl time_control)
    : config_(std::move(config)), time_control_(std::move(time_control))
{
}

std::string EnginePlayer::Name() const
{
    if (engine_ && !engine_->Name().empty())
    {
        return engine_->Name();
    }
    return config_.name.empty() ? config_.executable : config_.name;
}

void EnginePlayer::SetLogger(uci::EngineLogger logger)
{
    logger_ = std::move(logger);
    if (engine_)
    {
        engine_->SetLogger(logger_);
    }
}

std::vector<std::string> EnginePlayer::Warnings() const
{
    return engine_ ? engine_->Warnings() : std::vector<std::string>{};
}

Status EnginePlayer::StartGame(const chess::Game &game)
{
    static_cast<void>(game);
    if (engine_ && (!is_healthy_ || !engine_->IsRunning()))
    {
        engine_.reset(); // Quits it, killing it if it does not go.
    }
    if (!engine_)
    {
        engine_ = std::make_unique<uci::Engine>(config_);
        engine_->SetLogger(logger_);
        const auto started = engine_->Start();
        if (!started)
        {
            engine_.reset();
            return started;
        }
    }
    const auto ready = engine_->NewGame();
    is_healthy_      = ready.HasValue();
    return ready;
}

uci::GoParameters EnginePlayer::GoFor(const MoveRequest &request, const chess::TimeControl &time_control)
{
    uci::GoParameters parameters;
    const auto       &own =
        request.game.CurrentPosition().SideToMove() == chess::kWhite ? request.white_clock : request.black_clock;
    if (!request.white_clock.Control().sessions.empty() || !request.black_clock.Control().sessions.empty())
    {
        parameters.white_time      = request.white_clock.Remaining();
        parameters.black_time      = request.black_clock.Remaining();
        parameters.white_increment = request.white_clock.Increment();
        parameters.black_increment = request.black_clock.Increment();
        if (own.MovesToGo() > 0)
        {
            parameters.moves_to_go = own.MovesToGo();
        }
    }
    parameters.move_time = time_control.move_time;
    parameters.depth     = time_control.depth;
    parameters.nodes     = time_control.nodes;
    return parameters;
}

MoveReply EnginePlayer::RequestMove(const MoveRequest &request)
{
    MoveReply reply;
    if (!engine_ || !engine_->IsRunning())
    {
        reply.status = MoveReply::Status::kDisconnected;
        reply.detail = "the engine is not running";
        is_healthy_  = false;
        return reply;
    }
    const auto &game = request.game;
    const auto  fen  = game.StartsFromStandardPosition() ? std::string{} : game.StartPosition().ToFen();
    engine_->SetPosition(fen, game.UciMoves());
    const auto result = engine_->Search(GoFor(request, time_control_), request.deadline);
    reply.info        = result.info;
    reply.elapsed     = result.elapsed;
    switch (result.ending)
    {
    case uci::SearchResult::Ending::kBestMove:
        reply.status = MoveReply::Status::kMove;
        reply.move   = result.best_move.move;
        break;
    case uci::SearchResult::Ending::kTimeout:
        reply.status = MoveReply::Status::kTimeout;
        is_healthy_  = false; // A late answer would muddle the next search: the engine is started afresh.
        break;
    case uci::SearchResult::Ending::kExited: {
        reply.status    = MoveReply::Status::kDisconnected;
        const auto code = engine_->ExitCode();
        reply.detail    = code ? "exit code " + std::to_string(*code) : "the engine stopped answering";
        is_healthy_     = false;
        break;
    }
    }
    return reply;
}

} // namespace uci_host::match
