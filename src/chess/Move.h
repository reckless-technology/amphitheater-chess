#pragma once
/// @file Move.h Types and functions for using chess moves.
///
/// Adapted from pawnstar's src/move.h (commit c41099798db594ae2c379cba94d917c9c28d9c04): the move's identity - its
/// squares, its piece, what it captures and its type - in the same bit layout, without the bits the search packs
/// above them (the gives-check flag, the transposition table's node type, depth and age, the ordering score), and
/// with the accessors named in the host's style.

#include "ChessTypes.h"
#include "Square.h"
#include "StackList.h"

#include <cstdint>
#include <string>

namespace uci_host::chess
{

/**
 * A chess move in 22 bits of a 32-bit word:
 *
 *   Bits  | Meaning
 * --------|--------
 *  0 -  5 | to square
 *  6 - 11 | from square
 * 12 - 14 | moving piece
 * 15 - 17 | captured piece (for a capture)
 * 18 - 21 | move type
 *
 * A castling move is the king's move, from its square to its destination (e1g1); classical chess only.
 */
class Move
{
  private:
    static constexpr int kToShift       = 0;  ///< Bits 0-5: the to square.
    static constexpr int kFromShift     = 6;  ///< Bits 6-11: the from square.
    static constexpr int kPieceShift    = 12; ///< Bits 12-14: the moving piece.
    static constexpr int kCapturedShift = 15; ///< Bits 15-17: the captured piece.
    static constexpr int kTypeShift     = 18; ///< Bits 18-21: the move type.

    static constexpr std::uint32_t kSquareMask = 0x3F; ///< A 6-bit square field.
    static constexpr std::uint32_t kPieceMask  = 0x07; ///< A 3-bit piece field.
    static constexpr std::uint32_t kTypeMask   = 0x0F; ///< The 4-bit move-type field.

    std::uint32_t value_; ///< The packed fields.

  public:
    /** Move types. */
    enum Type : std::uint8_t
    {
        kNonCapture,                ///< A move that captures nothing (a single pawn push among them).
        kCapture,                   ///< A capture.
        kPromotionKnight = kKnight, ///< Promotion to a knight, capturing or not.
        kPromotionBishop = kBishop, ///< Promotion to a bishop, capturing or not.
        kPromotionRook   = kRook,   ///< Promotion to a rook, capturing or not.
        kPromotionQueen  = kQueen,  ///< Promotion to a queen, capturing or not.
        kPawnDoublePush,            ///< A pawn's double step, which leaves an en passant square.
        kEpCapture,                 ///< An en passant capture.
        kCastling,                  ///< Castling, as the king's move.
    };

    /** An uninitialised move, so arrays of moves cost nothing to make; Move::None() is the null move. */
    constexpr Move()
    {
    }

    /**
     * Moves are equal when every field is.
     * @param that The other move.
     * @return Whether they are the same move.
     */
    constexpr bool operator==(const Move &that) const
    {
        return value_ == that.value_;
    }

    /** @return The destination square. */
    constexpr Square To() const
    {
        return Square{static_cast<int>(value_ & kSquareMask)};
    }

    /** @return The source square. */
    constexpr Square From() const
    {
        return Square{static_cast<int>((value_ >> kFromShift) & kSquareMask)};
    }

    /** @return The move's type. */
    constexpr Type MoveType() const
    {
        return static_cast<Type>((value_ >> kTypeShift) & kTypeMask);
    }

    /** @return The moving piece. */
    constexpr Piece MovingPiece() const
    {
        return static_cast<Piece>((value_ >> kPieceShift) & kPieceMask);
    }

    /** @return The captured piece (a pawn for en passant), or kNone. */
    constexpr Piece CapturedPiece() const
    {
        return static_cast<Piece>((value_ >> kCapturedShift) & kPieceMask);
    }

