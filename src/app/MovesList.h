#pragma once
/// @file MovesList.h The game's moves in the toolkit's list: a row a move number, White's move and Black's beside it,
/// the move shown on the board lit, and under a move's row a row for each variation that leaves the line there - its
/// first few moves, dimmed - which a press enters.

#include "ListView.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace amphitheater::app
{

/** A variation shown under the moves: an alternative to one of the line's moves. */
struct VariationRow
{
    int         move_index = 0; ///< The line's move it is an alternative to, from 0: shown under that move's row.
    std::string text;           ///< Its first moves, "(12...a5 13.Nd5 Nxd5 ...)"; "main line:" before the main line's.
    int         id = 0;         ///< Its handle, handed back when it is pressed.
};

/** The moves as the list shows them, and the move the board shows. */
class MovesModel final : public reckless::view::ListModel
{
  public:
    /**
     * The moves.
     * @param sans Every move in SAN, in order.
     * @param first_number The first move's number (a FEN's fullmove number).
     * @param is_black_first Whether the first move is Black's: its row's White cell then shows "...".
     * @param variations The variations to show under the moves, in the line's order.
     */
    void SetMoves(std::vector<std::string> sans, int first_number, bool is_black_first,
                  std::vector<VariationRow> variations = {});

    /** @param ply The position the board shows: 0 for the start, n after the n-th move. */
    void SetCurrentPly(int ply)
    {
        current_ply_ = ply;
    }

    /** @return The position the board shows. */
    int current_ply() const
    {
        return current_ply_;
    }

    /** @return How many moves there are. */
    int move_count() const
    {
        return static_cast<int>(sans_.size());
    }

    /**
     * Where a move is in the list.
     * @param move_index The move, from 0.
     * @return Its row, and its column (1 for White, 2 for Black).
     */
    std::pair<std::size_t, std::size_t> CellOf(int move_index) const;

    /**
     * The move in a cell.
     * @param row The row.
     * @param column The column.
     * @return The move, from 0; none for the number's column, a "..." cell or an empty one.
     */
    std::optional<int> MoveAt(std::size_t row, std::size_t column) const;

    /**
     * The variation a row shows.
     * @param row The row.
     * @return Its handle; none for a row of moves.
     */
    std::optional<int> VariationAt(std::size_t row) const;

    std::size_t                         RowCount() const override;
    std::string                         CellText(std::size_t row, std::size_t column) const override;
    std::optional<reckless::gfx::Color> CellColour(std::size_t row, std::size_t column) const override;
    std::optional<reckless::gfx::Color> CellBackground(std::size_t row, std::size_t column) const override;
    bool PaintRow(reckless::gfx::Canvas &c, std::size_t row, reckless::gfx::RectI bounds, bool selected) override;

  private:
    /** A row of the list: a pair of moves, or a variation. */
    struct Row
    {
        int move_row  = 0;  ///< The pair of moves it shows or follows, from 0.
        int variation = -1; ///< The variation it shows, an index into variations_; -1 for a row of moves.
    };

    /** The rows laid out again from the moves and the variations. */
    void Lay();

    std::vector<std::string>  sans_;                   ///< The moves.
    int                       first_number_   = 1;     ///< The first move's number.
    bool                      is_black_first_ = false; ///< Whether the first move is Black's.
    int                       current_ply_    = 0;     ///< The position the board shows.
    std::vector<VariationRow> variations_;             ///< The variations shown.
    std::vector<Row>          rows_;                   ///< The rows, in order.
};

/** The list of moves: the toolkit's list over the model, which tells which move a person pressed. */
class MovesList
{
  public:
    MovesList();

    /** Not copyable: the list holds the model by address. */
    MovesList(const MovesList &) = delete;
    /** Not assignable: the list holds the model by address. @return Never. */
    MovesList &operator=(const MovesList &) = delete;

    std::function<void(int ply)> on_select;    ///< A press on a move: the position after it, as a ply.
    std::function<void(int id)>  on_variation; ///< A press on a variation: its handle.

    /**
     * The moves and the position shown, the list scrolled to keep it in view.
     * @param sans Every move in SAN.
     * @param first_number The first move's number.
     * @param is_black_first Whether the first move is Black's.
     * @param current_ply The position the board shows.
     * @param variations The variations to show under the moves.
     */
    void SetMoves(std::vector<std::string> sans, int first_number, bool is_black_first, int current_ply,
                  std::vector<VariationRow> variations = {});

    /** @param ply The position the board shows; the list scrolls to it. */
    void SetCurrentPly(int ply);

    /**
     * The list's height in a room: its header and as many whole rows as fit, so a list kept on the move shown never
     * leaves a part of a row under the header.
     * @param room The height there is, in pixels.
     * @return The height to give the list: at most the room, and at least the header and one row.
     */
    int FittedHeight(int room) const;

    /** @return The list, for the window to add and lay out. */
    reckless::view::ListView &view()
    {
        return list_;
    }

    /** @return The model. */
    const MovesModel &model() const
    {
        return model_;
    }

  private:
    MovesModel               model_; ///< The moves.
    reckless::view::ListView list_;  ///< The list.
};

} // namespace amphitheater::app
