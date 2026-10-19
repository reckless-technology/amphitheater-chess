#include "Position.h"

#include <charconv>
#include <vector>

namespace uci_host::chess
{

namespace
{

/**
 * Split text into fields at single spaces (strict) or runs of whitespace (tolerant).
 * @param text The text.
 * @param is_strict Whether every separator must be exactly one space.
 * @param fields The fields, appended.
 * @return False when a strict text has a leading, trailing or doubled space.
 */
bool SplitFields(std::string_view text, bool is_strict, std::vector<std::string_view> &fields)
{
    std::size_t position = 0;
    if (is_strict)
    {
        while (true)
        {
            const std::size_t space = text.find(' ', position);
            const auto        field = text.substr(position, space - position);
            if (field.empty())
            {
                return false;
            }
            fields.push_back(field);
            if (space == std::string_view::npos)
            {
                return true;
            }
            position = space + 1;
        }
    }
    const auto is_space = [](char character) {
        return character == ' ' || character == '\t' || character == '\r' || character == '\n';
    };
    while (position < text.size())
    {
        while (position < text.size() && is_space(text[position]))
        {
            ++position;
        }
        const std::size_t start = position;
        while (position < text.size() && !is_space(text[position]))
        {
            ++position;
        }
        if (position > start)
        {
            fields.push_back(text.substr(start, position - start));
        }
    }
    return true;
}

/**
 * Read a non-negative decimal integer that is the whole of a field.
 * @param text The field.
 * @param value The number read.
 * @return Whether the field was such a number (of at most six digits).
 */
bool ParseCount(std::string_view text, int &value)
{
    if (text.empty() || text.size() > 6)
    {
        return false;
    }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && value >= 0;
}

} // namespace

Position Position::Start()
{
    return *FromFen(kStartFen);
}

Result<Position> Position::FromFen(std::string_view fen, FenMode mode)
{
    const bool                    is_strict = mode == FenMode::kStrict;
    std::vector<std::string_view> fields;
    if (!SplitFields(fen, is_strict, fields))
    {
        return Error{"a FEN's fields are separated by single spaces, with none before or after"};
    }
    if (is_strict ? fields.size() != 6 : (fields.size() < 4 || fields.size() > 6))
    {
        return Error{"a FEN has " + std::string(is_strict ? "six" : "four to six") + " fields, not " +
                     std::to_string(fields.size())};
    }

    Position position{};

    // The placement, from the eighth rank down, each rank from the a-file.
    int rank = 7;
    int file = 0;
    for (const char character : fields[0])
    {
        if (character == '/')
        {
            if (file != 8)
            {
                return Error{"rank " + std::to_string(rank + 1) + " of the placement has " + std::to_string(file) +
                             " squares, not 8"};
            }
            if (--rank < 0)
            {
                return Error{"the placement has more than eight ranks"};
            }
            file = 0;
            continue;
        }
        if (character >= '1' && character <= '8')
        {
            file += character - '0';
        }
        else
        {
            const Piece piece = PieceFromLetter(character);
            if (piece == kNone)
            {
                return Error{std::string("'") + character + "' is not a piece letter"};
            }
            if (file < 8)
            {
                position.AddPiece(character >= 'a' ? kBlack : kWhite, piece, Square{file, rank});
            }
            ++file;
        }
        if (file > 8)
        {
            return Error{"rank " + std::to_string(rank + 1) + " of the placement has more than 8 squares"};
        }
    }
    if (rank != 0 || file != 8)
    {
        return Error{"the placement does not describe eight ranks of eight squares"};
    }
    for (const Color color : {kWhite, kBlack})
    {
        const std::string side  = color == kWhite ? "white" : "black";
        const int         kings = position.Pieces(kKing, color).PopCount();
        if (kings != 1)
        {
            return Error{side + " must have exactly one king, not " + std::to_string(kings)};
        }
        if (is_strict && position.Pieces(kPawn, color).PopCount() > 8)
        {
            return Error{side + " has more than eight pawns"};
        }
        if (is_strict && position.colors_[color].PopCount() > 16)
        {
            return Error{side + " has more than sixteen pieces"};
        }
        position.king_location_[color] = position.Pieces(kKing, color).Lsb();
    }
    if ((position.pieces_[kPawn] & (kRank1 | kRank8)).IsNotEmpty())
    {
        return Error{"a pawn stands on the first or the last rank"};
    }

    // The side to move.
    const std::string_view side = fields[1];
    if (side == "w" || (!is_strict && side == "W"))
    {
        position.color_to_move_ = kWhite;
    }
    else if (side == "b" || (!is_strict && side == "B"))
    {
        position.color_to_move_ = kBlack;
    }
    else
    {
        return Error{"the side to move is 'w' or 'b', not '" + std::string(side) + "'"};
    }
    const Color mover = EnemyOf(position.color_to_move_);
    if (position.IsAttacked(position.king_location_[mover], position.color_to_move_))
    {
        return Error{"the side not to move is in check"};
    }

    // Castling: KQkq, each checked against the king and the rook it needs. A tolerant read takes a Shredder-FEN's
    // corner files too (H and A for white, h and a for black).
    const std::string_view castling = fields[2];
    std::uint8_t           rights   = 0;
    if (castling != "-")
    {
        for (const char character : castling)
        {
            char letter = character;
            if (!is_strict)
            {
                letter = letter == 'H' ? 'K' : letter == 'A' ? 'Q' : letter == 'h' ? 'k' : letter == 'a' ? 'q' : letter;
            }
            const std::string_view flags = "KQkq";
            const auto             index = flags.find(letter);
            if (index == std::string_view::npos)
            {
                return Error{std::string("'") + character + "' is not a castling right"};
            }
            const std::uint8_t flag = static_cast<std::uint8_t>(1 << index);
            if ((rights & flag) != 0 && is_strict)
            {
                return Error{std::string("castling right '") + character + "' appears twice"};
            }
            rights |= flag;
        }
    }
    position.castling_rights_ = CastlingRights{rights};
    for (const Color color : {kWhite, kBlack})
    {
        const int back_rank = color == kWhite ? 0 : 7;
        for (const bool is_kingside : {true, false})
        {
            if (!position.castling_rights_.May(color, is_kingside))
            {
                continue;
            }
            const Square king{4, back_rank};
            const Square rook{is_kingside ? 7 : 0, back_rank};
            const bool   is_allowed = position.king_location_[color] == king && position.PieceOn(rook) == kRook &&
                                      position.ColorOn(rook) == color;
            if (is_allowed)
            {
                continue;
            }
            if (is_strict)
            {
                return Error{std::string("castling right '") + "KQkq"[color * 2 + (is_kingside ? 0 : 1)] +
                             "' has no king and rook to use it"};
            }
            position.castling_rights_.Revoke(color, is_kingside);
        }
    }

    // The en passant square: behind a pawn of the side not to move that has just made a double step.
    const std::string_view en_passant = fields[3];
    position.en_passant_square_       = Square{0};
    if (en_passant != "-")
    {
        const auto square = Square::Parse(en_passant);
        if (!square)
        {
            return Error{"'" + std::string(en_passant) + "' is not an en passant square"};
        }
        const int  forward  = mover == kWhite ? 8 : -8;
        const int  rank_ok  = mover == kWhite ? 2 : 5;
        const bool is_valid = square->Rank() == rank_ok && position.PieceOn(*square) == kNone &&
                              position.PieceOn(Square{*square - forward}) == kNone &&
                              position.PieceOn(Square{*square + forward}) == kPawn &&
                              position.ColorOn(Square{*square + forward}) == mover;
        if (is_valid)
        {
            position.en_passant_square_ = *square;
        }
        else if (is_strict)
        {
            return Error{"no pawn has just made a double step past " + std::string(en_passant)};
        }
    }

    // The counters, which an EPD leaves out.
    position.reversible_move_count_ = 0;
    position.full_move_count_       = 0;
    if (fields.size() >= 5)
    {
        int halfmove_clock = 0;
        if (!ParseCount(fields[4], halfmove_clock))
        {
            return Error{"the halfmove clock is a whole number, not '" + std::string(fields[4]) + "'"};
        }
        position.reversible_move_count_ = halfmove_clock;
    }
    if (fields.size() == 6)
    {
        int fullmove_number = 0;
        if (!ParseCount(fields[5], fullmove_number) || (is_strict && fullmove_number == 0))
        {
            return Error{"the move number is a positive whole number, not '" + std::string(fields[5]) + "'"};
        }
        position.full_move_count_ = fullmove_number == 0 ? 0 : fullmove_number - 1;
    }

    position.hash_     = position.ComputeHash();
    position.checkers_ = position.AttacksTo(position.king_location_[position.color_to_move_], mover);
    return position;
}

std::string Position::ToFen() const
{
    std::string fen;
    for (int rank = 7; rank >= 0; --rank)
    {
        int empty = 0;
        for (int file = 0; file < 8; ++file)
        {
            const Square square{file, rank};
            const Piece  piece = squares_[square];
            if (piece == kNone)
            {
                ++empty;
                continue;
            }
            if (empty > 0)
            {
                fen += static_cast<char>('0' + empty);
                empty = 0;
            }
            fen += ColorOn(square) == kWhite ? " PNBRQK"[piece] : " pnbrqk"[piece];
        }
        if (empty > 0)
        {
            fen += static_cast<char>('0' + empty);
        }
        if (rank > 0)
        {
            fen += '/';
        }
    }
    fen += color_to_move_ == kWhite ? " w " : " b ";
    fen += castling_rights_.ToFenString();
    fen += ' ';
    fen += en_passant_square_ == 0 ? std::string("-") : en_passant_square_.ToString();
    fen += ' ' + std::to_string(reversible_move_count_) + ' ' + std::to_string(full_move_count_ + 1);
    return fen;
}

bool Position::HasLegalEnPassantCapture() const
{
    if (en_passant_square_ == 0)
    {
        return false;
    }
    for (const Move &move : GenerateLegalCaptures())
    {
        if (move.MoveType() == Move::kEpCapture)
        {
            return true;
        }
    }
    return false;
}

zobrist_t Position::RepetitionKey() const
{
    if (en_passant_square_ != 0 && !HasLegalEnPassantCapture())
    {
        return hash_ ^ kEnPassantHashes[en_passant_square_];
    }
    return hash_;
}

bool Position::IsCheckmate() const
{
    return IsInCheck() && GenerateLegalMoves().size() == 0;
}

bool Position::IsStalemate() const
{
    return !IsInCheck() && GenerateLegalMoves().size() == 0;
}

bool Position::HasInsufficientMaterial() const
{
    if ((pieces_[kPawn] | pieces_[kRook] | pieces_[kQueen]).IsNotEmpty())
    {
        return false;
    }
    const Bitboard knights = pieces_[kKnight];
    const Bitboard bishops = pieces_[kBishop];
    if ((knights | bishops).PopCount() <= 1)
    {
        return true;
    }
    // Bishops alone, every one on squares of one colour, can never give mate.
    return knights.IsEmpty() && ((bishops & kWhiteSquares).IsEmpty() || (bishops & kBlackSquares).IsEmpty());
}

bool Position::operator==(const Position &that) const
{
    return squares_ == that.squares_ && colors_[kWhite] == that.colors_[kWhite] &&
           color_to_move_ == that.color_to_move_ && castling_rights_.Value() == that.castling_rights_.Value() &&
           en_passant_square_ == that.en_passant_square_ && reversible_move_count_ == that.reversible_move_count_ &&
           full_move_count_ == that.full_move_count_;
}

} // namespace uci_host::chess
