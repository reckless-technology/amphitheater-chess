#include "MainView.h"

#include "Commands.h"
#include "Theme.h"

#include "PanelStyle.h"

#include <algorithm>
#include <cmath>

namespace amphitheater::app
{

MainView::MainView(const PieceSet &pieces)
    : board_(pieces), moves_label_("Moves"), card_label_("Game"), analysis_label_("Analysis"),
      graph_label_("Evaluation")
{
    SetOpaque(true);
    const auto painter = theme::PlatePainter();

    const struct
    {
        int         command;
        const char *text;
        int         width;
        bool        is_toggle;
    } plates[] = {{kFirst, "|<", 46, false}, {kBack, "<", 40, false},    {kForward, ">", 40, false},
                  {kLast, ">|", 46, false},  {kFlip, "Flip", 64, false}, {kAnalyse, "Analyse", 108, true}};

    for (const auto &plate : plates)
    {
        const int command = plate.command;
        nav_.push_back({command, plate.width,
                        std::make_unique<reckless::view::Button>(
                            plate.text, painter, [this, command] { Tell(command); }, plate.is_toggle)});
    }
    nav_[0].button->tooltip = "The start of the game (Home).";
    nav_[1].button->tooltip = "Back a move (Left).";
    nav_[2].button->tooltip = "Forward a move (Right).";
    nav_[3].button->tooltip = "The last move (End).";
    nav_[4].button->tooltip = "Turn the board round (F).";
    nav_[5].button->tooltip = "The selected engines analyse the position shown.";

    AddChild(top_strip_);
    AddChild(board_);
    AddChild(bottom_strip_);
    for (NavPlate &plate : nav_)
    {
        AddChild(*plate.button);
    }
    AddChild(moves_label_);
    AddChild(moves_.view());
    AddChild(card_label_);
    AddChild(card_);
    AddChild(analysis_label_);
    AddChild(analysis_);
    AddChild(graph_label_);
    AddChild(graph_);
    AddChild(log_header_);
    AddChild(log_.view());
    AddChild(status_);

    log_header_.on_toggle     = [this] { ToggleLog(); };
    log_header_.on_drag_start = [this] { drag_from_ = log_.view().height(); };
    log_header_.on_drag       = [this](float travel) {
        // Dragged up, the log's top edge rises with the pointer: the log grows by the travel.
        const int height = std::clamp(drag_from_ - static_cast<int>(std::lround(travel)), kLogLeastHeight,
                                      std::max(kLogLeastHeight, LogMostHeight()));
        if (height != log_height_)
        {
            log_height_ = height;
            Resized();
            Repaint();
        }
    };
    log_header_.on_drag_end = [this] {
        if (on_log_layout)
        {
            on_log_layout();
        }
    };

    board_.on_move = [this](chess::Move move) {
        if (on_move)
        {
            on_move(move);
        }
    };
    moves_.on_select = [this](int ply) {
        if (on_select_ply)
        {
            on_select_ply(ply);
        }
    };
    graph_.on_select = [this](int ply) {
        if (on_select_ply)
        {
            on_select_ply(ply);
        }
    };
    analysis_.SetEmptyText("No engine is analysing. Choose engines under Engines, then press Analyse.");
    black_.is_white = false;
    SetBounds({kDesignWidth, kDesignHeight});
}

reckless::view::Button *MainView::Plate(int command)
{
    for (NavPlate &plate : nav_)
    {
        if (plate.command == command)
        {
            return plate.button.get();
        }
    }
    return nullptr;
}

void MainView::Tell(int command)
{
    if (on_command)
    {
        on_command(command);
    }
}

void MainView::SetBoard(const chess::Position &position, std::optional<chess::Move> last_move, bool is_interactive)
{
    board_.SetPosition(position, last_move);
    board_.SetInteractive(is_interactive);
}

void MainView::SetFlipped(bool is_flipped)
{
    board_.SetFlipped(is_flipped);
    SetPlayers(white_, black_);
}

void MainView::SetArrows(std::vector<BoardArrow> arrows)
{
    board_.SetArrows(std::move(arrows));
}

void MainView::SetPlayers(PlayerStripState white, PlayerStripState black)
{
    white_ = std::move(white);
    black_ = std::move(black);
    top_strip_.SetState(board_.flipped() ? white_ : black_);
    bottom_strip_.SetState(board_.flipped() ? black_ : white_);
}

void MainView::SetMoves(std::vector<std::string> sans, int first_number, bool is_black_first, int current_ply)
{
    moves_.SetMoves(std::move(sans), first_number, is_black_first, current_ply);
}

void MainView::SetCurrentPly(int ply)
{
    moves_.SetCurrentPly(ply);
}

void MainView::SetCard(std::vector<CardLine> lines)
{
    card_.SetLines(std::move(lines));
    Resized();
}

void MainView::SetAnalysis(std::vector<EngineBlock> blocks)
{
    analysis_.SetBlocks(std::move(blocks));
}

void MainView::SetAnalysing(bool is_analysing)
{
    if (reckless::view::Button *plate = Plate(kAnalyse))
    {
        plate->SetOn(is_analysing);
    }
}

void MainView::SetEval(std::vector<EvalSeries> series, int ply_count, int current_ply, std::string caption)
{
    graph_.SetSeries(std::move(series), ply_count, current_ply, std::move(caption));
}

void MainView::SetStatus(std::vector<std::string> items)
{
    status_.SetItems(std::move(items));
}

void MainView::SetLogLayout(int height, bool is_open)
{
    log_height_  = std::max(kLogLeastHeight, height);
    is_log_open_ = is_open;
    log_header_.SetOpen(is_open);
    Resized();
    Repaint();
}

void MainView::ToggleLog()
{
    SetLogLayout(log_height_, !is_log_open_);
    if (on_log_layout)
    {
        on_log_layout();
    }
}

int MainView::LogMostHeight() const
{
    const int menu_height = menu_bar_ ? kMenuHeight : 0;
    const int board_least = BoardView::kFileGutter + 2 * BoardView::kFrame + 8 * kLeastSquare;
    const int above_log =
        menu_height + kMargin + board_least + kGap + kLabelHeight + kAnalysisHeight + kLogHeaderGap + kLabelHeight;
    return height() - kStatusHeight - kMargin - above_log;
}

void MainView::SetMenuBarView(std::unique_ptr<reckless::app::MenuBarView> bar)
{
    if (menu_bar_)
    {
        RemoveChild(*menu_bar_);
    }
    menu_bar_ = std::move(bar);
    if (menu_bar_)
    {
        AddChild(*menu_bar_);
    }
    Resized();
}

void MainView::SetOverlay(reckless::view::View *overlay)
{
    if (overlay_ != nullptr && overlay_ != overlay)
    {
        RemoveChild(*overlay_);
    }
    overlay_ = overlay;
    if (overlay_ != nullptr)
    {
        AddChild(*overlay_);
        overlay_->SetBounds(LocalBounds());
    }
}

void MainView::Resized()
{
    if (overlay_ != nullptr)
    {
        overlay_->SetBounds(LocalBounds());
    }
    const int w           = width();
    const int h           = height();
    const int menu_height = menu_bar_ ? kMenuHeight : 0;
    if (menu_bar_)
    {
        menu_bar_->SetBounds({0, 0, w, menu_height});
    }
    status_.SetBounds({0, h - kStatusHeight, w, kStatusHeight});

    const int left   = kMargin;
    const int top    = menu_height + kMargin;
    const int right  = w - kMargin;
    const int bottom = h - kStatusHeight - kMargin;

    // The foot: the log at the bottom under its header - the height asked for, held so the board keeps its least
    // squares, in whole rows as the moves - or the header alone while the log is folded; the analysis and the graph
    // over it.
    int log_height = 0;
    if (is_log_open_)
    {
        log_height =
            log_.FittedHeight(std::clamp(log_height_, kLogLeastHeight, std::max(kLogLeastHeight, LogMostHeight())));
    }
    const int log_top = bottom - log_height;
    log_.view().SetVisible(is_log_open_);
    log_.view().SetBounds({left, log_top, right - left, log_height});
    if (is_log_open_)
    {
        log_.Fit();
    }
    const int header_top = log_top - kLabelHeight;
    log_header_.SetBounds({left, header_top, right - left, kLabelHeight});
    const int foot_top   = header_top - kLogHeaderGap - kLabelHeight - kAnalysisHeight;
    const int graph_left = right - kGraphWidth;
    analysis_label_.SetBounds({left, foot_top, graph_left - kGap - left, kLabelHeight});
    analysis_.SetBounds({left, foot_top + kLabelHeight, graph_left - kGap - left, kAnalysisHeight});
    graph_label_.SetBounds({graph_left, foot_top, kGraphWidth, kLabelHeight});
    graph_.SetBounds({graph_left, foot_top + kLabelHeight, kGraphWidth, kAnalysisHeight});

    // The board: as large as the room above the foot allows beside the two narrow columns, its squares whole pixels;
    // the width it leaves goes to the columns.
    const int column_bottom = foot_top - kGap;
    const int across =
        right - left - kMovesWidth - kSideWidth - 2 * kGap - BoardView::kRankGutter - 2 * BoardView::kFrame;
    const int down        = column_bottom - top - BoardView::kFileGutter - 2 * BoardView::kFrame;
    const int square      = std::max(8, std::min(across, down) / 8);
    const int board_width = BoardView::kRankGutter + 2 * BoardView::kFrame + 8 * square;
    const int board_tall  = BoardView::kFileGutter + 2 * BoardView::kFrame + 8 * square;
    board_.SetBounds({left, top, board_width, board_tall});
    const int spare      = std::max(0, right - left - board_width - kMovesWidth - kSideWidth - 2 * kGap);
    const int moves_left = left + board_width + kGap;
    const int moves_wide = kMovesWidth + spare / 3;
    const int side_left  = moves_left + moves_wide + kGap;
    const int side_wide  = std::max(120, right - side_left);

    // The moves, down the whole height, a whole number of rows tall: the list scrolls by the pixel, so a height
    // between rows would leave the top row half under the header whenever the move shown is kept in view.
    moves_label_.SetBounds({moves_left, top - 6, moves_wide, kLabelHeight});
    const int moves_top = top - 6 + kLabelHeight;
    moves_.view().SetBounds(
        {moves_left, moves_top, moves_wide, moves_.FittedHeight(std::max(60, column_bottom - moves_top))});
    moves_.SetCurrentPly(moves_.model().current_ply()); // the move shown kept in view at the list's new size

    // The players' column beside it: the strip of the player at the board's top level with its top, the other's
    // level with its bottom, the plates over that, and the game's card filling the room between.
    const int board_bottom = top + board_tall - BoardView::kFileGutter;
    top_strip_.SetBounds({side_left, top, side_wide, kStripHeight});
    const int strip_y = std::max(top + kStripHeight + kGap, board_bottom - kStripHeight);
    bottom_strip_.SetBounds({side_left, strip_y, side_wide, kStripHeight});
    const int plates_y = strip_y - 10 - kNavHeight;
    int       x        = side_left;
    for (NavPlate &plate : nav_)
    {
        plate.button->SetBounds({x, plates_y, plate.width, kNavHeight});
        x += plate.width + 6;
    }
    const int card_label_y = top + kStripHeight + kGap - 6;
    card_label_.SetBounds({side_left, card_label_y, side_wide, kLabelHeight});
    const int card_top = card_label_y + kLabelHeight;
    card_.SetBounds({side_left, card_top, side_wide, std::max(40, plates_y - kGap - card_top)});
}

void MainView::Paint(reckless::gfx::Canvas &c)
{
    c.FillAll(theme::kWindow);
}

} // namespace amphitheater::app
