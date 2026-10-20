#pragma once
/// @file BoardSetup.h A position being set up square by square - the pieces where a person puts them, the side to
/// move, the castling rights, the en passant square and the counters - whether or not it makes a legal position yet:
/// what the window's Set up position dialog edits. Its FEN is read back strictly (Position::FromFen), so what it makes
/// is a position the host can play from, or the reason it is not one. A castling right or an en passant square the
/// pieces no longer allow is dropped as the pieces move, so those two never stand in the way.

#include "Position.h"
#include "Result.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace amphitheater::chess
{

/** What stands on a square being set up: a piece and its colour, or kNone for nothing. */
struct SetupPiece
{
    Color color = kWhite; ///< The piece's colour; nothing when the piece is kNone.
    Piece piece = kNone;  ///< The piece, or kNone for an empty square.

    /**
     * Whether two squares hold the same.
     * @param a One.
     * @param b The other.
     * @return Whether both are empty, or the same piece of the same colour.
     */
    friend bool operator==(const SetupPiece &a, const SetupPiece &b)
    {
        return a.piece == b.piece && (a.piece == kNone || a.color == b.color);
    }
};

/** A position being set up. */
class BoardSetup
{
  public:
    /** An empty board, White to move, no castling rights, no en passant square, move 1. */
    BoardSetup() = default;

    /**
     * A position's pieces and fields, to edit.
     * @param position The position.
     * @return The setup; its FEN is the position's.
     */
    static BoardSetup FromPosition(const Position &position);

    /** @return The starting position, to edit. */
    static BoardSetup Start();

    /**
     * @param square A square.
     * @return What stands on it.
     */
    SetupPiece At(Square square) const
    {
        return squares_[square];
    }

    /**
     * Put a piece on a square, replacing what stood there, or empty it.
     * @param square The square.
     * @param piece The piece; kNone empties the square.
     */
    void Put(Square square, SetupPiece piece);

    /**
     * Move what stands on one square to another, replacing what stood there; the first square is left empty.
     * @param from The square it leaves.
     * @param to The square it goes to.
     */
    void Move(Square from, Square to);

    /** Every piece taken off: castling rights and the en passant square go with them. */
    void Clear();

    /** @return The side to move. */
    Color side_to_move() const
    {
        return side_to_move_;
    }

    /** @param side The side to move; an en passant square it no longer allows is dropped. */
    void SetSideToMove(Color side);

    /**
     * @param color A side.
     * @param is_kingside Kingside (O-O) or queenside (O-O-O).
     * @return Whether that side keeps that right.
     */
    bool castling(Color color, bool is_kingside) const
    {
        return castling_[color * 2 + (is_kingside ? 0 : 1)];
    }

    /**
     * @param color A side.
     * @param is_kingside Kingside or queenside.
     * @return Whether the pieces allow the right: the king on its first square, the rook in its corner.
     */
    bool CanCastle(Color color, bool is_kingside) const;

    /**
     * Give or take away a castling right; one the pieces do not allow is never given.
     * @param color A side.
     * @param is_kingside Kingside or queenside.
     * @param may Whether the side keeps it.
     */
    void SetCastling(Color color, bool is_kingside, bool may);

    /** @return The en passant square: behind a pawn that has just made a double step; none for none. */
    std::optional<Square> en_passant() const
    {
        return en_passant_;
    }

    /**
     * @return The squares the side not to move's pawns may just have passed with a double step: behind each such pawn
     *         on its fourth rank, the two squares it crossed empty. In file order.
     */
    std::vector<Square> EnPassantSquares() const;

    /** @param square The en passant square, one of EnPassantSquares(); any other is ignored; none for none. */
    void SetEnPassant(std::optional<Square> square);

    /** @return The moves since the last capture or pawn move, as the fifty-move rule counts them. */
    int halfmove_clock() const
    {
        return halfmove_clock_;
    }

    /** @param count The halfmove clock, held at 0 or more. */
    void SetHalfmoveClock(int count);

    /** @return The move number. */
    int move_number() const
    {
        return move_number_;
    }

    /** @param number The move number, held at 1 or more. */
    void SetMoveNumber(int number);

    /** @return The setup as a FEN, legal or not. */
    std::string ToFen() const;

    /**
     * The position the setup makes, read back strictly.
     * @return The position, or why it is not a legal one ("white must have exactly one king, not 0").
     */
    Result<Position> ToPosition() const;

  private:
    /** Castling rights and an en passant square the pieces no longer allow, dropped. */
    void Prune();

    std::array<SetupPiece, 64> squares_{};                                   ///< By square, a1 first.
    Color                      side_to_move_ = kWhite;                       ///< The side to move.
    std::array<bool, 4>        castling_     = {false, false, false, false}; ///< K, Q, k, q.
    std::optional<Square>      en_passant_;                                  ///< The en passant square.
    int                        halfmove_clock_ = 0;                          ///< The fifty-move rule's count.
    int                        move_number_    = 1;                          ///< The move number.
};

} // namespace amphitheater::chess
