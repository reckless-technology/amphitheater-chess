#include "PositionEditor.h"

#include "BoardView.h"
#include "Theme.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <utility>
#include <vector>

namespace amphitheater::app
{

namespace
{

using reckless::gfx::PointF;
using reckless::gfx::RectF;

/** The palette's pieces in a row, the king first. */
constexpr chess::Piece kPaletteOrder[] = {chess::kKing,   chess::kQueen,  chess::kRook,
                                          chess::kBishop, chess::kKnight, chess::kPawn};

constexpr int kCardWidth  = 880; ///< The dialog's card.
constexpr int kCardHeight = 690; ///< Likewise.
constexpr int kSquare     = 52;  ///< The setup board's squares, in design pixels.
constexpr int kLabelWidth = 120; ///< A control's caption beside it.
constexpr int kRowHeight  = 34;  ///< A control's row.
constexpr int kRowGap     = 10;  ///< Between the rows.
constexpr int kColumnGap  = 28;  ///< Between the board and the controls.

/**
 * @param text A sentence's start, as the FEN reader words a reason.
 * @return It with its first letter in capitals and a full stop at its end.
 */
std::string Sentence(std::string text)
{
    if (!text.empty())
    {
        text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
        if (text.back() != '.')
        {
            text += '.';
        }
    }
    return text;
}

} // namespace

SetupBoardView::SetupBoardView(const PieceSet &pieces, const chess::BoardSetup &setup) : pieces_(pieces), setup_(setup)
{
    SetOpaque(false);
    cursor = reckless::view::Cursor::kPointingHand;
}

void SetupBoardView::SetFlipped(bool is_flipped)
{
    if (is_flipped != is_flipped_)
    {
        is_flipped_ = is_flipped;
        Repaint();
    }
}

float SetupBoardView::SquareSize() const
{
    const float across = static_cast<float>(width() - kRankGutter);
    const float down   = static_cast<float>(height() - kFileGutter);
    return std::max(1.0f, std::floor(std::min(across, down) / 8.0f));
}

RectF SetupBoardView::BoardArea() const
{
    const float side = SquareSize() * 8.0f;
    return {static_cast<float>(kRankGutter), 0.0f, side, side};
}

RectF SetupBoardView::SquareBounds(chess::Square square) const
{
    const RectF area   = BoardArea();
    const float size   = SquareSize();
    const int   column = is_flipped_ ? 7 - square.File() : square.File();
    const int   row    = is_flipped_ ? square.Rank() : 7 - square.Rank();
    return {area.x() + static_cast<float>(column) * size, area.y() + static_cast<float>(row) * size, size, size};
}

std::optional<chess::Square> SetupBoardView::SquareAt(PointF point) const
{
    const RectF area = BoardArea();
    if (!area.Contains(point))
    {
        return std::nullopt;
    }
    const float size   = SquareSize();
    const int   column = std::clamp(static_cast<int>((point.x - area.x()) / size), 0, 7);
    const int   row    = std::clamp(static_cast<int>((point.y - area.y()) / size), 0, 7);
    return is_flipped_ ? chess::Square(7 - column, row) : chess::Square(column, 7 - row);
}

void SetupBoardView::Paint(reckless::gfx::Canvas &c)
{
    const RectF area = BoardArea();
    const float size = SquareSize();
    c.SetColor(theme::kBoardEdge);
    c.FillRect(area.Expanded(1.0f));
    for (int index = 0; index < 64; ++index)
    {
        const chess::Square square(index);
        c.SetColor(SquareFill(square, false));
        c.FillRect(SquareBounds(square));
    }
    c.SetFont(fonts::Small());
    c.SetColor(theme::kMuted);
    for (int index = 0; index < 8; ++index)
    {
        const RectF rank = SquareBounds(chess::Square(0, index));
        c.DrawText(std::string(1, static_cast<char>('1' + index)),
                   RectF{0.0f, rank.y(), static_cast<float>(kRankGutter), size}, reckless::gfx::Justify::kCentred,
                   false);
        const RectF file = SquareBounds(chess::Square(index, 0));
        c.DrawText(std::string(1, static_cast<char>('a' + index)),
                   RectF{file.x(), area.bottom(), size, static_cast<float>(kFileGutter)},
                   reckless::gfx::Justify::kCentred, false);
    }
    for (int index = 0; index < 64; ++index)
    {
        const chess::Square     square(index);
        const chess::SetupPiece piece = setup_.At(square);
        if (piece.piece == chess::kNone || (is_dragging_ && pressed_ == square))
        {
            continue;
        }
        pieces_.Draw(c, piece.color, piece.piece, SquareBounds(square));
    }
    if (is_dragging_ && pressed_)
    {
        const chess::SetupPiece piece = setup_.At(*pressed_);
        pieces_.Draw(c, piece.color, piece.piece,
                     RectF{drag_point_.x - size / 2.0f, drag_point_.y - size / 2.0f, size, size});
    }
}

void SetupBoardView::MouseDown(const reckless::view::MouseEvent &e)
{
    is_dragging_ = false;
    pressed_.reset();
    const std::optional<chess::Square> square = SquareAt(e.position);
    if (!square)
    {
        return;
    }
    if (e.right)
    {
        if (on_clear)
        {
            on_clear(*square);
        }
        return;
    }
    pressed_ = square;
}

void SetupBoardView::MouseDrag(const reckless::view::MouseEvent &e)
{
    if (!pressed_ || setup_.At(*pressed_).piece == chess::kNone)
    {
        return;
    }
    const float travel = std::hypot(e.position.x - e.down.x, e.position.y - e.down.y);
    if (!is_dragging_ && travel < kDragThreshold)
    {
        return;
    }
    is_dragging_ = true;
    drag_point_  = e.position;
    Repaint();
}

void SetupBoardView::MouseUp(const reckless::view::MouseEvent &e)
{
    const std::optional<chess::Square> pressed = std::exchange(pressed_, std::nullopt);
    const bool                         dragged = std::exchange(is_dragging_, false);
    if (!pressed || e.right)
    {
        return;
    }
    const std::optional<chess::Square> released = SquareAt(e.position);
    if (dragged)
    {
        Repaint();
        if (released && on_move)
        {
            on_move(*pressed, *released);
        }
        else if (!released && on_drag_off)
        {
            on_drag_off(*pressed);
        }
        return;
    }
    if (released == pressed && on_click)
    {
        on_click(*pressed);
    }
}

PiecePalette::PiecePalette(const PieceSet &pieces) : pieces_(pieces)
{
    SetOpaque(false);
    cursor = reckless::view::Cursor::kPointingHand;
}

void PiecePalette::Choose(chess::SetupPiece piece)
{
    if (!(piece == chosen_))
    {
        chosen_ = piece;
        Repaint();
    }
}

RectF PiecePalette::CellBounds(chess::SetupPiece piece) const
{
    const float cell = std::floor(std::min(static_cast<float>(width()) / 6.0f, static_cast<float>(height()) / 2.0f));
    const auto  column =
        std::find(std::begin(kPaletteOrder), std::end(kPaletteOrder), piece.piece) - std::begin(kPaletteOrder);
    return {static_cast<float>(column) * cell, piece.color == chess::kWhite ? 0.0f : cell, cell, cell};
}

void PiecePalette::Paint(reckless::gfx::Canvas &c)
{
    for (const chess::Color color : {chess::kWhite, chess::kBlack})
    {
        for (const chess::Piece piece : kPaletteOrder)
        {
            const RectF bounds = CellBounds({color, piece});
            // On a light square's colour, so Black's pieces show as well as White's.
            c.SetColor(theme::kLightSquare);
            c.FillRoundedRect(bounds.Reduced(2.0f), 4.0f);
            pieces_.Draw(c, color, piece, bounds.Reduced(4.0f));
            if (chess::SetupPiece{color, piece} == chosen_)
            {
                c.SetColor(theme::kAmber);
                c.DrawRoundedRect(bounds.Reduced(2.0f), 4.0f, 2.0f);
            }
        }
    }
}

void PiecePalette::MouseDown(const reckless::view::MouseEvent &e)
{
    for (const chess::Color color : {chess::kWhite, chess::kBlack})
    {
        for (const chess::Piece piece : kPaletteOrder)
        {
            if (CellBounds({color, piece}).Contains(e.position))
            {
                Choose({color, piece});
                if (on_choose)
                {
                    on_choose(chosen_);
                }
                return;
            }
        }
    }
}

PositionEditorDialog::PositionEditorDialog(const PieceSet &pieces, const chess::Position &position, bool is_flipped)
    : DialogFrame("Set up position", kCardWidth, kCardHeight), setup_(chess::BoardSetup::FromPosition(position)),
      board_(pieces, setup_), palette_(pieces), side_label_("Side to move"), castling_label_("Castling"),
      en_passant_label_("En passant"), move_number_label_("Move number"), move_number_(FieldStyle()), fen_label_("FEN"),
      fen_(FieldStyle()),
      hint_("Click a square to put the chosen piece there, and again to take it off; right-click to empty a "
            "square; drag a piece to move it, or off the board to take it off.",
            true)
{
    board_.SetFlipped(is_flipped);
    const char *castling_names[] = {"White O-O", "White O-O-O", "Black O-O", "Black O-O-O"};
    for (int index = 0; index < 4; ++index)
    {
        castling_[static_cast<std::size_t>(index)]            = std::make_unique<CheckBox>(castling_names[index]);
        const chess::Color color                              = index < 2 ? chess::kWhite : chess::kBlack;
        const bool         is_kingside                        = index % 2 == 0;
        castling_[static_cast<std::size_t>(index)]->on_change = [this, color, is_kingside](bool is_checked) {
            Edit([&](chess::BoardSetup &setup) { setup.SetCastling(color, is_kingside, is_checked); });
        };
    }
    start_          = MakePlate("Start position",
                                [this] { Edit([](chess::BoardSetup &setup) { setup = chess::BoardSetup::Start(); }); });
    clear_          = MakePlate("Clear board", [this] { Edit([](chess::BoardSetup &setup) { setup.Clear(); }); });
    flip_           = MakePlate("Flip board", [this] { board_.SetFlipped(!board_.flipped()); });
    start_->tooltip = "The pieces as a game starts.";
    clear_->tooltip = "Every piece taken off.";
    flip_->tooltip  = "Black at the bottom, or White.";

    board_.on_click = [this](chess::Square square) {
        Edit([&](chess::BoardSetup &setup) {
            const chess::SetupPiece chosen = palette_.chosen();
            setup.Put(square, setup.At(square) == chosen ? chess::SetupPiece{} : chosen);
        });
    };
    board_.on_clear = [this](chess::Square square) { Edit([&](chess::BoardSetup &setup) { setup.Put(square, {}); }); };
    board_.on_move  = [this](chess::Square from, chess::Square to) {
        Edit([&](chess::BoardSetup &setup) { setup.Move(from, to); });
    };
    board_.on_drag_off = [this](chess::Square from) { Edit([&](chess::BoardSetup &setup) { setup.Put(from, {}); }); };
    side_.on_change    = [this](int index) {
        Edit([&](chess::BoardSetup &setup) { setup.SetSideToMove(index == 1 ? chess::kBlack : chess::kWhite); });
    };
    en_passant_.on_change = [this](int index) {
        Edit([&](chess::BoardSetup &setup) {
            const std::vector<chess::Square> squares = setup.EnPassantSquares();
            setup.SetEnPassant(index > 0 && index <= static_cast<int>(squares.size())
                                   ? std::optional<chess::Square>(squares[static_cast<std::size_t>(index - 1)])
                                   : std::nullopt);
        });
    };
    move_number_.on_commit = [this](const std::string &text) {
        int        number = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
        Edit([&](chess::BoardSetup &setup) {
            if (parsed.ec == std::errc() && parsed.ptr == text.data() + text.size() && number >= 1)
            {
                setup.SetMoveNumber(number);
            }
        });
    };
    fen_.on_commit = [this](const std::string &text) {
        if (text != setup_.ToFen())
        {
            ReadFen(text);
        }
    };

    AddChild(board_);
    AddChild(palette_);
    for (reckless::view::View *view : std::initializer_list<reckless::view::View *>{
             &side_label_, &side_, &castling_label_, &en_passant_label_, &en_passant_, &move_number_label_,
             &move_number_, &fen_label_, &fen_, start_.get(), clear_.get(), flip_.get(), &hint_, &message_})
    {
        AddChild(*view);
    }
    for (const auto &box : castling_)
    {
        AddChild(*box);
    }
    set_up_ = &AddButton("Set up", [this] { SetUp(); });
    AddButton("Cancel", [this] {
        if (on_close)
        {
            on_close();
        }
    });
    on_accept = [this] { SetUp(); };
    on_cancel = [this] {
        if (on_close)
        {
            on_close();
        }
    };
    Refresh();
}

void PositionEditorDialog::Edit(const std::function<void(chess::BoardSetup &setup)> &edit)
{
    edit(setup_);
    Refresh();
}

bool PositionEditorDialog::ReadFen(const std::string &fen)
{
    const auto position = chess::Position::FromFen(fen, chess::FenMode::kTolerant);
    if (!position)
    {
        message_.SetText("That FEN cannot be read: " + position.ErrorMessage() + ".");
        message_.SetError(true);
        return false;
    }
    Edit([&](chess::BoardSetup &setup) { setup = chess::BoardSetup::FromPosition(*position); });
    return true;
}

bool PositionEditorDialog::can_set_up() const
{
    return static_cast<bool>(setup_.ToPosition());
}

void PositionEditorDialog::SetUp()
{
    const auto position = setup_.ToPosition();
    if (!position)
    {
        Refresh();
        return;
    }
    if (on_set_up)
    {
        on_set_up(*position);
    }
}

void PositionEditorDialog::Refresh()
{
    board_.Repaint();
    side_.SetChoices({"White", "Black"}, setup_.side_to_move() == chess::kWhite ? 0 : 1);
    for (int index = 0; index < 4; ++index)
    {
        const chess::Color color       = index < 2 ? chess::kWhite : chess::kBlack;
        const bool         is_kingside = index % 2 == 0;
        CheckBox          &box         = *castling_[static_cast<std::size_t>(index)];
        box.SetChecked(setup_.castling(color, is_kingside));
        box.SetEnabled(setup_.CanCastle(color, is_kingside));
    }
    std::vector<std::string>         squares{"None"};
    int                              current = 0;
    const std::vector<chess::Square> allowed = setup_.EnPassantSquares();
    for (std::size_t index = 0; index < allowed.size(); ++index)
    {
        squares.push_back(allowed[index].ToString());
        if (setup_.en_passant() == allowed[index])
        {
            current = static_cast<int>(index + 1);
        }
    }
    en_passant_.SetChoices(std::move(squares), current);
    en_passant_.SetEnabled(!allowed.empty());
    move_number_.SetText(std::to_string(setup_.move_number()));
    fen_.SetText(setup_.ToFen());
    const auto position = setup_.ToPosition();
    if (position)
    {
        message_.SetText(std::string("A legal position, ") +
                         (setup_.side_to_move() == chess::kWhite ? "White" : "Black") + " to move.");
    }
    else
    {
        message_.SetText(Sentence(position.ErrorMessage()));
    }
    message_.SetError(!position);
    if (set_up_ != nullptr)
    {
        set_up_->SetEnabled(static_cast<bool>(position));
    }
}

void PositionEditorDialog::LayOutContent(reckless::gfx::RectI area)
{
    const int board_side = SetupBoardView::kRankGutter + 8 * kSquare;
    board_.SetBounds({area.x(), area.y() + 4, board_side, SetupBoardView::kFileGutter + 8 * kSquare});

    const int left  = area.x() + board_side + kColumnGap;
    const int wide  = area.right() - left;
    int       y     = area.y() + 4;
    const int cells = 6 * kSquare;
    palette_.SetBounds({left, y, cells, 2 * kSquare});
    y += 2 * kSquare + 2 * kRowGap;

    const int control_left = left + kLabelWidth;
    const int control_wide = wide - kLabelWidth;
    side_label_.SetBounds({left, y, kLabelWidth, kRowHeight});
    side_.SetBounds({control_left, y, std::min(160, control_wide), kRowHeight});
    y += kRowHeight + kRowGap;
    castling_label_.SetBounds({left, y, kLabelWidth, kRowHeight});
    const int box_wide = control_wide / 2;
    for (int index = 0; index < 4; ++index)
    {
        castling_[static_cast<std::size_t>(index)]->SetBounds(
            {control_left + (index % 2) * box_wide, y + (index / 2) * (kRowHeight - 4), box_wide, kRowHeight - 6});
    }
    y += 2 * (kRowHeight - 4) + kRowGap;
    en_passant_label_.SetBounds({left, y, kLabelWidth, kRowHeight});
    en_passant_.SetBounds({control_left, y, std::min(160, control_wide), kRowHeight});
    y += kRowHeight + kRowGap;
    move_number_label_.SetBounds({left, y, kLabelWidth, kRowHeight});
    move_number_.SetBounds({control_left, y, std::min(100, control_wide), kRowHeight - 2});
    y += kRowHeight + 2 * kRowGap;
    const int plate_gap  = 8;
    const int plate_wide = (wide - 2 * plate_gap) / 3;
    int       x          = left;
    for (reckless::view::Button *plate : {start_.get(), clear_.get(), flip_.get()})
    {
        plate->SetBounds({x, y, plate_wide, kRowHeight});
        x += plate_wide + plate_gap;
    }

    int foot = area.y() + 4 + SetupBoardView::kFileGutter + 8 * kSquare + 2 * kRowGap;
    fen_label_.SetBounds({area.x(), foot, 50, kRowHeight});
    fen_.SetBounds({area.x() + 50, foot, area.width() - 50, kRowHeight - 2});
    foot += kRowHeight + kRowGap;
    hint_.SetBounds({area.x(), foot, area.width(), 46});
    foot += 50;
    message_.SetBounds({area.x(), foot, area.width(), std::max(22, area.bottom() - foot)});
}

} // namespace amphitheater::app
