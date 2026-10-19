#include "MainController.h"

#include "Commands.h"

#include <algorithm>

namespace uci_host::app
{

MainController::MainController(MainView &view) : view_(view)
{
    view_.on_command    = [this](int command) { Command(command); };
    view_.on_select_ply = [this](int ply) { ShowPly(ply); };
    view_.SetMenus(BuildMenus([this] { return Menus(); }));
    Refresh();
}

void MainController::ShowGame(chess::Game game, GameInfo info)
{
    game_     = std::move(game);
    info_     = std::move(info);
    view_ply_ = game_.PlyCount();
    Refresh();
}

void MainController::ShowPly(int ply)
{
    view_ply_ = std::clamp(ply, 0, game_.PlyCount());
    Refresh();
}

void MainController::Command(int command)
{
    switch (command)
    {
    case kFirst:
        ShowPly(0);
        break;
    case kBack:
        ShowPly(view_ply_ - 1);
        break;
    case kForward:
        ShowPly(view_ply_ + 1);
        break;
    case kLast:
        ShowPly(game_.PlyCount());
        break;
    case kFlip:
        is_flipped_ = !is_flipped_;
        view_.SetFlipped(is_flipped_);
        break;
    case kClearLog:
        view_.log().Clear();
        break;
    default:
        break;
    }
}

void MainController::Frame(double seconds)
{
    static_cast<void>(seconds);
}

MenuState MainController::Menus() const
{
    MenuState state;
    state.can_step_back    = view_ply_ > 0;
    state.can_step_forward = view_ply_ < game_.PlyCount();
    return state;
}

void MainController::Refresh()
{
    const chess::Position     &position = game_.PositionAt(view_ply_);
    std::optional<chess::Move> last;
    if (view_ply_ > 0)
    {
        last = game_.Moves()[static_cast<std::size_t>(view_ply_ - 1)];
    }
    view_.SetBoard(position, last, false);
    view_.SetFlipped(is_flipped_);
    const chess::Position &start = game_.StartPosition();
    view_.SetMoves(game_.SanMoves(), start.FullmoveNumber(), start.SideToMove() == chess::kBlack, view_ply_);

    PlayerStripState white;
    white.is_white = true;
    white.name     = info_.white;
    white.details  = "White";
    PlayerStripState black;
    black.is_white = false;
    black.name     = info_.black;
    black.details  = "Black";
    view_.SetPlayers(white, black);

    std::vector<CardLine> card;
    if (!info_.event.empty())
    {
        card.push_back({"Event", info_.event});
    }
    if (!info_.opening.empty())
    {
        card.push_back({"Opening", info_.opening});
    }
    if (!info_.time_control.empty())
    {
        card.push_back({"Time control", info_.time_control});
    }
    card.push_back({"FEN", position.ToFen(), true});
    view_.SetCard(std::move(card));

    std::vector<std::string> status;
    status.push_back(position.SideToMove() == chess::kWhite ? "White to move" : "Black to move");
    if (game_.IsOver())
    {
        status.push_back(chess::ResultText(game_.GetOutcome().result) + " " + game_.GetOutcome().description);
    }
    view_.SetStatus(std::move(status));
}

} // namespace uci_host::app
