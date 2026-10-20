#include "BoardSetup.h"

#include <algorithm>
#include <cctype>

namespace amphitheater::chess
{

BoardSetup BoardSetup::FromPosition(const Position &position)
{
    BoardSetup setup;
    for (int index = 0; index < 64; ++index)
    {
        const Square square{index};
        if (const Piece piece = position.PieceOn(square); piece != kNone)
        {
            setup.squares_[square] = {position.ColorOn(square), piece};
        }
    }
    setup.side_to_move_         = position.SideToMove();
    const CastlingRights rights = position.Castling();
    for (const Color color : {kWhite, kBlack})
    {
        for (const bool is_kingside : {true, false})
        {
            setup.castling_[color * 2 + (is_kingside ? 0 : 1)] = rights.May(color, is_kingside);
        }
    }
    setup.en_passant_     = position.EnPassantSquare();
    setup.halfmove_clock_ = position.HalfmoveClock();
    setup.move_number_    = position.FullmoveNumber();
    setup.Prune();
    return setup;
}

BoardSetup BoardSetup::Start()
{
    return FromPosition(Position::Start());
}

void BoardSetup::Put(Square square, SetupPiece piece)
{
    squares_[square] = piece.piece == kNone ? SetupPiece{} : piece;
    Prune();
}

void BoardSetup::Move(Square from, Square to)
{
    if (from == to)
    {
        return;
    }
    squares_[to]   = squares_[from];
    squares_[from] = {};
    Prune();
}

void BoardSetup::Clear()
{
    squares_.fill({});
    Prune();
}

void BoardSetup::SetSideToMove(Color side)
{
    side_to_move_ = side;
    Prune();
}

bool BoardSetup::CanCastle(Color color, bool is_kingside) const
{
    const int        back_rank = color == kWhite ? 0 : 7;
    const SetupPiece king      = squares_[Square{4, back_rank}];
    const SetupPiece rook      = squares_[Square{is_kingside ? 7 : 0, back_rank}];
    return king == SetupPiece{color, kKing} && rook == SetupPiece{color, kRook};
}

void BoardSetup::SetCastling(Color color, bool is_kingside, bool may)
{
    castling_[color * 2 + (is_kingside ? 0 : 1)] = may && CanCastle(color, is_kingside);
}

std::vector<Square> BoardSetup::EnPassantSquares() const
{
    // The side that has just moved made the double step: White's pawn on the fourth rank with the third and the second
    // empty behind it while Black is to move, Black's on the fifth with the sixth and the seventh empty while White is.
    const Color         mover  = EnemyOf(side_to_move_);
    const int           pawn   = mover == kWhite ? 3 : 4;
    const int           passed = mover == kWhite ? 2 : 5;
    const int           home   = mover == kWhite ? 1 : 6;
    std::vector<Square> squares;
    for (int file = 0; file < 8; ++file)
    {
        if (squares_[Square{file, pawn}] == SetupPiece{mover, kPawn} && squares_[Square{file, passed}].piece == kNone &&
            squares_[Square{file, home}].piece == kNone)
        {
            squares.push_back(Square{file, passed});
        }
    }
    return squares;
}

void BoardSetup::SetEnPassant(std::optional<Square> square)
{
    if (!square)
    {
        en_passant_.reset();
        return;
    }
    const std::vector<Square> allowed = EnPassantSquares();
    if (std::find(allowed.begin(), allowed.end(), *square) != allowed.end())
    {
        en_passant_ = square;
    }
}

void BoardSetup::SetHalfmoveClock(int count)
{
    halfmove_clock_ = std::max(0, count);
}

void BoardSetup::SetMoveNumber(int number)
{
    move_number_ = std::max(1, number);
}

std::string BoardSetup::ToFen() const
{
    std::string fen;
    for (int rank = 7; rank >= 0; --rank)
    {
        int empty = 0;
        for (int file = 0; file < 8; ++file)
        {
            const SetupPiece piece = squares_[Square{file, rank}];
            if (piece.piece == kNone)
            {
                ++empty;
                continue;
            }
            if (empty > 0)
            {
                fen += static_cast<char>('0' + empty);
                empty = 0;
            }
            const char letter = PieceLetter(piece.piece);
            fen += piece.color == kWhite ? letter : static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
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
    fen += side_to_move_ == kWhite ? " w " : " b ";
    std::string rights;
    for (int index = 0; index < 4; ++index)
    {
        if (castling_[index])
        {
            rights += "KQkq"[index];
        }
    }
    fen += rights.empty() ? "-" : rights;
    fen += ' ';
    fen += en_passant_ ? en_passant_->ToString() : "-";
    fen += ' ' + std::to_string(halfmove_clock_) + ' ' + std::to_string(move_number_);
    return fen;
}

Result<Position> BoardSetup::ToPosition() const
{
    return Position::FromFen(ToFen(), FenMode::kStrict);
}

void BoardSetup::Prune()
{
    for (const Color color : {kWhite, kBlack})
    {
        for (const bool is_kingside : {true, false})
        {
            if (!CanCastle(color, is_kingside))
            {
                castling_[color * 2 + (is_kingside ? 0 : 1)] = false;
            }
        }
    }
    if (en_passant_)
    {
        const std::vector<Square> allowed = EnPassantSquares();
        if (std::find(allowed.begin(), allowed.end(), *en_passant_) == allowed.end())
        {
            en_passant_.reset();
        }
    }
}

} // namespace amphitheater::chess
