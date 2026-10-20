#include "GameAnalysis.h"

#include "chess/Notation.h"
#include "uci/Engine.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace amphitheater::match
{

namespace
{

/** How long past a search's own time the analysis waits for its answer before it gives the engine up. */
constexpr std::chrono::seconds kGrace{10};

/** How long a depth-limited search is waited for. */
constexpr std::chrono::minutes kDepthWait{10};

} // namespace

double WhiteWinningChances(const uci::Score &score, chess::Color side_to_move)
{
    double chances = 0.0;
    if (score.kind == uci::Score::Kind::kMate)
    {
        // A mate in n for the side to move wins; mated (a mate in 0, or a negative one) loses.
        chances = score.value > 0 ? 1.0 : -1.0;
    }
    else
    {
        chances = 2.0 / (1.0 + std::exp(-0.00368208 * static_cast<double>(score.value))) - 1.0;
    }
    return side_to_move == chess::kWhite ? chances : -chances;
}

std::optional<MoveQuality> JudgeMove(const PositionAnalysis &before, const PositionAnalysis &after)
{
    if (!before.score || !after.score)
    {
        return std::nullopt;
    }
    const double sign = before.side_to_move == chess::kWhite ? 1.0 : -1.0;
    const double lost = sign * (WhiteWinningChances(*before.score, before.side_to_move) -
                                WhiteWinningChances(*after.score, after.side_to_move));
    if (lost > 0.3)
    {
        return MoveQuality::kBlunder;
    }
    if (lost > 0.2)
    {
        return MoveQuality::kMistake;
    }
    if (lost > 0.1)
    {
        return MoveQuality::kInaccuracy;
    }
    return MoveQuality::kGood;
}

int NagOf(MoveQuality quality)
{
    switch (quality)
    {
    case MoveQuality::kInaccuracy:
        return 6;
    case MoveQuality::kMistake:
        return 2;
    case MoveQuality::kBlunder:
        return 4;
    case MoveQuality::kGood:
        break;
    }
    return 0;
}

GameAnalyser::GameAnalyser(GameAnalysisSettings settings, chess::Position start, std::vector<std::string> moves)
    : settings_(std::move(settings)), start_(start), moves_(std::move(moves))
{
}

GameAnalyser::~GameAnalyser()
{
    Stop();
    if (thread_.joinable())
    {
        thread_.join();
    }
}

void GameAnalyser::Start()
{
    if (!thread_.joinable())
    {
        thread_ = std::thread([this] { Run(); });
    }
}

void GameAnalyser::Stop()
{
    is_stopping_ = true;
}

std::optional<PositionAnalysis> GameAnalyser::Next()
{
    return results_.TryPop();
}

std::string GameAnalyser::Failure() const
{
    std::lock_guard lock(mutex_);
    return failure_;
}

std::string GameAnalyser::EngineName() const
{
    std::lock_guard lock(mutex_);
    return engine_name_;
}

void GameAnalyser::Fail(std::string failure)
{
    std::lock_guard lock(mutex_);
    failure_ = std::move(failure);
}

void GameAnalyser::Run()
{
    uci::Engine engine(settings_.engine);
    if (const auto started = engine.Start(); !started)
    {
        Fail("the engine could not be started: " + started.ErrorMessage());
        is_finished_ = true;
        return;
    }
    {
        std::lock_guard lock(mutex_);
        engine_name_ = engine.Name();
    }
    static_cast<void>(engine.NewGame());
    const std::string fen      = start_.ToFen();
    chess::Position   position = start_;
    for (int ply = 0; ply <= static_cast<int>(moves_.size()) && !is_stopping_; ++ply)
    {
        if (ply > 0)
        {
            const auto move = chess::ParseUciMove(position, moves_[static_cast<std::size_t>(ply - 1)]);
            if (!move)
            {
                break; // the moves were legal when given: nothing more to search
            }
            position = position.MakeMove(*move);
        }
        PositionAnalysis analysis;
        analysis.ply          = ply;
        analysis.side_to_move = position.SideToMove();
        // A position the game ends in is scored by the rules: the side to move mated, or a draw.
        if (position.GenerateLegalMoves().size() == 0)
        {
            uci::Score score;
            if (position.IsInCheck())
            {
                score.kind  = uci::Score::Kind::kMate;
                score.value = 0;
            }
            analysis.score = score;
            results_.Push(std::move(analysis));
            continue;
        }
        const std::vector<std::string> played(moves_.begin(), moves_.begin() + ply);
        uci::GoParameters              go;
        go.move_time       = settings_.move_time;
        go.depth           = settings_.depth;
        const auto waiting = settings_.move_time ? *settings_.move_time + kGrace
                                                 : std::chrono::duration_cast<chess::Milliseconds>(kDepthWait);
        if (!engine.SetPosition(fen, played))
        {
            Fail("the engine stopped taking commands");
            break;
        }
        const uci::SearchResult result = engine.Search(go, std::chrono::steady_clock::now() + waiting);
        if (result.ending != uci::SearchResult::Ending::kBestMove)
        {
            Fail(result.ending == uci::SearchResult::Ending::kExited ? "the engine quit"
                                                                     : "the engine did not answer in time");
            break;
        }
        analysis.score = result.info.score;
        analysis.depth = result.info.depth.value_or(0);
        analysis.pv    = result.info.pv;
        if (analysis.pv.empty() && !result.best_move.move.empty())
        {
            analysis.pv = {result.best_move.move};
        }
        results_.Push(std::move(analysis));
    }
    engine.Quit();
    is_finished_ = true;
}

} // namespace amphitheater::match
