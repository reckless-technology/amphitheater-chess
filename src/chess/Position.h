#pragma once
/// @file Position.h Chess position representation and legal move generation.
///
/// Adapted from pawnstar's src/position.h (commit c41099798db594ae2c379cba94d917c9c28d9c04): the representation,
/// the legal move generator (check evasions, pins, the en passant discovered check), copy-make and the Zobrist hash
/// are pawnstar's. What the host changes:
///  - FEN is read by FromFen, which validates the text (strictly or tolerantly) and says what is wrong with it,
///    where pawnstar falls back to the starting position; pawnstar's generator trusts its castling rights and en
///    passant square, so FromFen drops or refuses any the pieces do not allow.
///  - The two counters are ints, not bytes: a game the host plays may pass move 255.
///  - Castling advances the halfmove clock, as the fifty-move rule counts it (only captures and pawn moves reset
///    it); pawnstar resets it there, which is harmless to a search and wrong in a game record.
///  - RepetitionKey leaves the en passant square out of the hash unless an en passant capture is legal, as the
///    repetition rule compares positions; pawnstar's hash holds the square after every double step.
///  - No null move, no captures-only generation outside the generator's template, no debug counters.
///
/// Classical chess only: castling is the king from e1 or e8 to the g- or c-file, with the rook from the corner.

