#pragma once
/// @file PieceSet.h The twelve pieces as drawings: Jonny's Staunton set (assets/pieces/staunton), carried inside the
/// binary as pieces/<colour><piece>.svg and read once by the toolkit's SVG reader, drawn at any size.

#include "chess/ChessTypes.h"

#include "Canvas.h"
#include "SvgImage.h"

#include <array>
#include <optional>
#include <string>

namespace uci_host::app
{

/** The pieces, parsed once and drawn into any square. */
class PieceSet
{
  public:
    /**
     * The set the binary carries (the embedded files pieces/wK.svg and the rest).
     * @param error Why a piece could not be read, when one could not (may be null).
     * @return The set; a piece that could not be read draws nothing.
     */
    static PieceSet FromEmbedded(std::string *error = nullptr);

    /** @return Whether all twelve pieces were read. */
    bool IsComplete() const;

    /**
     * A piece drawn into a square, its drawing fitted and centred.
     * @param canvas The canvas.
     * @param color The piece's colour.
     * @param piece The piece; kNone draws nothing.
     * @param area The square, or any box.
     */
    void Draw(reckless::gfx::Canvas &canvas, chess::Color color, chess::Piece piece, reckless::gfx::RectF area) const;

    /**
     * The embedded file's name for a piece.
     * @param color The colour.
     * @param piece The piece, not kNone.
     * @return "pieces/wK.svg" and the like.
     */
    static std::string FileName(chess::Color color, chess::Piece piece);

  private:
    /**
     * A piece's index in the table.
     * @param color The colour.
     * @param piece The piece, not kNone.
     * @return 0 to 11.
     */
    static int IndexOf(chess::Color color, chess::Piece piece);

    std::array<std::optional<reckless::gfx::SvgImage>, 12> drawings_; ///< By IndexOf; empty where unread.
};

} // namespace uci_host::app
