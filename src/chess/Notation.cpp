#include "Notation.h"

#include <cctype>

namespace amphitheater::chess
{

namespace
{

/**
 * Text without the whitespace around it.
 * @param text The text.
 * @return The trimmed view.
 */
std::string_view Trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    {
        text.remove_suffix(1);
    }
    return text;
}

/**
 * Whether a castling move goes to the kingside.
 * @param move A castling move.
 * @return Whether the king goes to the g-file.
 */
bool IsKingside(Move move)
{
    return move.To().File() == 6;
}

} // namespace

std::string MoveToUci(Move move)
{
    return move.ToUci();
}

Result<Move> ParseUciMove(const Position &position, std::string_view text)
{
    text               = Trim(text);
    const auto not_uci = [&]() { return Error{"'" + std::string(text) + "' is not a move in UCI notation"}; };
    if (text.size() != 4 && text.size() != 5)
    {
        return not_uci();
    }
    const auto from = Square::Parse(text.substr(0, 2));
    const auto to   = Square::Parse(text.substr(2, 2));
    if (!from || !to)
    {
        return not_uci();
    }
    Piece promotion = kNone;
    if (text.size() == 5)
    {
        promotion = PieceFromLetter(text[4]);
        if (promotion == kNone || promotion == kPawn || promotion == kKing)
        {
            return Error{"'" + std::string(text) + "' promotes to no piece a pawn can become"};
        }
    }
    const MoveList moves = position.GenerateLegalMoves();
    for (const Move &move : moves)
    {
        if (move.From() == *from && move.To() == *to && move.PromotedPiece() == promotion)
        {
            return move;
        }
    }
    // Castling written as the king onto its own rook.
    if (promotion == kNone)
    {
        for (const Move &move : moves)
        {
            if (move.MoveType() == Move::kCastling && move.From() == *from && to->Rank() == from->Rank() &&
                to->File() == (IsKingside(move) ? 7 : 0))
            {
                return move;
            }
        }
    }
    return Error{"'" + std::string(text) + "' is not a legal move in this position"};
}

std::string MoveToSan(const Position &position, Move move)
{
    if (move.IsNone())
    {
        return "--";
    }
    std::string text;
    if (move.MoveType() == Move::kCastling)
    {
        text = IsKingside(move) ? "O-O" : "O-O-O";
    }
    else
    {
        const Square from    = move.From();
        const Square to      = move.To();
        const Piece  piece   = move.MovingPiece();
        const bool   capture = move.CapturedPiece() != kNone;
        if (piece == kPawn)
        {
            if (capture)
            {
                text += static_cast<char>('a' + from.File());
                text += 'x';
            }
            text += to.ToString();
            if (move.PromotedPiece() != kNone)
            {
                text += '=';
                text += PieceLetter(move.PromotedPiece());
            }
        }
        else
        {
            text += PieceLetter(piece);
            // The other pieces of the same kind that could also move there.
            bool is_ambiguous = false;
            bool shares_file  = false;
            bool shares_rank  = false;
            for (const Move &other : position.GenerateLegalMoves())
            {
                if (other.To() != to || other.From() == from || other.MovingPiece() != piece)
                {
                    continue;
                }
                is_ambiguous = true;
                shares_file |= other.From().File() == from.File();
                shares_rank |= other.From().Rank() == from.Rank();
            }
            if (is_ambiguous)
            {
                if (!shares_file)
                {
                    text += static_cast<char>('a' + from.File());
                }
                else if (!shares_rank)
                {
                    text += static_cast<char>('1' + from.Rank());
                }
                else
                {
                    text += from.ToString();
                }
            }
            if (capture)
            {
                text += 'x';
            }
            text += to.ToString();
        }
    }
    const Position after = position.MakeMove(move);
    if (after.IsInCheck())
    {
        text += after.GenerateLegalMoves().size() == 0 ? '#' : '+';
    }
    return text;
}