#include "Attacks.h"
#include "Bitboard.h"
#include "CastlingRights.h"
#include "Move.h"
#include "Result.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace amphitheater::chess
{

/** How strictly Position::FromFen reads a FEN. */
enum class FenMode
{
    /**
     * All six fields separated by single spaces, each exactly as the standard writes it, and a position that could
     * arise: a king each, at most eight pawns and sixteen pieces a side, no pawn on the first or last rank, castling
     * rights the pieces allow and an en passant square a double step made.
     */
    kStrict,
    /**
     * The four position fields at least (an EPD's), the counters defaulting to 0 and 1; any whitespace between
     * fields; castling rights or an en passant square the pieces do not allow dropped rather than refused. A king each,
     * no pawn on the first or last rank and the side not to move out of check are still required: the move generator
     * cannot work without them.
     */
    kTolerant,
};

/**
 * A chess position: the state of the board and the rules of chess. Copy-make: MakeMove returns the position after
 * the move and leaves this one as it was. 168 bytes.
 */
class Position
{
  public:
    // State variables, public as pawnstar has them: Pins reads them.
    std::array<Bitboard, 7> pieces_;                ///< Bitboards indexed by Piece (index 0 = occupied squares).
    std::array<Bitboard, 2> colors_;                ///< Per-colour bitboards indexed by Color.
    std::array<Piece, 64>   squares_;               ///< The piece on each square, for fast lookup.
    Bitboard                checkers_;              ///< The squares of the pieces checking the side to move.
    zobrist_t               hash_;                  ///< Zobrist hash, maintained incrementally.
    std::array<Square, 2>   king_location_;         ///< Square of the white and the black king.
    Square                  en_passant_square_;     ///< En passant target square; 0 (a1) for none.
    int                     reversible_move_count_; ///< Half-moves since the last capture or pawn move.
    int                     full_move_count_;       ///< Number of full moves (zero indexed).
    CastlingRights          castling_rights_;       ///< Castling rights flags.
    Color                   color_to_move_;         ///< Side to move.

    /** The starting position's FEN. */
    static constexpr std::string_view kStartFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    /** An empty board; every position the host uses comes from FromFen or Start. */
    constexpr Position()                                = default;
    constexpr Position(const Position &that)            = default;
    constexpr Position &operator=(const Position &that) = default;

    /** @return The starting position. */
    static Position Start();

    /**
     * Read a FEN.
     * @param fen The text.
     * @param mode How strictly to read it.
     * @return The position, or what is wrong with the text.
     */
    static Result<Position> FromFen(std::string_view fen, FenMode mode = FenMode::kStrict);

    /** @return The position as FEN. */
    std::string ToFen() const;

    /**
     * Make a move.
     * @param move A legal move of this position.
     * @return The position after it.
     */
    constexpr Position MakeMove(const Move &move) const;

    /** @return Every occupied square. */
    constexpr Bitboard OccupiedSquares() const;

    /**
     * The pieces of one colour attacking a square.
     * @param location The square.
     * @param color The attacking colour.
     * @return The attackers' squares.
     */
    constexpr Bitboard AttacksTo(Square location, Color color) const;

    /** @return Every legal move of the side to move. */
    constexpr MoveList GenerateLegalMoves() const;

    /** @return The legal captures and promotions of the side to move. */
    constexpr MoveList GenerateLegalCaptures() const;

    /**
     * Whether a colour attacks a square.
     * @param s The square.
     * @param c The attacking colour.
     * @return Whether any of its pieces attacks the square.
     */
    constexpr bool IsAttacked(Square s, Color c) const;

    /** @return Whether the side to move is in check. */
    constexpr bool IsInCheck() const;

    /** @return The side to move. */
    Color SideToMove() const
    {
        return color_to_move_;
    }

    /**
     * The piece on a square.
     * @param square The square.
     * @return The piece, or kNone.
     */
    Piece PieceOn(Square square) const
    {
        return squares_[square];
    }

    /**
     * The colour of the piece on a square.
     * @param square An occupied square.
     * @return Its piece's colour.
     */
    Color ColorOn(Square square) const
    {
        return (colors_[kWhite] & Bitboard{square}).IsNotEmpty() ? kWhite : kBlack;
    }

    /**
     * The squares of one colour's pieces of one kind.
     * @param piece The kind.
     * @param color The colour.
     * @return The squares.
     */
    Bitboard Pieces(Piece piece, Color color) const
    {
        return pieces_[piece] & colors_[color];
    }

    /** @return The en passant target square, if the last move was a pawn's double step. */
    std::optional<Square> EnPassantSquare() const
    {
        return en_passant_square_ == 0 ? std::nullopt : std::optional<Square>{en_passant_square_};
    }

    /** @return The castling rights. */
    CastlingRights Castling() const
    {
        return castling_rights_;
    }

    /** @return Half-moves since the last capture or pawn move, which the fifty-move rule counts. */
    int HalfmoveClock() const
    {
        return reversible_move_count_;
    }

    /** @return The move number, starting at 1 and rising after each of black's moves. */
    int FullmoveNumber() const
    {
        return full_move_count_ + 1;
    }

    /**
     * The position's identity under the repetition rule: the pieces, the side to move, the castling rights, and the
     * en passant square only when an en passant capture is legal.
     * @return A 64-bit key.
     */
    zobrist_t RepetitionKey() const;

    /** @return Whether the side to move has a legal en passant capture. */
    bool HasLegalEnPassantCapture() const;

    /** @return Whether the side to move is checkmated. */
    bool IsCheckmate() const;

    /** @return Whether the side to move is stalemated. */
    bool IsStalemate() const;

    /**
     * Whether neither side can mate by any series of legal moves: king against king, king and one minor piece
     * against king, or kings and bishops alone with every bishop on squares of one colour.
     * @return Whether the position is drawn for want of material.
     */
    bool HasInsufficientMaterial() const;

    /**
     * Positions are equal when their pieces, side to move, castling rights, en passant square and counters are.
     * @param that The other position.
     * @return Whether they are equal.
     */
    bool operator==(const Position &that) const;

  private:
    /**
     * Place a piece on the board.
     * @param color The piece's colour.
     * @param piece The piece.
     * @param to The square.
     */
    constexpr void AddPiece(Color color, Piece piece, Square to);

    /**
     * Remove a piece from the board.
     * @param color The piece's colour.
     * @param piece The piece.
     * @param from The square.
     */
    constexpr void RemovePiece(Color color, Piece piece, Square from);

    /**
     * Move a piece on the board.
     * @param color The piece's colour.
     * @param piece The piece.
     * @param from Its square.
     * @param to Its destination.
     */
    constexpr void MovePiece(Color color, Piece piece, Square from, Square to);

    /** @return The Zobrist hash computed from scratch. */
    constexpr zobrist_t ComputeHash() const;

    /**
     * Generate legal moves.
     * @tparam color The side to move.
     * @tparam do_all_moves Every move, or captures and promotions only.
     * @return The moves.
     */
    template <Color color, bool do_all_moves> constexpr MoveList GenMoves() const;
};

constexpr MoveList Position::GenerateLegalMoves() const
{
    return color_to_move_ == kWhite ? GenMoves<kWhite, true>() : GenMoves<kBlack, true>();
}

constexpr MoveList Position::GenerateLegalCaptures() const
{
    return color_to_move_ == kWhite ? GenMoves<kWhite, false>() : GenMoves<kBlack, false>();
}

constexpr bool Position::IsAttacked(Square s, Color c) const
{
    return AttacksTo(s, c).IsNotEmpty();
}

constexpr Bitboard Position::OccupiedSquares() const
{
    return pieces_[kNone];
}

constexpr bool Position::IsInCheck() const
{
    return checkers_.IsNotEmpty();
}

constexpr Bitboard Position::AttacksTo(Square location, Color color) const
{
    const Bitboard occupied_squares = OccupiedSquares();
    Bitboard       result =
        color == kWhite ? kPawnAttacksBlack[location] & pieces_[kPawn] : kPawnAttacksWhite[location] & pieces_[kPawn];
    result |= (kKnightAttacks[location] & pieces_[kKnight]) | (kKingAttacks[location] & pieces_[kKing]);
    result |= RookAttacks(occupied_squares, location) & (pieces_[kRook] | pieces_[kQueen]);
    result |= BishopAttacks(occupied_squares, location) & (pieces_[kBishop] | pieces_[kQueen]);
    result &= colors_[color];
    return result;
}

} // namespace amphitheater::chess

