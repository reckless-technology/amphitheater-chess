#include "PieceSet.h"

#include "Embedded.h"

#include <string_view>

namespace amphitheater::app
{

PieceSet PieceSet::FromEmbedded(std::string *error)
{
    PieceSet set;
    for (const chess::Color color : {chess::kWhite, chess::kBlack})
    {
        for (const chess::Piece piece :
             {chess::kPawn, chess::kKnight, chess::kBishop, chess::kRook, chess::kQueen, chess::kKing})
        {
            const std::string name  = FileName(color, piece);
            const auto        bytes = reckless::embedded::Find(name);
            std::string       why;
            if (bytes.empty())
            {
                why = "not carried";
            }
            else
            {
                const std::string_view text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
                set.drawings_[static_cast<std::size_t>(IndexOf(color, piece))] =
                    reckless::gfx::SvgImage::Parse(text, &why);
            }
            if (!set.drawings_[static_cast<std::size_t>(IndexOf(color, piece))] && error != nullptr && error->empty())
            {
                *error = name + ": " + why;
            }
        }
    }
    return set;
}

bool PieceSet::IsComplete() const
{
    for (const auto &drawing : drawings_)
    {
        if (!drawing)
        {
            return false;
        }
    }
    return true;
}

void PieceSet::Draw(reckless::gfx::Canvas &canvas, chess::Color color, chess::Piece piece,
                    reckless::gfx::RectF area) const
{
    if (piece == chess::kNone)
    {
        return;
    }
    if (const auto &drawing = drawings_[static_cast<std::size_t>(IndexOf(color, piece))])
    {
        drawing->DrawIn(canvas, area);
    }
}

std::string PieceSet::FileName(chess::Color color, chess::Piece piece)
{
    return std::string("pieces/") + (color == chess::kWhite ? 'w' : 'b') + chess::PieceLetter(piece) + ".svg";
}

int PieceSet::IndexOf(chess::Color color, chess::Piece piece)
{
    return (color == chess::kWhite ? 0 : 6) + static_cast<int>(piece) - 1;
}

} // namespace amphitheater::app
