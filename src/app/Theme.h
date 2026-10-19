#pragma once
/// @file Theme.h The window's colours and fonts: one dark look, the approved mockup's tokens
/// (docs/design/main-window-mockup.html), Noto Serif for the interface and Monaspace Xenon for engine output and FEN.

#include "AppTheme.h"
#include "Button.h"
#include "Color.h"
#include "LookDrawing.h"
#include "LookPalette.h"
#include "Text.h"

#include <string_view>

namespace uci_host::app::theme
{

using reckless::gfx::Color;

inline constexpr Color kWindow{0xff29282cu};   ///< The window's colour, behind the columns.
inline constexpr Color kPanel{0xff1b1a1du};    ///< A card, a list, a strip: one step darker.
inline constexpr Color kRow{0xff222125u};      ///< Every other row of a list.
inline constexpr Color kRule{0xff3b3936u};     ///< The rules and the frames round a card.
inline constexpr Color kInk{0xffe8dec8u};      ///< The text.
inline constexpr Color kMuted{0xff9c9483u};    ///< Secondary text: a caption, a move number, the coordinates.
inline constexpr Color kFaint{0xff5f5a52u};    ///< A hint, a disabled item.
inline constexpr Color kAmber{0xffe2a23eu};    ///< The accent: the running clock, the best line's arrow, a lit plate.
inline constexpr Color kAmberDim{0xff6e5428u}; ///< The accent at rest: a plate's edge.
inline constexpr Color kSelect{0xff5d4423u};   ///< The move shown, the best line, the menu under the pointer.
inline constexpr Color kPlus{0xff86c9a2u};     ///< A score for White.
inline constexpr Color kMinus{0xffe2705fu};    ///< A score for Black, an error.
inline constexpr Color kLightSquare{0xffecdab8u}; ///< The light squares.
inline constexpr Color kDarkSquare{0xff9d7553u};  ///< The dark squares.
inline constexpr Color kBoardEdge{0xff121113u};   ///< The board's frame, a stopped clock's face, the log's background.
inline constexpr Color kTitleBar{0xff19181bu};    ///< The status bar.
inline constexpr Color kPlate{0xff232125u};       ///< A button's plate.
inline constexpr Color kLogText{0xffc9c0abu};     ///< An engine's line in the log.
inline constexpr Color kWhiteSide{0xfff3ead6u};   ///< White's side dot.
inline constexpr Color kBlackSide{0xff141210u};   ///< Black's side dot.
inline constexpr Color kLastMoveLight{0xffe8c285u}; ///< The last move on a light square: the accent at 42 %.
inline constexpr Color kLastMoveDark{0xffcd9444u};  ///< On a dark square: at 70 %, where 42 % barely shows.

/** @return The window's colours as the toolkit's app shell and its widgets take them. */
inline reckless::app::Theme Shell()
{
    return {kWindow, kRule, kPanel, kRow, kInk, kMuted, kFaint, kAmber, kAmberDim, kPlus, kMinus};
}

/** @return The series' palette for the lists and text views: rows on the panel colour, in the ink, lit in amber. */
inline reckless::look::Palette Lists()
{
    return Shell().Palette();
}

/** @return The series' palette for the buttons: the plate a step lighter than a card, its edge the accent at rest. */
inline reckless::look::Palette Buttons()
{
    return {kWindow, kInk, kAmber, kPlate, kAmberDim};
}

} // namespace uci_host::app::theme

namespace uci_host::app::fonts
{

using reckless::gfx::Font;
using reckless::gfx::FontFamily;

/** @return The interface's text: a move, a value on the game card. */
inline Font Body()
{
    return Font(19.0f);
}

/** @return Smaller text: a player's details, the status bar, an engine's statistics. */
inline Font Small()
{
    return Font(16.0f);
}

/** @return A section's label, in capitals, bold and spaced. */
inline Font Label()
{
    return Font(15.5f).Bold().WithKerning(0.08f);
}

/** @return Engine output, the log and the FEN: the monospaced family. */
inline Font Mono()
{
    return Font(16.0f).WithFamily(FontFamily::kMonospace);
}

/** @return A clock's face: the monospaced family, large. */
inline Font Clock()
{
    return Font(27.0f).WithFamily(FontFamily::kMonospace);
}

/** @return A plate's caption: the body face, bold, a little smaller. */
inline Font Plate()
{
    return Font(16.5f).Bold();
}

} // namespace uci_host::app::fonts

namespace uci_host::app::theme
{

/**
 * The window's buttons: the series' button plate in the window's colours (look::DrawButtonPlate), with a toggle's lamp
 * at its left (look::DrawPlateLamp), and its caption in the interface's face at the size the mockup sets it.
 * @return The painter.
 */
inline reckless::view::Button::Painter PlatePainter()
{
    return [](reckless::gfx::Canvas &c, reckless::gfx::RectI bounds, std::string_view text,
              reckless::view::Button::Look look) {
        reckless::look::DrawButtonPlate(c, Buttons(), bounds, look.on, look.over, look.held);
        if (look.lamp)
        {
            reckless::look::DrawPlateLamp(c, bounds, look.on);
        }
        c.SetFont(fonts::Plate());
        c.SetColor(look.dimmed ? kInk.WithAlpha(0.4f) : kInk);
        const float left = look.lamp ? 18.0f : 4.0f;
        c.DrawText(text,
                   reckless::gfx::RectF{left, 0.0f, static_cast<float>(bounds.width()) - left - 4.0f,
                                        static_cast<float>(bounds.height())},
                   reckless::gfx::Justify::kCentred, true);
    };
}

} // namespace uci_host::app::theme
