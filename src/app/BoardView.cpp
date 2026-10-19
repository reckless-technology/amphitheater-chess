#include "BoardView.h"

#include "Theme.h"

#include "Path.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace uci_host::app
{

namespace
{

using reckless::gfx::Color;
using reckless::gfx::PointF;
using reckless::gfx::RectF;

/** How far a press must move before it is a drag, in design pixels. */
constexpr float kDragThreshold = 4.0f;

/** The order of the promotion picker, from the promotion square inwards. */
constexpr chess::Piece kPromotionOrder[] = {chess::kQueen, chess::kKnight, chess::kRook, chess::kBishop};

} // namespace

BoardView::BoardView(const PieceSet &pieces) : pieces_(pieces), position_(chess::Position::Start())
{
    SetOpaque(false);
}

void BoardView::SetPosition(const chess::Position &position, std::optional<chess::Move> last_move)
{
    position_  = position;
    last_move_ = last_move;
    ClearSelection();
    Repaint();
}

void BoardView::SetFlipped(bool is_flipped)
{
    if (is_flipped != is_flipped_)
    {
        is_flipped_ = is_flipped;
        Repaint();
    }
}

void BoardView::SetInteractive(bool is_interactive)
{
    if (is_interactive != is_interactive_)
    {
        is_interactive_ = is_interactive;
        ClearSelection();
        cursor = is_interactive ? reckless::view::Cursor::kPointingHand : reckless::view::Cursor::kArrow;
        Repaint();
    }
}

void BoardView::SetArrows(std::vector<BoardArrow> arrows)
{
    arrows_ = std::move(arrows);
    Repaint();
}

float BoardView::SquareSize() const
{
    const float across = static_cast<float>(width() - kRankGutter - 2 * kFrame);
    const float down   = static_cast<float>(height() - kFileGutter - 2 * kFrame);
    return std::max(1.0f, std::floor(std::min(across, down) / 8.0f));
}

RectF BoardView::BoardArea() const
{
    const float side = SquareSize() * 8.0f;
    return {static_cast<float>(kRankGutter + kFrame), static_cast<float>(kFrame), side, side};
}

RectF BoardView::SquareBounds(chess::Square square) const
{
    const RectF area   = BoardArea();
    const float size   = SquareSize();
    const int   column = is_flipped_ ? 7 - square.File() : square.File();
    const int   row    = is_flipped_ ? square.Rank() : 7 - square.Rank();
    return {area.x() + static_cast<float>(column) * size, area.y() + static_cast<float>(row) * size, size, size};
}

std::optional<chess::Square> BoardView::SquareAt(PointF point) const
{
    const RectF area = BoardArea();
    if (!area.Contains(point))
    {
        return std::nullopt;
    }
    const float size   = SquareSize();
    const int   column = std::clamp(static_cast<int>((point.x - area.x()) / size), 0, 7);
    const int   row    = std::clamp(static_cast<int>((point.y - area.y()) / size), 0, 7);
    const int   file   = is_flipped_ ? 7 - column : column;
    const int   rank   = is_flipped_ ? row : 7 - row;
    return chess::Square(file, rank);
}

RectF BoardView::PromotionChoiceBounds(std::size_t index) const
{
    if (promotion_choices_.empty())
    {
        return {};
    }
    const RectF target = SquareBounds(promotion_choices_.front().To());
    const RectF area   = BoardArea();
    // From the promotion square towards the board's middle: down from the top edge, up from the bottom one.
    const bool  downwards = target.CentreY() < area.CentreY();
    const float step      = downwards ? target.height() : -target.height();
    return target.Translated(0.0f, step * static_cast<float>(index));
}

std::optional<chess::Square> BoardView::CheckedKing() const
{
    if (!position_.IsInCheck())
    {
        return std::nullopt;
    }
    const auto king = position_.Pieces(chess::kKing, position_.SideToMove());
    if (!king.IsNotEmpty())
    {
        return std::nullopt;
    }
    return king.Lsb();
}

void BoardView::Paint(reckless::gfx::Canvas &c)
{
    const RectF area = BoardArea();
    const float size = SquareSize();

    // The frame, the squares, and the coordinates beside them.
    c.SetColor(theme::kBoardEdge);
    c.FillRect(area.Expanded(static_cast<float>(kFrame)));
    for (int file = 0; file < 8; ++file)
    {
        for (int rank = 0; rank < 8; ++rank)
        {
            const chess::Square square(file, rank);
            c.SetColor((file + rank) % 2 == 0 ? theme::kDarkSquare : theme::kLightSquare);
            c.FillRect(SquareBounds(square));
        }
    }
    c.SetFont(fonts::Small());
    c.SetColor(theme::kMuted);
    for (int index = 0; index < 8; ++index)
    {
        const chess::Square rank_square(0, index);
        const RectF         rank_bounds = SquareBounds(rank_square);
        c.DrawText(std::string(1, static_cast<char>('1' + index)),
                   RectF{0.0f, rank_bounds.y(), static_cast<float>(kRankGutter), size},
                   reckless::gfx::Justify::kCentred, false);
        const chess::Square file_square(index, 0);
        const RectF         file_bounds = SquareBounds(file_square);
        c.DrawText(std::string(1, static_cast<char>('a' + index)),
                   RectF{file_bounds.x(), area.bottom() + static_cast<float>(kFrame), size,
                         static_cast<float>(kFileGutter - kFrame)},
                   reckless::gfx::Justify::kCentred, false);
    }

    // The last move, a king in check, the selection.
    if (last_move_ && !last_move_->IsNone())
    {
        c.SetColor(theme::kAmber.WithAlpha(0.42f));
        c.FillRect(SquareBounds(last_move_->From()));
        c.FillRect(SquareBounds(last_move_->To()));
    }
    if (const auto king = CheckedKing())
    {
        const RectF             bounds = SquareBounds(*king);
        reckless::gfx::Gradient glow;
        glow.radial      = true;
        glow.start       = bounds.Centre();
        glow.start_color = Color(0xffe0302au);
        glow.end         = PointF{bounds.right(), bounds.CentreY()};
        glow.end_color   = Color(0x00e0302au);
        glow.stops       = {{0.35f, Color(0xd8e0302au)}};
        c.SetGradient(glow);
        c.FillRect(bounds);
    }
    if (selected_)
    {
        c.SetColor(theme::kAmber.WithAlpha(0.55f));
        c.FillRect(SquareBounds(*selected_));
    }

    // The pieces, but the one being dragged.
    for (int index = 0; index < 64; ++index)
    {
        const chess::Square square(index);
        const chess::Piece  piece = position_.PieceOn(square);
        if (piece == chess::kNone || (is_dragging_ && selected_ && *selected_ == square))
        {
            continue;
        }
        pieces_.Draw(c, position_.ColorOn(square), piece, SquareBounds(square).Reduced(size * 0.04f));
    }

    // Where the selected piece may go: a dot on an empty square, a ring round a capture.
    for (const chess::Square target : targets_)
    {
        const RectF bounds = SquareBounds(target);
        c.SetColor(Color(0x55201c18u));
        if (position_.PieceOn(target) == chess::kNone)
        {
            const float dot = size * 0.30f;
            c.FillEllipse(RectF{bounds.CentreX() - dot / 2.0f, bounds.CentreY() - dot / 2.0f, dot, dot});
        }
        else
        {
            c.DrawEllipse(bounds.Reduced(size * 0.04f), size * 0.08f);
        }
    }

    for (const BoardArrow &arrow : arrows_)
    {
        DrawArrow(c, arrow);
    }

    if (is_dragging_ && selected_)
    {
        const RectF dragged{drag_point_.x - size / 2.0f, drag_point_.y - size / 2.0f, size, size};
        pieces_.Draw(c, position_.ColorOn(*selected_), position_.PieceOn(*selected_), dragged.Reduced(size * 0.02f));
    }

    // The promotion picker: the board dimmed, the four pieces in discs on the promotion square's file.
    if (!promotion_choices_.empty())
    {
        c.SetColor(Color(0x99000000u));
        c.FillRect(area);
        const chess::Color mover = position_.SideToMove();
        for (std::size_t index = 0; index < promotion_choices_.size(); ++index)
        {
            const RectF bounds = PromotionChoiceBounds(index);
            c.SetColor(theme::kLightSquare);
            c.FillEllipse(bounds.Reduced(size * 0.03f));
            c.SetColor(theme::kAmber);
            c.DrawEllipse(bounds.Reduced(size * 0.03f), 2.0f);
            pieces_.Draw(c, mover, promotion_choices_[index].PromotedPiece(), bounds.Reduced(size * 0.12f));
        }
    }
}

void BoardView::DrawArrow(reckless::gfx::Canvas &c, const BoardArrow &arrow) const
{
    const float  size = SquareSize();
    const PointF from = SquareBounds(arrow.from).Centre();
    const PointF to   = SquareBounds(arrow.to).Centre();
    const float  dx   = to.x - from.x;
    const float  dy   = to.y - from.y;
    const float  len  = std::hypot(dx, dy);
    if (len < 1.0f)
    {
        return;
    }
    const float  ux    = dx / len;
    const float  uy    = dy / len;
    const float  head  = size * 0.30f; // the head's length
    const float  half  = size * 0.26f; // half the head's width
    const float  shaft = size * 0.17f; // the shaft's width
    const PointF neck{to.x - ux * head, to.y - uy * head};
    c.SetColor(arrow.colour);
    reckless::gfx::Path line;
    line.MoveTo(from);
    line.LineTo(neck);
    c.StrokePath(
        line, reckless::gfx::Stroke(shaft, reckless::gfx::Stroke::Join::kCurved, reckless::gfx::Stroke::Cap::kRounded));
    reckless::gfx::Path tip;
    tip.AddTriangle(to, PointF{neck.x - uy * half, neck.y + ux * half}, PointF{neck.x + uy * half, neck.y - ux * half});
    c.FillPath(tip);
}

void BoardView::Select(chess::Square square)
{
    selected_ = square;
    targets_.clear();
    for (const chess::Move move : position_.GenerateLegalMoves())
    {
        if (move.From() == square && std::find(targets_.begin(), targets_.end(), move.To()) == targets_.end())
        {
            targets_.push_back(move.To());
        }
    }
    Repaint();
}

void BoardView::ClearSelection()
{
    if (selected_ || !targets_.empty() || is_dragging_ || !promotion_choices_.empty())
    {
        selected_.reset();
        targets_.clear();
        is_dragging_ = false;
        promotion_choices_.clear();
        Repaint();
    }
}

void BoardView::MoveOrPromote(chess::Square from, chess::Square to)
{
    std::vector<chess::Move> matches;
    for (const chess::Move move : position_.GenerateLegalMoves())
    {
        if (move.From() == from && move.To() == to)
        {
            matches.push_back(move);
        }
    }
    if (matches.empty())
    {
        return;
    }
    if (matches.size() == 1)
    {
        const chess::Move move = matches.front();
        ClearSelection();
        if (on_move)
        {
            on_move(move);
        }
        return;
    }
    // A promotion: one move a piece; the picker offers them in its own order.
    std::vector<chess::Move> choices;
    for (const chess::Piece piece : kPromotionOrder)
    {
        for (const chess::Move move : matches)
        {
            if (move.PromotedPiece() == piece)
            {
                choices.push_back(move);
            }
        }
    }
    selected_.reset();
    targets_.clear();
    is_dragging_       = false;
    promotion_choices_ = std::move(choices);
    Repaint();
}

void BoardView::MouseDown(const reckless::view::MouseEvent &e)
{
    if (!promotion_choices_.empty())
    {
        for (std::size_t index = 0; index < promotion_choices_.size(); ++index)
        {
            if (PromotionChoiceBounds(index).Contains(e.position))
            {
                const chess::Move move = promotion_choices_[index];
                ClearSelection();
                if (on_move)
                {
                    on_move(move);
                }
                return;
            }
        }
        ClearSelection();
        return;
    }
    const auto square = SquareAt(e.position);
    if (!is_interactive_ || !square || e.right)
    {
        ClearSelection();
        return;
    }
    if (selected_ && std::find(targets_.begin(), targets_.end(), *square) != targets_.end())
    {
        MoveOrPromote(*selected_, *square);
        return;
    }
    if (position_.PieceOn(*square) != chess::kNone && position_.ColorOn(*square) == position_.SideToMove())
    {
        Select(*square);
        drag_point_ = e.position;
        return;
    }
    ClearSelection();
}

void BoardView::MouseDrag(const reckless::view::MouseEvent &e)
{
    if (!selected_ || !promotion_choices_.empty())
    {
        return;
    }
    if (!is_dragging_ && std::hypot(e.position.x - e.down.x, e.position.y - e.down.y) < kDragThreshold)
    {
        return;
    }
    is_dragging_ = true;
    drag_point_  = e.position;
    cursor       = reckless::view::Cursor::kGrabbing;
    Repaint();
}

void BoardView::MouseUp(const reckless::view::MouseEvent &e)
{
    cursor = is_interactive_ ? reckless::view::Cursor::kPointingHand : reckless::view::Cursor::kArrow;
    if (!is_dragging_ || !selected_)
    {
        return;
    }
    is_dragging_      = false;
    const auto square = SquareAt(e.position);
    if (square && std::find(targets_.begin(), targets_.end(), *square) != targets_.end())
    {
        MoveOrPromote(*selected_, *square);
        return;
    }
    Repaint(); // back where it was, still selected
}

} // namespace uci_host::app
