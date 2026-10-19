#include "MainView.h"

#include "Commands.h"
#include "Theme.h"

#include "PanelStyle.h"

#include <algorithm>

namespace uci_host::app
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

    AddChild(menu_bar_);
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
    AddChild(log_.view());
    AddChild(status_);

    menu_bar_.on_command = [this](int command) { Tell(command); };
    board_.on_move       = [this](chess::Move move) {
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

void MainView::SetEval(std::vector<std::optional<double>> scores, int current_ply, std::string caption)
{
    graph_.SetScores(std::move(scores), current_ply, std::move(caption));
}

void MainView::SetStatus(std::vector<std::string> items)
{
    status_.SetItems(std::move(items));
}

void MainView::SetMenus(std::vector<MenuBarEntry> entries)
{
    menu_bar_.SetEntries(std::move(entries));
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
    const int w = width();
    const int h = height();
    menu_bar_.SetBounds({0, 0, w, kMenuHeight});
    status_.SetBounds({0, h - kStatusHeight, w, kStatusHeight});

    const int left   = kMargin;
    const int top    = kMenuHeight + kMargin;
    const int right  = w - kMargin;
    const int bottom = h - kStatusHeight - kMargin;

    // The foot: the log at the bottom, the analysis and the graph over it.
    const int log_height = log_.FittedHeight(std::clamp(h / 8, 100, kLogHeight)); // whole rows, as the moves
    const int log_top    = bottom - log_height;
    log_.view().SetBounds({left, log_top, right - left, log_height});
    const int foot_top   = log_top - kGap - kLabelHeight - kAnalysisHeight;
    const int graph_left = right - kGraphWidth;
    analysis_label_.SetBounds({left, foot_top, graph_left - kGap - left, kLabelHeight});
    analysis_.SetBounds({left, foot_top + kLabelHeight, graph_left - kGap - left, kAnalysisHeight});
    graph_label_.SetBounds({graph_left, foot_top, kGraphWidth, kLabelHeight});
    graph_.SetBounds({graph_left, foot_top + kLabelHeight, kGraphWidth, kAnalysisHeight});

    // The board column: as large a board as the height leaves, and no wider than about half the window.
    const int column_bottom = foot_top - kGap;
    const int board_height  = column_bottom - top - 2 * kStripHeight - 3 * 8 - kNavHeight - 2;
    const int across        = (right - left) * 52 / 100 - BoardView::kRankGutter - 2 * BoardView::kFrame;
    const int down          = board_height - BoardView::kFileGutter - 2 * BoardView::kFrame;
    const int square        = std::max(8, std::min(across, down) / 8);
    const int board_width   = BoardView::kRankGutter + 2 * BoardView::kFrame + 8 * square;
    const int board_tall    = BoardView::kFileGutter + 2 * BoardView::kFrame + 8 * square;
    const int column_width  = std::max(board_width, kColumnWidth);
    int       y             = top;
    top_strip_.SetBounds({left, y, column_width, kStripHeight});
    y += kStripHeight + 8;
    board_.SetBounds({left, y, board_width, board_tall});
    y += board_tall + 8;
    bottom_strip_.SetBounds({left, y, column_width, kStripHeight});
    y += kStripHeight + 10;
    int x = left;
    for (NavPlate &plate : nav_)
    {
        plate.button->SetBounds({x, y, plate.width, kNavHeight});
        x += plate.width + 6;
    }

    // The right column: the moves over the game's card.
    const int side_left  = left + column_width + kGap + 6;
    const int side_width = std::max(120, right - side_left);
    const int card_tall  = card_.PreferredHeight();
    const int card_top   = column_bottom - card_tall;
    card_.SetBounds({side_left, card_top, side_width, card_tall});
    card_label_.SetBounds({side_left, card_top - kLabelHeight, side_width, kLabelHeight});
    moves_label_.SetBounds({side_left, top - 6, side_width, kLabelHeight});
    // The moves a whole number of rows tall: the list scrolls by the pixel, so a height between rows would leave the
    // top row half under the header whenever the move shown is kept in view at the bottom.
    const int moves_top = top - 6 + kLabelHeight;
    moves_.view().SetBounds({side_left, moves_top, side_width,
                             moves_.FittedHeight(std::max(60, card_top - kLabelHeight - kGap - moves_top))});
    moves_.SetCurrentPly(moves_.model().current_ply()); // the move shown kept in view at the list's new size
}

void MainView::Paint(reckless::gfx::Canvas &c)
{
    c.FillAll(theme::kWindow);
}

bool MainView::KeyPressed(const reckless::view::KeyEvent &key)
{
    using reckless::view::Key;
    switch (key.key)
    {
    case Key::kLeft:
        Tell(kBack);
        return true;
    case Key::kRight:
        Tell(kForward);
        return true;
    case Key::kHome:
        Tell(kFirst);
        return true;
    case Key::kEnd:
        Tell(kLast);
        return true;
    case Key::kCharacter: {
        const char32_t letter =
            key.character >= U'A' && key.character <= U'Z' ? key.character - U'A' + U'a' : key.character;
        if (!key.modifiers.command)
        {
            if (letter == U'f')
            {
                Tell(kFlip);
                return true;
            }
            return false;
        }
        // The window's shortcuts: Command on a Mac, Control elsewhere.
        const int command = letter == U'n'   ? kNewGame
                            : letter == U'o' ? kOpenPgn
                            : letter == U's' ? kSavePgn
                            : letter == U'c' ? (key.modifiers.shift ? kCopyPgn : kCopyFen)
                            : letter == U'v' ? kPasteFen
                            : letter == U'z' ? kTakeBack
                            : letter == U'e' ? kEngineManager
                            : letter == U'q' ? kQuit
                                             : kNoCommand;
        if (command == kNoCommand)
        {
            return false;
        }
        Tell(command);
        return true;
    }
    default:
        return false;
    }
}

} // namespace uci_host::app