Result<Move> ParseSan(const Position &position, std::string_view original)
{
    std::string_view text = Trim(original);
    const auto illegal    = [&]() { return Error{"'" + std::string(Trim(original)) + "' is not a legal move here"}; };
    const auto as_uci     = [&]() {
        const auto uci = ParseUciMove(position, original);
        return uci.HasValue() ? uci : Result<Move>(illegal());
    };

    // Suffixes that say nothing about which move it is.
    while (true)
    {
        if (!text.empty() && (text.back() == '+' || text.back() == '#' || text.back() == '!' || text.back() == '?'))
        {
            text.remove_suffix(1);
        }
        else if (text.size() > 4 && text.substr(text.size() - 4) == "e.p.")
        {
            text = Trim(text.substr(0, text.size() - 4));
        }
        else
        {
            break;
        }
    }
    if (text.empty())
    {
        return Error{"an empty move"};
    }

    const MoveList moves = position.GenerateLegalMoves();

    // Castling, with the letter O or the digit zero, in either case.
    std::string castling;
    for (const char character : text)
    {
        castling += (character == '0' || character == 'o') ? 'O' : character;
    }
    if (castling == "O-O" || castling == "O-O-O")
    {
        for (const Move &move : moves)
        {
            if (move.MoveType() == Move::kCastling && IsKingside(move) == (castling == "O-O"))
            {
                return move;
            }
        }
        return Error{"'" + std::string(Trim(original)) + "': castling is not legal here"};
    }

    // The promotion piece, at the end: "=Q", "(Q)", or a bare letter after a rank digit.
    Piece promotion = kNone;
    if (text.size() >= 4 && text.back() == ')' && text[text.size() - 3] == '(')
    {
        promotion = PieceFromLetter(text[text.size() - 2]);
        text.remove_suffix(3);
    }
    else if (text.size() >= 4 && text[text.size() - 2] == '=')
    {
        promotion = PieceFromLetter(text.back());
        text.remove_suffix(2);
    }
    else if (text.size() >= 3 && std::isalpha(static_cast<unsigned char>(text.back())) &&
             std::isdigit(static_cast<unsigned char>(text[text.size() - 2])))
    {
        promotion = PieceFromLetter(text.back());
        text.remove_suffix(1);
    }
    if (text.size() < 2)
    {
        return illegal();
    }
    const auto to = Square::Parse(text.substr(text.size() - 2));
    if (!to)
    {
        return as_uci();
    }
    text.remove_suffix(2);

    // The piece letter, upper case (a lower-case one only where it cannot be a file: not 'b').
    Piece piece = kPawn;
    if (!text.empty())
    {
        const char first = text.front();
        if (first == 'N' || first == 'B' || first == 'R' || first == 'Q' || first == 'K' || first == 'P' ||
            first == 'n' || first == 'r' || first == 'q' || first == 'k')
        {
            piece = PieceFromLetter(first);
            text.remove_prefix(1);
        }
    }
    if (!text.empty() && (text.back() == 'x' || text.back() == ':' || text.back() == '-'))
    {
        text.remove_suffix(1);
    }

    // What is left disambiguates: a file, a rank, or both.
    int from_file = -1;
    int from_rank = -1;
    for (const char character : text)
    {
        if (character >= 'a' && character <= 'h' && from_file < 0 && from_rank < 0)
        {
            from_file = character - 'a';
        }
        else if (character >= '1' && character <= '8' && from_rank < 0)
        {
            from_rank = character - '1';
        }
        else
        {
            return as_uci();
        }
    }

    Move found = Move::None();
    int  count = 0;
    for (const Move &move : moves)
    {
        if (move.MoveType() == Move::kCastling || move.To() != *to || move.MovingPiece() != piece ||
            (from_file >= 0 && move.From().File() != from_file) ||
            (from_rank >= 0 && move.From().Rank() != from_rank) || move.PromotedPiece() != promotion)
        {
            continue;
        }
        found = move;
        ++count;
    }
    if (count == 1)
    {
        return found;
    }
    if (count > 1)
    {
        return Error{"'" + std::string(Trim(original)) + "' is ambiguous here"};
    }
    // A king's two-square step written in long algebraic notation, or a move only UCI notation describes.
    const auto uci = ParseUciMove(position, original);
    if (uci.HasValue())
    {
        return uci;
    }
    const int last_rank = position.SideToMove() == kWhite ? 7 : 0;
    if (piece == kPawn && promotion == kNone && to->Rank() == last_rank)
    {
        return Error{"'" + std::string(Trim(original)) + "' reaches the last rank without naming a promotion piece"};
    }
    return illegal();
}

} // namespace amphitheater::chess
