#pragma once
/// @file ChessTypes.h Pieces and colours.
///
/// Adapted from pawnstar's src/constants.h (commit c41099798db594ae2c379cba94d917c9c28d9c04): the piece and colour
/// enumerations and the move-list capacity, without the search's constants. Pieces carry no colour; the colour of
/// the piece on a square is read from the position's per-colour bitboards.

#include <cstdint>

namespace uci_host::chess
{

/** The six kinds of piece, and none. */
enum Piece : std::uint8_t
{
    kNone,   ///< No piece: an empty square, or a move that promotes to nothing.
    kPawn,   ///< A pawn.
    kKnight, ///< A knight.
    kBishop, ///< A bishop.
    kRook,   ///< A rook.
    kQueen,  ///< A queen.
    kKing,   ///< A king.
};

/** The two sides. */
enum Color : std::uint8_t
{
    kWhite, ///< The side that moves first.
    kBlack, ///< The side that moves second.
};

/**
 * The other side.
 * @param color A side.
 * @return Its opponent.
 */
constexpr Color EnemyOf(Color color)
{
    return color == Color::kWhite ? Color::kBlack : Color::kWhite;
}

/** The most moves any position can have, with room to spare: the largest known count of legal moves is 218. */
constexpr int kMaxMovesPerPosition = 256;

/**
 * A piece's upper-case letter, as SAN and FEN write white's pieces.
 * @param piece A piece, not kNone.
 * @return 'P', 'N', 'B', 'R', 'Q' or 'K'.
 */
constexpr char PieceLetter(Piece piece)
{
    return " PNBRQK"[piece];
}

/**
 * Read a piece letter, ignoring case.
 * @param letter One of PNBRQK or pnbrqk.
 * @return The piece, or kNone for any other character.
 */
constexpr Piece PieceFromLetter(char letter)
{
    switch (letter | 0x20)
    {
    case 'p':
        return kPawn;
    case 'n':
        return kKnight;
    case 'b':
        return kBishop;
    case 'r':
        return kRook;
    case 'q':
        return kQueen;
    case 'k':
        return kKing;
    default:
        return kNone;
    }
}

} // namespace uci_host::chess
