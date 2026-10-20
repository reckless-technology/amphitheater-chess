#pragma once
/// @file BookPanel.h The opening book's moves for the position shown, in the toolkit's list: a row a move - its SAN,
/// a bar and the share of the position's weight it holds, and its weight - the heaviest first. A press on a row plays
/// its move.

#include "chess/Move.h"
#include "chess/PolyglotBook.h"
#include "chess/Position.h"

#include "ListView.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace amphitheater::app
{

/** A move the book holds for a position. */
struct BookMove
{
    chess::Move move;       ///< The move.
    std::string san;        ///< It in SAN.
    int         weight = 0; ///< Its weight in the book.
    double      share  = 0; ///< Its weight's share of every move's there, from 0 to 1.
};

/**
 * The moves a book holds for a position, the heaviest first, each with its SAN and its share.
 * @param book The book.
 * @param position The position.
 * @return The moves; none where the book has none.
 */
std::vector<BookMove> BookMovesOf(const chess::PolyglotBook &book, const chess::Position &position);

/** The book's moves as the list shows them. */
class BookModel final : public reckless::view::ListModel
{
  public:
    /** @param moves The moves, in the order shown. */
    void SetMoves(std::vector<BookMove> moves)
    {
        moves_ = std::move(moves);
    }

    /** @return The moves. */
    const std::vector<BookMove> &moves() const
    {
        return moves_;
    }

    std::size_t RowCount() const override;
    std::string CellText(std::size_t row, std::size_t column) const override;
    bool PaintRow(reckless::gfx::Canvas &c, std::size_t row, reckless::gfx::RectI bounds, bool selected) override;

  private:
    std::vector<BookMove> moves_; ///< The moves.
};

/** The book's list: the toolkit's list over the model, which tells which move a person pressed. */
class BookPanel
{
  public:
    BookPanel();

    /** Not copyable: the list holds the model by address. */
    BookPanel(const BookPanel &) = delete;
    /** Not assignable: the list holds the model by address. @return Never. */
    BookPanel &operator=(const BookPanel &) = delete;

    std::function<void(chess::Move move)> on_move; ///< A press on a move.

    /** @param moves The moves for the position shown, in the order shown; the list scrolled to its top. */
    void SetMoves(std::vector<BookMove> moves);

    /**
     * The list's height in a room: its header and as many whole rows as fit, so no row is cut at the bottom.
     * @param room The height there is, in pixels.
     * @return The height to give the list: at most the room, and at least the header and one row.
     */
    int FittedHeight(int room) const;

    /** @return The model. */
    const BookModel &model() const
    {
        return model_;
    }

    /** @return The list, for the window to add and lay out. */
    reckless::view::ListView &view()
    {
        return list_;
    }

  private:
    BookModel                model_; ///< The moves.
    reckless::view::ListView list_;  ///< The list.
};

} // namespace amphitheater::app
