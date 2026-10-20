#pragma once
/// @file BoardView.h The board: the squares, the coordinates, the pieces from the SVG set at whatever size the view is
/// given, the last move, a king in check, arrows, and a person's moves by click or by drag with the legal targets
/// marked and a picker for a promotion.

#include "PieceSet.h"
#include "chess/Move.h"
#include "chess/Position.h"

#include "Color.h"
#include "Geometry.h"
#include "View.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace amphitheater::app
{

/** An arrow from one square to another: an engine's best move. */
struct BoardArrow
{
    chess::Square        from;   ///< Where it starts.
    chess::Square        to;     ///< Where it points.
    reckless::gfx::Color colour; ///< Its colour, with its opacity.
};

/**
 * A square's fill: light or dark, and lit in its own shade of the accent when the last move left or reached it.
 * @param square The square.
 * @param is_last_move Whether the last move left it or reached it.
 * @return The fill.
 */
reckless::gfx::Color SquareFill(chess::Square square, bool is_last_move);

/**
 * The board. It shows a position and takes a person's moves when it is interactive: a press on a piece of the side
 * to move selects it and marks the squares it may go to (a dot on an empty square, a ring on a capture); a press on
 * one of them, or a drag of the piece onto one, makes the move. A promotion opens a picker of the four pieces on the
 * promotion square's file; a press elsewhere closes it with no move. Every move it hands on (on_move) is legal.
 */
class BoardView final : public reckless::view::View
{
  public:
    static constexpr int kRankGutter = 18; ///< The width of the rank numbers' column, in design pixels.
    static constexpr int kFileGutter = 20; ///< The height of the file letters' row, in design pixels.
    static constexpr int kFrame      = 2;  ///< The frame round the squares, in design pixels.

    /** @param pieces The pieces it draws with; outlives the view. */
    explicit BoardView(const PieceSet &pieces);

    std::function<void(chess::Move move)> on_move; ///< A person's move, legal in the position shown.

    /** @return The pieces it draws with. */
    const PieceSet &pieces() const
    {
        return *pieces_;
    }

    /** @param pieces The pieces to draw with from now on; outlive the view, or the next call. */
    void SetPieces(const PieceSet &pieces);

    /**
     * The position shown, and the move that led to it, which is marked. A selection or a picker is closed.
     * @param position The position.
     * @param last_move The move that led to it, or none.
     */
    void SetPosition(const chess::Position &position, std::optional<chess::Move> last_move);

    /** @return The position shown. */
    const chess::Position &position() const
    {
        return position_;
    }

    /** @param is_flipped Whether Black is at the bottom. */
    void SetFlipped(bool is_flipped);

    /** @return Whether Black is at the bottom. */
    bool flipped() const
    {
        return is_flipped_;
    }

    /** @param is_interactive Whether a person may move the side to move's pieces; off closes a selection. */
    void SetInteractive(bool is_interactive);

    /** @return Whether a person may move the side to move's pieces. */
    bool interactive() const
    {
        return is_interactive_;
    }

    /** @param arrows The arrows to draw over the pieces. */
    void SetArrows(std::vector<BoardArrow> arrows);

    /**
     * A card over the middle of the board: a game's result and how it ended. A press anywhere on the board hides it.
     * @param title The large line, "1-0"; empty for no card.
     * @param detail The line under it, "White mates".
     */
    void SetBanner(std::string title, std::string detail);

    /** @return The card's large line; empty while none shows. */
    const std::string &banner() const
    {
        return banner_title_;
    }

    /** @return The arrows drawn. */
    const std::vector<BoardArrow> &arrows() const
    {
        return arrows_;
    }

    /** @return A square's side, in design pixels, for the view's size. */
    float SquareSize() const;

    /** @return The squares' area, inside the frame and clear of the coordinates. */
    reckless::gfx::RectF BoardArea() const;

    /**
     * Where a square is drawn.
     * @param square The square.
     * @return Its rectangle, in the view's coordinates.
     */
    reckless::gfx::RectF SquareBounds(chess::Square square) const;

    /**
     * The square under a point.
     * @param point The point, in the view's coordinates.
     * @return The square, or none off the board.
     */
    std::optional<chess::Square> SquareAt(reckless::gfx::PointF point) const;

    /** @return The selected square, while a piece is selected. */
    std::optional<chess::Square> selected() const
    {
        return selected_;
    }

    /** @return The squares the selected piece may go to. */
    const std::vector<chess::Square> &targets() const
    {
        return targets_;
    }

    /** @return The promotion picker's moves, from the top of its column, while it is open; empty otherwise. */
    const std::vector<chess::Move> &promotion_choices() const
    {
        return promotion_choices_;
    }

    /**
     * Where a promotion choice is drawn, while the picker is open.
     * @param index The choice, from 0.
     * @return Its square's rectangle.
     */
    reckless::gfx::RectF PromotionChoiceBounds(std::size_t index) const;

    /**
     * The squares, the marks, the pieces, the arrows, the piece being dragged and the picker.
     * @param c The canvas.
     */
    void Paint(reckless::gfx::Canvas &c) override;

    /**
     * Selects, moves, or starts a drag.
     * @param e The press.
     */
    void MouseDown(const reckless::view::MouseEvent &e) override;

    /**
     * Drags the selected piece.
     * @param e The drag.
     */
    void MouseDrag(const reckless::view::MouseEvent &e) override;

    /**
     * Drops a dragged piece: a move onto a target, else back where it was.
     * @param e The release.
     */
    void MouseUp(const reckless::view::MouseEvent &e) override;

  private:
    /** @return The side to move's king's square, when it is in check. */
    std::optional<chess::Square> CheckedKing() const;

    /**
     * Select a square's piece and find where it may go.
     * @param square The square.
     */
    void Select(chess::Square square);

    /** Close the selection, a drag and the picker. */
    void ClearSelection();

    /**
     * Make the move from one square to another: hand it on, or open the picker for a promotion.
     * @param from The piece's square.
     * @param to Where it goes.
     */
    void MoveOrPromote(chess::Square from, chess::Square to);

    /**
     * Draw an arrow.
     * @param c The canvas.
     * @param arrow The arrow.
     */
    void DrawArrow(reckless::gfx::Canvas &c, const BoardArrow &arrow) const;

    const PieceSet              *pieces_;                 ///< The pieces.
    chess::Position              position_;               ///< The position shown.
    std::optional<chess::Move>   last_move_;              ///< The move that led to it.
    bool                         is_flipped_     = false; ///< Black at the bottom.
    bool                         is_interactive_ = false; ///< A person may move.
    std::vector<BoardArrow>      arrows_;                 ///< The arrows.
    std::optional<chess::Square> selected_;               ///< The selected piece's square.
    std::vector<chess::Square>   targets_;                ///< Where it may go.
    bool                         is_dragging_ = false;    ///< The selected piece follows the pointer.
    reckless::gfx::PointF        drag_point_{};           ///< Where the pointer is during a drag.
    std::vector<chess::Move>     promotion_choices_;      ///< The picker's moves, while it is open.
    std::string                  banner_title_;           ///< The result card's large line.
    std::string                  banner_detail_;          ///< The line under it.
};

} // namespace amphitheater::app
