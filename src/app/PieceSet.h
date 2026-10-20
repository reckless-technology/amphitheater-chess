#pragma once
/// @file PieceSet.h The twelve pieces as drawings, in one of the sets the binary carries - Colin M.L. Burnett's
/// (assets/pieces/cburnett, the default) or Jonny's Staunton set (assets/pieces/staunton) - each piece the embedded
/// file pieces/<set>/<colour><piece>.svg, read once by the toolkit's SVG reader and drawn at any size.

#include "chess/ChessTypes.h"

#include "Canvas.h"
#include "SvgImage.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace amphitheater::app
{

/** A set of pieces the binary carries. */
struct PieceSetName
{
    const char *id;    ///< Its folder among the embedded files, and its name in the layout file: "cburnett".
    const char *label; ///< Its name in View > Pieces.
};

/** The sets the binary carries, in View > Pieces' order. */
inline constexpr std::array<PieceSetName, 2> kPieceSets{{{"cburnett", "Cburnett"}, {"staunton", "Staunton"}}};

/** The set the board is drawn with until a person chooses another. */
inline constexpr std::string_view kDefaultPieceSet = "cburnett";

/**
 * @param id A set's id.
 * @return Whether the binary carries it.
 */
bool IsPieceSet(std::string_view id);

/** The pieces, parsed once and drawn into any square. */
class PieceSet
{
  public:
    /**
     * A set the binary carries (the embedded files pieces/<set>/wK.svg and the rest).
     * @param set The set's id: one of kPieceSets.
     * @param error Why a piece could not be read, when one could not (may be null).
     * @return The set; a piece that could not be read draws nothing.
     */
    static PieceSet FromEmbedded(std::string_view set = kDefaultPieceSet, std::string *error = nullptr);

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
     * @param set The set's id.
     * @param color The colour.
     * @param piece The piece, not kNone.
     * @return "pieces/cburnett/wK.svg" and the like.
     */
    static std::string FileName(std::string_view set, chess::Color color, chess::Piece piece);

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

} // namespace amphitheater::app