    /** @return The piece a promotion makes, or kNone. */
    constexpr Piece PromotedPiece() const
    {
        switch (MoveType())
        {
        case kPromotionKnight:
            return kKnight;
        case kPromotionBishop:
            return kBishop;
        case kPromotionRook:
            return kRook;
        case kPromotionQueen:
            return kQueen;
        default:
            return kNone;
        }
    }

    /** @return Whether this is the null move, Move::None(). */
    constexpr bool IsNone() const
    {
        return value_ == 0;
    }

    /** @return The packed word. */
    constexpr std::uint32_t Bits() const
    {
        return value_;
    }

    /** @return The null move, which no position has among its legal moves. */
    static constexpr Move None()
    {
        return Move{std::uint32_t{0}};
    }

    /**
     * A move that captures nothing.
     * @param from The source square.
     * @param to The destination square.
     * @param piece The moving piece.
     * @return The move.
     */
    static constexpr Move NonCapture(Square from, Square to, Piece piece)
    {
        return Move{from, to, piece, kNonCapture, kNone};
    }

    /**
     * A capture.
     * @param from The source square.
     * @param to The destination square.
     * @param piece The moving piece.
     * @param captured The captured piece.
     * @return The move.
     */
    static constexpr Move Capture(Square from, Square to, Piece piece, Piece captured)
    {
        return Move{from, to, piece, kCapture, captured};
    }

    /**
     * A promotion, capturing or not.
     * @param from The pawn's square.
     * @param to The promotion square.
     * @param promoted The piece the pawn becomes.
     * @param captured The captured piece, or kNone.
     * @return The move.
     */
    static constexpr Move Promotion(Square from, Square to, Piece promoted, Piece captured = kNone)
    {
        return Move{from, to, kPawn, static_cast<Type>(promoted), captured};
    }

    /**
     * Castling.
     * @param from The king's square.
     * @param to The king's destination.
     * @return The move.
     */
    static constexpr Move Castling(Square from, Square to)
    {
        return Move{from, to, kKing, kCastling, kNone};
    }

    /**
     * An en passant capture.
     * @param from The capturing pawn's square.
     * @param to The en passant square.
     * @return The move.
     */
    static constexpr Move EpCapture(Square from, Square to)
    {
        return Move{from, to, kPawn, kEpCapture, kPawn};
    }

    /**
     * A pawn's double step.
     * @param from The pawn's square.
     * @param to Two squares ahead.
     * @return The move.
     */
    static constexpr Move DoublePush(Square from, Square to)
    {
        return Move{from, to, kPawn, kPawnDoublePush, kNone};
    }

    /** @return The move in UCI's long algebraic notation ("e1g1", "a7a8q"); "0000" for the null move. */
    std::string ToUci() const
    {
        if (IsNone())
        {
            return "0000";
        }
        std::string result = From().ToString() + To().ToString();
        if (PromotedPiece() != kNone)
        {
            result.push_back(" pnbrqk"[PromotedPiece()]);
        }
        return result;
    }

  private:
    /**
     * A move from its fields.
     * @param from The source square.
     * @param to The destination square.
     * @param piece The moving piece.
     * @param type The move type.
     * @param captured The captured piece, or kNone.
     */
    constexpr Move(Square from, Square to, Piece piece, Type type, Piece captured)
        : value_(static_cast<std::uint32_t>(to) | (static_cast<std::uint32_t>(from) << kFromShift) |
                 (static_cast<std::uint32_t>(piece) << kPieceShift) |
                 (static_cast<std::uint32_t>(captured) << kCapturedShift) |
                 (static_cast<std::uint32_t>(type) << kTypeShift))
    {
    }

    /** @param value The packed word. */
    explicit constexpr Move(std::uint32_t value) : value_(value)
    {
    }
};

/** The legal moves of a position, on the stack: at most kMaxMovesPerPosition. */
using MoveList = StackList<Move, kMaxMovesPerPosition>;

} // namespace uci_host::chess
