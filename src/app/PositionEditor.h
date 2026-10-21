#pragma once
/// @file PositionEditor.h Edit > Set up position: a board to put pieces on, the twelve pieces to choose from, the side
/// to move, the castling rights and the en passant square the pieces allow, the move number, and the position's FEN to
/// read or type, with the reason the position is not a legal one until it is. Set up shows the position on the board,
/// as a pasted FEN does.

#include "Controls.h"
#include "PieceSet.h"
#include "chess/BoardSetup.h"

#include "TextField.h"
#include "View.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace amphitheater::app
{

/**
 * The board a position is set up on. A click on a square puts the chosen piece there, or, where that piece already
 * stands, takes it off; a right-click empties a square; a piece dragged to another square moves there, and off the
 * board is taken off. It shows a setup it does not own and tells what a person does.
 */
class SetupBoardView final : public reckless::view::View
{
  public:
    static constexpr int   kRankGutter    = 18;   ///< The rank numbers' column, in design pixels.
    static constexpr int   kFileGutter    = 20;   ///< The file letters' row.
    static constexpr float kDragThreshold = 4.0f; ///< How far a press moves before it is a drag.

    /**
     * @param pieces The pieces it draws with; outlives the view.
     * @param setup The setup it shows; outlives the view.
     */
    SetupBoardView(const PieceSet &pieces, const chess::BoardSetup &setup);

    std::function<void(chess::Square square)>                 on_click;    ///< A square clicked.
    std::function<void(chess::Square square)>                 on_clear;    ///< A square right-clicked.
    std::function<void(chess::Square from, chess::Square to)> on_move;     ///< A piece dragged to a square.
    std::function<void(chess::Square from)>                   on_drag_off; ///< A piece dragged off the board.

    /** @param is_flipped Whether Black is at the bottom. */
    void SetFlipped(bool is_flipped);

    /** @return Whether Black is at the bottom. */
    bool flipped() const
    {
        return is_flipped_;
    }

    /** @return A square's side, in design pixels. */
    float SquareSize() const;

    /**
     * @param square A square.
     * @return Where it is drawn, in the view's coordinates.
     */
    reckless::gfx::RectF SquareBounds(chess::Square square) const;

    /**
     * @param point A point, in the view's coordinates.
     * @return The square under it, or none off the board.
     */
    std::optional<chess::Square> SquareAt(reckless::gfx::PointF point) const;

    /**
     * The squares, the coordinates and the pieces, the one being dragged under the pointer.
     * @param c The canvas.
     */
    void Paint(reckless::gfx::Canvas &c) override;

    /**
     * A press: a click, a right-click, or the start of a drag.
     * @param e The press.
     */
    void MouseDown(const reckless::view::MouseEvent &e) override;

    /**
     * A piece follows the pointer once the press has moved past kDragThreshold.
     * @param e The pointer.
     */
    void MouseDrag(const reckless::view::MouseEvent &e) override;

    /**
     * A drag dropped, or a click told.
     * @param e The release.
     */
    void MouseUp(const reckless::view::MouseEvent &e) override;

  private:
    /** @return The squares' area, inside the coordinates. */
    reckless::gfx::RectF BoardArea() const;

    const PieceSet              &pieces_;              ///< The pieces.
    const chess::BoardSetup     &setup_;               ///< The setup shown.
    bool                         is_flipped_ = false;  ///< Black at the bottom.
    std::optional<chess::Square> pressed_;             ///< The square a left press began on.
    bool                         is_dragging_ = false; ///< The pressed square's piece follows the pointer.
    reckless::gfx::PointF        drag_point_{};        ///< Where the pointer is during a drag.
};

/** The twelve pieces in two rows, White's over Black's, the one chosen ringed in the accent. */
class PiecePalette final : public reckless::view::View
{
  public:
    /** @param pieces The pieces it draws with; outlives the view. */
    explicit PiecePalette(const PieceSet &pieces);

    std::function<void(chess::SetupPiece piece)> on_choose; ///< A piece chosen.

    /** @param piece The piece chosen; nothing is told. */
    void Choose(chess::SetupPiece piece);

    /** @return The piece chosen. */
    chess::SetupPiece chosen() const
    {
        return chosen_;
    }

    /**
     * @param piece One of the twelve.
     * @return Where it is drawn, in the view's coordinates.
     */
    reckless::gfx::RectF CellBounds(chess::SetupPiece piece) const;

    /**
     * The pieces, the chosen one ringed.
     * @param c The canvas.
     */
    void Paint(reckless::gfx::Canvas &c) override;

    /**
     * A press on a piece chooses it.
     * @param e The press.
     */
    void MouseDown(const reckless::view::MouseEvent &e) override;

  private:
    const PieceSet   &pieces_;                              ///< The pieces.
    chess::SetupPiece chosen_{chess::kWhite, chess::kKing}; ///< The piece chosen.
};

/** The position's setup dialog. */
class PositionEditorDialog final : public DialogFrame
{
  public:
    /**
     * @param pieces The pieces it draws with; outlive the dialog.
     * @param position The position it starts from: the one shown.
     * @param is_flipped Whether its board starts with Black at the bottom, as the window's does.
     */
    PositionEditorDialog(const PieceSet &pieces, const chess::Position &position, bool is_flipped);

    std::function<void(const chess::Position &position)> on_set_up; ///< Set up pressed, the position legal.
    std::function<void()>                                on_close;  ///< Cancelled.

    /** @return The setup as it stands. */
    const chess::BoardSetup &setup() const
    {
        return setup_;
    }

    /**
     * Edit the setup as a person does, everything shown following it.
     * @param edit What to do to it.
     */
    void Edit(const std::function<void(chess::BoardSetup &setup)> &edit);

    /**
     * Read a FEN, tolerantly, into the setup, as typing one into the FEN line and pressing Return does.
     * @param fen The FEN.
     * @return Whether it was read; when not, the reason is shown.
     */
    bool ReadFen(const std::string &fen);

    /** Set up pressed: the position handed on when it is legal, the reason shown when it is not. */
    void SetUp();

    /** @return The message under the controls: "A legal position." or what is wrong. */
    const std::string &message() const
    {
        return message_.text();
    }

    /** @return Whether Set up may be pressed: the position is legal. */
    bool can_set_up() const;

    /** @return The board. */
    SetupBoardView &board()
    {
        return board_;
    }

    /** @return The palette. */
    PiecePalette &palette()
    {
        return palette_;
    }

    /** @return The side to move's list. */
    DropDown &side()
    {
        return side_;
    }

    /**
     * @param color A side.
     * @param is_kingside Kingside or queenside.
     * @return That castling right's box.
     */
    CheckBox &castling(chess::Color color, bool is_kingside)
    {
        return *castling_[color * 2 + (is_kingside ? 0 : 1)];
    }

    /** @return The en passant square's list: "None", then the squares the pieces allow. */
    DropDown &en_passant()
    {
        return en_passant_;
    }

    /** @return The move number's line. */
    reckless::view::TextField &move_number()
    {
        return move_number_;
    }

    /** @return The FEN's line. */
    reckless::view::TextField &fen()
    {
        return fen_;
    }

  protected:
    void LayOutContent(reckless::gfx::RectI area) override;

  private:
    /** Every control set from the setup, and the message from whether its position is legal. */
    void Refresh();

    chess::BoardSetup                        setup_;             ///< The setup being edited.
    SetupBoardView                           board_;             ///< The board.
    PiecePalette                             palette_;           ///< The pieces to put.
    Caption                                  side_label_;        ///< "Side to move".
    DropDown                                 side_;              ///< White or Black.
    Caption                                  castling_label_;    ///< "Castling".
    std::array<std::unique_ptr<CheckBox>, 4> castling_;          ///< K, Q, k, q.
    Caption                                  en_passant_label_;  ///< "En passant".
    DropDown                                 en_passant_;        ///< None, or a square the pieces allow.
    Caption                                  move_number_label_; ///< "Move number".
    reckless::view::TextField                move_number_;       ///< The move number.
    Caption                                  fen_label_;         ///< "FEN".
    reckless::view::TextField                fen_;               ///< The FEN, as it stands or typed.
    std::unique_ptr<reckless::view::Button>  start_;             ///< Start position.
    std::unique_ptr<reckless::view::Button>  clear_;             ///< Clear board.
    std::unique_ptr<reckless::view::Button>  flip_;              ///< Flip board.
    Caption                                  hint_;              ///< How the board is used.
    Caption                                  message_;           ///< A legal position, or what is wrong.
    reckless::view::Button                  *set_up_ = nullptr;  ///< The Set up button.
};

} // namespace amphitheater::app
