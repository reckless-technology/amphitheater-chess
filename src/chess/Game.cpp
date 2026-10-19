#include "Game.h"

#include "Notation.h"

#include <algorithm>

namespace uci_host::chess
{

std::string ResultText(GameResult result)
{
    switch (result)
    {
    case GameResult::kWhiteWins:
        return "1-0";
    case GameResult::kBlackWins:
        return "0-1";
    case GameResult::kDraw:
        return "1/2-1/2";
    default:
        return "*";
    }
}

GameResult ParseResultText(std::string_view text)
{
    if (text == "1-0")
    {
        return GameResult::kWhiteWins;
    }
    if (text == "0-1")
    {
        return GameResult::kBlackWins;
    }
    if (text == "1/2-1/2")
    {
        return GameResult::kDraw;
    }
    return GameResult::kNone;
}

std::string TerminationTag(Termination termination)
{
    switch (termination)
    {
    case Termination::kNone:
        return "unterminated";
    case Termination::kTimeForfeit:
        return "time forfeit";
    case Termination::kIllegalMove:
        return "rules infraction";
    case Termination::kAdjudication:
    case Termination::kResignation:
        return "adjudication";
    case Termination::kDisconnection:
    case Termination::kStalled:
    case Termination::kAbandoned:
        return "abandoned";
    default:
        return "normal";
    }
}

Game::Game() : Game(Position::Start())
{
}

Game::Game(const Position &start)
{
    positions_.push_back(start);
    outcome_ = RulesOutcome();
}

bool Game::StartsFromStandardPosition() const
{
    return StartPosition() == Position::Start();
}

bool Game::Play(Move move)
{
    if (IsOver())
    {
        return false;
    }
    bool is_legal = false;
    for (const Move &legal : CurrentPosition().GenerateLegalMoves())
    {
        is_legal |= legal == move;
    }
    if (!is_legal)
    {
        return false;
    }
    positions_.push_back(CurrentPosition().MakeMove(move));
    moves_.push_back(move);
    outcome_ = RulesOutcome();
    return true;
}

Result<Move> Game::PlayUci(std::string_view text)
{
    if (IsOver())
    {
        return Error{"the game is over"};
    }
    auto move = ParseUciMove(CurrentPosition(), text);
    if (move)
    {
        Play(*move);
    }
    return move;
}

Result<Move> Game::PlaySan(std::string_view text)
{
    if (IsOver())
    {
        return Error{"the game is over"};
    }
    auto move = ParseSan(CurrentPosition(), text);
    if (move)
    {
        Play(*move);
    }
    return move;
}

bool Game::Undo()
{
    if (moves_.empty())
    {
        return false;
    }
    moves_.pop_back();
    positions_.pop_back();
    outcome_ = RulesOutcome();
    return true;
}

std::string Game::San(int ply) const
{
    return MoveToSan(positions_[ply], moves_[ply]);
}

std::vector<std::string> Game::SanMoves() const
{
    std::vector<std::string> moves;
    for (int ply = 0; ply < PlyCount(); ++ply)
    {
        moves.push_back(San(ply));
    }
    return moves;
}

std::vector<std::string> Game::UciMoves() const
{
    std::vector<std::string> moves;
    for (const Move &move : moves_)
    {
        moves.push_back(MoveToUci(move));
    }
    return moves;
}

int Game::RepetitionCount() const
{
    // A repetition can only reach back as far as the last capture or pawn move, and only to positions with the
    // same side to move.
    const Position &current = CurrentPosition();
    const auto      key     = current.RepetitionKey();
    const int       last    = static_cast<int>(positions_.size()) - 1;
    const int       reach   = std::min(current.HalfmoveClock(), last);
    int             count   = 1;
    for (int back = 2; back <= reach; back += 2)
    {
        if (positions_[last - back].RepetitionKey() == key)
        {
            ++count;
        }
    }
    return count;
}

Outcome Game::RulesOutcome() const
{
    const Position &position = CurrentPosition();
    const bool      is_white = position.SideToMove() == kWhite;
    if (position.GenerateLegalMoves().size() == 0)
    {
        if (position.IsInCheck())
        {
            return {is_white ? GameResult::kBlackWins : GameResult::kWhiteWins, Termination::kCheckmate,
                    is_white ? "Black mates" : "White mates"};
        }
        return {GameResult::kDraw, Termination::kStalemate, "Draw by stalemate"};
    }
    if (position.HasInsufficientMaterial())
    {
        return {GameResult::kDraw, Termination::kInsufficientMaterial, "Draw by insufficient mating material"};
    }
    if (RepetitionCount() >= 3)
    {
        return {GameResult::kDraw, Termination::kThreefoldRepetition, "Draw by 3-fold repetition"};
    }
    if (position.HalfmoveClock() >= 100)
    {
        return {GameResult::kDraw, Termination::kFiftyMoves, "Draw by fifty moves rule"};
    }
    return {};
}

} // namespace uci_host::chess