#include "Pins.h"

namespace amphitheater::chess
{

template <Color color, bool do_all_moves> constexpr MoveList Position::GenMoves() const
{
    const Bitboard occupied_squares = OccupiedSquares();
    const Bitboard friendly_pieces  = colors_[color];
    const Bitboard enemy_pieces     = occupied_squares ^ friendly_pieces;
    const Bitboard enemy_pawns      = pieces_[kPawn] & enemy_pieces;
    const Square   king_locn        = king_location_[color];

    MoveList moves;

    // Determine the squares which our king cannot move to, i.e. any square which is attacked or X-ray attacked by any
    // enemy piece. Start with pawns.
    Bitboard forbidden_king_squares = color == kWhite ? enemy_pawns.ShiftSouthwest() | enemy_pawns.ShiftSoutheast()
                                                      : enemy_pawns.ShiftNorthwest() | enemy_pawns.ShiftNortheast();

    // Temporarily remove our king to detect X-ray attacks from enemy sliding pieces.
    const Bitboard occupied_except_king = occupied_squares ^ Bitboard(king_locn);

    for (auto &[piece, attack_fn] : piece_attackers)
    {
        const Bitboard b = pieces_[piece] & enemy_pieces;
        for (Square s : b)
        {
            forbidden_king_squares |= attack_fn(occupied_except_king, s);
        }
    }

    // The king may only safely move to squares which are not forbidden.
    const Bitboard king_move_targets = kKingAttacks[king_locn] & ~forbidden_king_squares;

    // Generate King moves.
    const Bitboard king_captures = king_move_targets & enemy_pieces;
    for (Square to : king_captures)
    {
        moves.push_back(Move::Capture(king_locn, to, kKing, squares_[to]));
    }
    if constexpr (do_all_moves)
    {
        const Bitboard king_non_captures = king_move_targets & ~occupied_squares;
        for (Square to : king_non_captures)
        {
            moves.push_back(Move::NonCapture(king_locn, to, kKing));
        }
    }

    Bitboard allowed_captures     = enemy_pieces;      // The set of pieces we may capture.
    Bitboard allowed_non_captures = ~occupied_squares; // The set of empty squares we may move to.

    // If we are in check then the set of possible captures and empty squares that are legal is much smaller, since we
    // must immediately get out of check.
    if (IsInCheck())
    {
        if (checkers_.PopCount() > 1)
        {
            // Multiple check: in the case of multiple check, only moving the king is possible so we are done with move
            // generation.
            return moves;
        }
        // We are in check, the options are:
        // a) Capture the checking piece, OR
        // b) If checked by a sliding piece, interpose a piece between the checker and our king.
        // This includes an en passant capture if the en passant square is between the king and the sliding checker.
        const Square checker_locn = checkers_.Lsb();
        allowed_captures          = checkers_;
        allowed_non_captures      = kInterveningSquares[king_locn][checker_locn];
    }

    const Pins pins{*this}; // Determine pinned pieces and their allowed destination squares.

    // Generate knight, bishop, rook and queen moves.
    for (auto &[piece, attack_fn] : piece_attackers_except_king)
    {
        const Bitboard b = pieces_[piece] & friendly_pieces;
        for (Square from : b)
        {
            const Bitboard attacks         = attack_fn(occupied_squares, from) & pins.AllowedSquares(from);
            const Bitboard capture_targets = attacks & allowed_captures;
            for (Square to : capture_targets)
            {
                moves.push_back(Move::Capture(from, to, piece, squares_[to]));
            }
            if constexpr (do_all_moves)
            {
                const Bitboard non_capture_targets = attacks & allowed_non_captures;
                for (Square to : non_capture_targets)
                {
                    moves.push_back(Move::NonCapture(from, to, piece));
                }
            }
        }
    }

    // Generate castling moves. The conditions for castling are:
    // 1) King must not have moved
    // 2) Rook must not have moved
    // 3) King is not in check
    // 4) Squares between King and Rook are vacant
    // 5) Square King passes through is not attacked by the enemy.
    if constexpr (do_all_moves)
    {
        if (!IsInCheck())
        {
            if constexpr (color == kWhite)
            {
                if (castling_rights_.MayWhiteCastleKingside() &&
                    (occupied_squares & (Bitboard("F1") | Bitboard("G1"))).IsEmpty() && !IsAttacked("F1", kBlack) &&
                    !IsAttacked("G1", kBlack))
                {
                    moves.push_back(Move::Castling("E1", "G1"));
                }
                if (castling_rights_.MayWhiteCastleQueenside() &&
                    (occupied_squares & (Bitboard("B1") | Bitboard("C1") | Bitboard("D1"))).IsEmpty() &&
                    !IsAttacked("D1", kBlack) && !IsAttacked("C1", kBlack))
                {
                    moves.push_back(Move::Castling("E1", "C1"));
                }
            }
            else
            {
                if (castling_rights_.MayBlackCastleKingside() &&
                    (occupied_squares & (Bitboard("F8") | Bitboard("G8"))).IsEmpty() && !IsAttacked("F8", kWhite) &&
                    !IsAttacked("G8", kWhite))
                {
                    moves.push_back(Move::Castling("E8", "G8"));
                }
                if (castling_rights_.MayBlackCastleQueenside() &&
                    (occupied_squares & (Bitboard("B8") | Bitboard("C8") | Bitboard("D8"))).IsEmpty() &&
                    !IsAttacked("D8", kWhite) && !IsAttacked("C8", kWhite))
                {
                    moves.push_back(Move::Castling("E8", "C8"));
                }
            }
        }
    }

    // Generate pawn moves.
    Bitboard pawns;              // friendly pawns
    Bitboard single_pushes;      // single push targets
    Bitboard double_pushes;      // double push targets
    Bitboard captures_west;      // capture west targets
    Bitboard captures_east;      // capture east targets
    Bitboard en_passant_sources; // pawns which can capture ep
    Bitboard promotions;         // push promotion targets
    Bitboard promotions_west;    // promotion capture west targets
    Bitboard promotions_east;    // promotion capture east targets
    int      push_delta;         // square index delta for push forward
    int      west_delta;         // square index delta for capture west
    int      east_delta;         // square index delta for capture east

    if constexpr (color == kWhite)
    {
        pawns              = pieces_[kPawn] & colors_[kWhite];
        single_pushes      = pawns.ShiftNorth() & ~occupied_squares;
        double_pushes      = single_pushes.ShiftNorth() & ~occupied_squares & kRank4;
        captures_west      = pawns.ShiftNorthwest() & colors_[kBlack];
        captures_east      = pawns.ShiftNortheast() & colors_[kBlack];
        en_passant_sources = en_passant_square_ ? kPawnAttacksBlack[en_passant_square_] & pawns : kNoSquares;
        promotions         = single_pushes & kRank8;
        promotions_west    = captures_west & kRank8;
        promotions_east    = captures_east & kRank8;
        push_delta         = 8;
        west_delta         = 7;
        east_delta         = 9;
    }
    else
    {
        pawns              = pieces_[kPawn] & colors_[kBlack];
        single_pushes      = pawns.ShiftSouth() & ~occupied_squares;
        double_pushes      = single_pushes.ShiftSouth() & ~occupied_squares & kRank5;
        captures_west      = pawns.ShiftSouthwest() & colors_[kWhite];
        captures_east      = pawns.ShiftSoutheast() & colors_[kWhite];
        en_passant_sources = en_passant_square_ ? kPawnAttacksWhite[en_passant_square_] & pawns : kNoSquares;
        promotions         = single_pushes & kRank1;
        promotions_west    = captures_west & kRank1;
        promotions_east    = captures_east & kRank1;
        push_delta         = -8;
        west_delta         = -9;
        east_delta         = -7;
    }
    captures_west ^= promotions_west;
    captures_east ^= promotions_east;
    single_pushes ^= promotions;

    // clang-format off
    const std::array<std::pair<Bitboard, int>, 2> capture_promotions
    {{
        {promotions_west, west_delta},
        {promotions_east, east_delta}
    }};
    // clang-format on
    for (auto &[bb, delta] : capture_promotions)
    {
        const Bitboard b = bb & allowed_captures;
        for (Square to : b)
        {
            const Square from = static_cast<Square>(to - delta);
            if (pins.IsAllowed(from, to))
            {
                moves.push_back(Move::Promotion(from, to, kQueen, squares_[to]));
                moves.push_back(Move::Promotion(from, to, kRook, squares_[to]));
                moves.push_back(Move::Promotion(from, to, kBishop, squares_[to]));
                moves.push_back(Move::Promotion(from, to, kKnight, squares_[to]));
            }
        }
    }
    const Bitboard push_promotions = promotions & allowed_non_captures;
    for (Square to : push_promotions)
    {
        const Square from = static_cast<Square>(to - push_delta);
        if (pins.IsAllowed(from, to))
        {
            moves.push_back(Move::Promotion(from, to, kQueen));
            moves.push_back(Move::Promotion(from, to, kRook));
            moves.push_back(Move::Promotion(from, to, kBishop));
            moves.push_back(Move::Promotion(from, to, kKnight));
        }
    }
    // clang-format off
    const std::array<std::pair<Bitboard, int>, 2> captures
    {{
        {captures_west, west_delta},
        {captures_east, east_delta}
    }};
    // clang-format on
    for (auto &[bb, delta] : captures)
    {
        const Bitboard b = bb & allowed_captures;
        for (Square to : b)
        {
            const Square from = static_cast<Square>(to - delta);
            if (pins.IsAllowed(from, to))
            {
                moves.push_back(Move::Capture(from, to, kPawn, squares_[to]));
            }
        }
    }
    if constexpr (do_all_moves)
    {
        Bitboard b = single_pushes & allowed_non_captures;
        for (Square to : b)
        {
            const Square from = static_cast<Square>(to - push_delta);
            if (pins.IsAllowed(from, to))
            {
                moves.push_back(Move::NonCapture(from, to, kPawn));
            }
        }
        b = double_pushes & allowed_non_captures;
        for (Square to : b)
        {
            const Square from = static_cast<Square>(to - push_delta * 2);
            if (pins.IsAllowed(from, to))
            {
                moves.push_back(Move::DoublePush(from, to));
            }
        }
    }
    // Generate en passant captures. En passant captures are only legal if the captured pawn location (capture target)
    // is in the capture mask. We also have to test for the special case of discovered horizontal check when removing
    // both pawns from the king's rank, if the king is on the same rank as the capturing and captured pawns, and there
    // is also an enemy rook or queen on the same rank.
    for (Square from : en_passant_sources)
    {
        const Square to = en_passant_square_;
        // The captured pawn is on the source rank, destination file.
        const Square captured_pawn_locn = static_cast<Square>((from & 0x38) | (to & 0x07));
        if (pins.IsAllowed(from, to) && (allowed_captures & Bitboard(captured_pawn_locn)).IsNotEmpty())
        {
            // Test for the weird check that occurs when there is an enemy rook or queen on the same rank as our king
            // which is only discovered after removing both pawns from the rank during an en passant capture.
            if (king_locn.Rank() == from.Rank())
            {
                // Remove the capturing and ep captured pawns from the occupied squares set and test for horizontal ray
                // attacks.
                const Bitboard pseudo_occupied_squares =
                    occupied_squares ^ Bitboard(captured_pawn_locn) ^ Bitboard(from);
                const Bitboard horizontal_attacks =
                    RookAttacks(pseudo_occupied_squares, king_locn) & kEastWest[king_locn];
                if ((horizontal_attacks & enemy_pieces & (pieces_[kRook] | pieces_[kQueen])).IsEmpty())
                {
                    // We can make this move since it's not a discovered check.
                    moves.push_back(Move::EpCapture(from, to));
                }
            }
            else
            {
                moves.push_back(Move::EpCapture(from, to));
            }
        }
    }
    return moves;
}

constexpr void Position::AddPiece(Color color, Piece piece, Square to)
{
    const Bitboard to_bb = Bitboard{to};
    pieces_[piece] ^= to_bb;
    colors_[color] ^= to_bb;
    pieces_[kNone] ^= to_bb; // pieces[kNone] holds the occupied-squares bitboard
    hash_ ^= kPieceSquareHashes[color][piece - 1][to];
    squares_[to] = piece;
}

constexpr void Position::RemovePiece(Color color, Piece piece, Square from)
{
    const Bitboard from_bb = Bitboard{from};
    pieces_[piece] ^= from_bb;
    colors_[color] ^= from_bb;
    pieces_[kNone] ^= from_bb; // pieces[kNone] holds the occupied-squares bitboard
    hash_ ^= kPieceSquareHashes[color][piece - 1][from];
    squares_[from] = Piece::kNone;
}

constexpr void Position::MovePiece(Color color, Piece piece, Square from, Square to)
{
    const Bitboard                   from_to_bb = Bitboard{from} | Bitboard{to};
    const std::array<zobrist_t, 64> &hash       = kPieceSquareHashes[color][piece - 1];
    pieces_[piece] ^= from_to_bb;
    colors_[color] ^= from_to_bb;
    pieces_[kNone] ^= from_to_bb; // pieces[kNone] holds the occupied-squares bitboard
    hash_ ^= hash[to] ^ hash[from];
    squares_[from] = Piece::kNone;
    squares_[to]   = piece;
}

constexpr Position Position::MakeMove(const Move &move) const
{
    constexpr Square G1{"G1"}; // White kingside castling king destination.
    constexpr Square C1{"C1"}; // White queenside castling king destination.
    constexpr Square G8{"G8"}; // Black kingside castling king destination.
    constexpr Square C8{"C8"}; // Black queenside castling king destination.

    const Color  color = color_to_move_;
    const Square from  = move.From();
    const Square to    = move.To();
    const Piece  piece = move.MovingPiece();

    Position position{*this};
    position.castling_rights_.AfterMove(move);
    position.hash_ ^= position.castling_rights_.Hash() ^ castling_rights_.Hash();
    position.hash_ ^= kEnPassantHashes[position.en_passant_square_];
    position.en_passant_square_ = Square{0};

    switch (move.MoveType())
    {
    case Move::Type::kNonCapture:
        // A single pawn push is encoded as kNonCapture (no dedicated type); like any pawn move it resets the
        // fifty-move / reversible-move clock. Every other kNonCapture is a reversible piece move and increments.
        if (piece == kPawn)
        {
            position.reversible_move_count_ = 0;
        }
        else
        {
            ++position.reversible_move_count_;
        }
        position.MovePiece(color, piece, from, to);
        break;

    case Move::Type::kCapture:
        position.reversible_move_count_ = 0;
        position.RemovePiece(EnemyOf(color), move.CapturedPiece(), to);
        position.MovePiece(color, piece, from, to);
        break;

    case Move::Type::kPromotionKnight:
    case Move::Type::kPromotionBishop:
    case Move::Type::kPromotionRook:
    case Move::Type::kPromotionQueen:
        position.reversible_move_count_ = 0;
        if (move.CapturedPiece() != Piece::kNone)
        {
            position.RemovePiece(EnemyOf(color), move.CapturedPiece(), to);
        }
        position.RemovePiece(color, kPawn, from);
        position.AddPiece(color, move.PromotedPiece(), to);
        break;

    case Move::Type::kPawnDoublePush:
        position.reversible_move_count_ = 0;
        position.MovePiece(color, kPawn, from, to);
        position.en_passant_square_ = Square{(from + to) >> 1};
        position.hash_ ^= kEnPassantHashes[position.en_passant_square_];
        break;

    case Move::Type::kEpCapture:
        position.reversible_move_count_ = 0;
        // En passant capture: capture location is source rank, destination file.
        position.RemovePiece(EnemyOf(color), kPawn, Square{(from & 0x38) | (to & 0x07)});
        position.MovePiece(color, kPawn, from, to);
        break;

    case Move::Type::kCastling:
        // Neither a capture nor a pawn move: the fifty-move rule keeps counting.
        ++position.reversible_move_count_;
        switch (to)
        {
        case G1:
            position.MovePiece(kWhite, kKing, "E1", "G1");
            position.MovePiece(kWhite, kRook, "H1", "F1");
            break;
        case C1:
            position.MovePiece(kWhite, kKing, "E1", "C1");
            position.MovePiece(kWhite, kRook, "A1", "D1");
            break;
        case G8:
            position.MovePiece(kBlack, kKing, "E8", "G8");
            position.MovePiece(kBlack, kRook, "H8", "F8");
            break;
        case C8:
            position.MovePiece(kBlack, kKing, "E8", "C8");
            position.MovePiece(kBlack, kRook, "A8", "D8");
            break;
        default:
            break;
        }
        break;
    }
    position.color_to_move_ = EnemyOf(color);
    position.hash_ ^= kBlackMoveHash;
    position.full_move_count_ += color; // Increments after black's move.
    position.king_location_[color] = (position.pieces_[kKing] & position.colors_[color]).Lsb();
    position.checkers_             = position.AttacksTo(position.king_location_[EnemyOf(color)], color);
    return position;
}

constexpr zobrist_t Position::ComputeHash() const
{
    zobrist_t hash = color_to_move_ == kBlack ? kBlackMoveHash : 0ull;
    hash ^= castling_rights_.Hash();
    hash ^= kEnPassantHashes[en_passant_square_];
    constexpr std::array piece_types{kPawn, kKnight, kBishop, kRook, kQueen, kKing};
    for (auto piece : piece_types)
    {
        Bitboard b = pieces_[piece] & colors_[kWhite];
        for (Square s : b)
        {
            hash ^= kPieceSquareHashes[kWhite][piece - 1][s];
        }
        b = pieces_[piece] & colors_[kBlack];
        for (Square s : b)
        {
            hash ^= kPieceSquareHashes[kBlack][piece - 1][s];
        }
    }
    return hash;
}

} // namespace amphitheater::chess
