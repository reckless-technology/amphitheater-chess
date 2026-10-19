#include "EnginePlayer.h"

#include "chess/Notation.h"

#include <algorithm>

namespace uci_host::match
{

namespace
{

using Clock = std::chrono::steady_clock;

/** How long a game's end waits for a pondering engine to answer "stop" before it is written off. */
constexpr chess::Milliseconds kStopAtEndTimeout{2000};

/**
 * The "go" for one side's search.
 * @param request The game and the clocks.
 * @param time_control The engine's time control.
 * @param side The side the engine plays: movestogo is its clock's.
 * @return The parameters.
 */
uci::GoParameters GoForSide(const MoveRequest &request, const chess::TimeControl &time_control, chess::Color side)
{
    uci::GoParameters parameters;
    const auto       &own = side == chess::kWhite ? request.white_clock : request.black_clock;
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

} // namespace

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
    expected_reply_.clear();
    pondering_on_.clear();
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
    return GoForSide(request, time_control, request.game.CurrentPosition().SideToMove());
}

MoveReply EnginePlayer::ReplyFrom(const uci::SearchResult &result)
{
    MoveReply reply;
    reply.info    = result.info;
    reply.elapsed = result.elapsed;
    switch (result.ending)
    {
    case uci::SearchResult::Ending::kBestMove:
        reply.status      = MoveReply::Status::kMove;
        reply.move        = result.best_move.move;
        reply.ponder_move = result.best_move.ponder.value_or("");
        expected_reply_   = config_.does_ponder ? reply.ponder_move : std::string{};
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

bool EnginePlayer::StopPondering(Clock::time_point deadline)
{
    pondering_on_.clear();
    engine_->Stop();
    return engine_->AwaitBestMove(Clock::now(), deadline).ending == uci::SearchResult::Ending::kBestMove;
}

MoveReply EnginePlayer::RequestMove(const MoveRequest &request)
{
    const auto start = Clock::now();
    if (!engine_ || !engine_->IsRunning())
    {
        MoveReply reply;
        reply.status = MoveReply::Status::kDisconnected;
        reply.detail = "the engine is not running";
        is_healthy_  = false;
        pondering_on_.clear();
        return reply;
    }
    const auto &game = request.game;
    expected_reply_.clear();
    chess::Milliseconds stop_wait{0};
    if (!pondering_on_.empty())
    {
        const std::string played = game.Moves().empty() ? std::string{} : chess::MoveToUci(game.Moves().back());
        if (played == pondering_on_)
        {
            // The expected move: the search goes on as a real one, and the engine's clock starts now.
            pondering_on_.clear();
            engine_->PonderHit();
            return ReplyFrom(engine_->AwaitBestMove(Clock::now(), request.deadline));
        }
        // Another move: the ponder search is stopped and its answer thrown away, on the engine's own time.
        const bool is_answered = StopPondering(request.deadline);
        stop_wait              = std::chrono::duration_cast<chess::Milliseconds>(Clock::now() - start);
        if (!is_answered)
        {
            MoveReply reply;
            reply.status  = engine_->IsRunning() ? MoveReply::Status::kTimeout : MoveReply::Status::kDisconnected;
            reply.elapsed = stop_wait;
            reply.detail  = "the engine did not answer 'stop' after pondering";
            is_healthy_   = false;
            return reply;
        }
    }
    const auto fen = game.StartsFromStandardPosition() ? std::string{} : game.StartPosition().ToFen();
    engine_->SetPosition(fen, game.UciMoves());
    auto parameters = GoFor(request, time_control_);
    if (stop_wait.count() > 0)
    {
        auto &own =
            game.CurrentPosition().SideToMove() == chess::kWhite ? parameters.white_time : parameters.black_time;
        if (own)
        {
            *own = std::max(chess::Milliseconds{0}, *own - stop_wait);
        }
    }
    MoveReply reply = ReplyFrom(engine_->Search(parameters, request.deadline));
    reply.elapsed += stop_wait;
    return reply;
}

void EnginePlayer::MoveMade(const MoveRequest &state)
{
    // expected_reply_ is set only by this engine's own answer, and the first MoveMade after it is that answer's
    // move: the engine now ponders on the reply it expects, while the opponent thinks.
    const std::string expected = std::move(expected_reply_);
    expected_reply_.clear();
    if (expected.empty() || !config_.does_ponder || !engine_ || !engine_->IsRunning() || !pondering_on_.empty())
    {
        return;
    }
    const auto &game  = state.game;
    const auto  reply = chess::ParseUciMove(game.CurrentPosition(), expected);
    if (!reply)
    {
        return; // A ponder move that is not legal here: no pondering.
    }
    std::vector<std::string> moves = game.UciMoves();
    moves.push_back(chess::MoveToUci(*reply));
    const auto fen = game.StartsFromStandardPosition() ? std::string{} : game.StartPosition().ToFen();
    engine_->SetPosition(fen, moves);
    auto parameters      = GoForSide(state, time_control_, chess::EnemyOf(game.CurrentPosition().SideToMove()));
    parameters.is_ponder = true;
    if (engine_->Go(parameters))
    {
        pondering_on_ = moves.back();
    }
}

void EnginePlayer::EndGame(const chess::Game &game)
{
    static_cast<void>(game);
    expected_reply_.clear();
    if (!pondering_on_.empty() && engine_)
    {
        // The game ended while the engine pondered: stop it, and start it afresh next game if it will not stop.
        if (!StopPondering(Clock::now() + kStopAtEndTimeout))
        {
            is_healthy_ = false;
        }
    }
    pondering_on_.clear();
}

} // namespace uci_host::match
