#!/usr/bin/env python3
"""The app's icon: Jonny's black queen (assets/pieces/staunton/bQ.svg, drawn unchanged) on a white square, rendered
into assets/icon/ in every form the platforms carry it. The results are committed, so a build needs no Python.

    python tools/make_icon.py [--check]

Two compositions, each a 1024-unit SVG written beside its renders:

    icon.svg        Windows and Linux: a white square with modestly rounded corners filling the canvas.
    icon-macos.svg  macOS: Apple's icon grid - an 824-unit rounded square centred in the 1024 canvas at the standard
                    corner radius (185.4), under the subtle drop shadow Apple's template carries.

On both the square has a hairline light-grey edge (#D0D0D0, 1/128 of the canvas, and never under one device pixel) so
it keeps its shape on a white background, and the queen fills 80 % of the square's height, centred across and lifted a
little above the square's centre: her plinth is wide and heavy and her crown open, so centred by her bounding box she
reads low. Under 32 pixels she fills 87.5 %, so her crown's points keep pixels of their own at 16, and up to 64 pixels
her foot lands on a whole pixel; the piece itself is never altered, only placed.

Every size is rendered from the SVG at that size - never one bitmap scaled down - by resvg (the resvg-py wheel, pinned
with Pillow in tools/requirements.txt, which needs nothing of the system on any platform), and Pillow packs the renders:

    png/icon-<N>.png        N = 16 20 24 32 40 48 64 128 256 512 1024 (Windows and Linux; icon-1024.png the master)
    png/icon-macos-<N>.png  N = 16 32 64 128 256 512 1024
    Icon.ico                16 to 256, a PNG each: the executable, the installer and its uninstaller
    Icon.icns               16 to 1024: the app bundle and the macOS uninstaller

The Linux package takes the hicolor sizes and icon.svg from here (packaging/linux/make-deb.sh), and the window's
_NET_WM_ICON the 256 (src/app/CMakeLists.txt). --check renders into memory and exits 1 when a committed file differs.
"""

import argparse
import io
import re
import sys
from pathlib import Path

import resvg_py
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
QUEEN = ROOT / "assets" / "pieces" / "staunton" / "bQ.svg"
OUT = ROOT / "assets" / "icon"

CANVAS = 1024.0
WHITE = "#FFFFFF"
EDGE = "#D0D0D0"
EDGE_FRACTION = 1.0 / 128.0
QUEEN_HEIGHT_FRACTION = 0.80
# Under 32 pixels she fills 87.5 % (14 of 16 pixels), which gives the crown's points pixels of their own.
SMALL_PIXELS = 32
SMALL_QUEEN_HEIGHT_FRACTION = 0.875
# Up to 64 pixels her foot is moved to the nearest whole pixel, so the plinth's lower edge is crisp.
SNAPPED_PIXELS = 64
# How far above the square's centre the queen's bounding box sits, as a fraction of the square's side.
OPTICAL_LIFT_FRACTION = 0.012

# The queen's extent in her own 100-unit viewBox, strokes included: the crown's outer balls (cy 13, r 3.4) and the
# plinth's foot (y 92), each with half of the 2.4 stroke; across, the plinth's ends at 15 and 85.
QUEEN_TOP = 13.0 - 3.4 - 1.2
QUEEN_BOTTOM = 92.0 + 1.2
QUEEN_CENTRE_X = 50.0

# Windows and Linux: the whole canvas, corners rounded at 9 % of the side.
SQUARE_FLAT = dict(origin=0.0, side=1024.0, radius=92.0, shadow=False)
# macOS: Apple's grid, the 824-unit body at the standard radius, with the template's drop shadow (black at 30 %,
# 10 units down, blurred by a deviation of 10).
SQUARE_MACOS = dict(origin=100.0, side=824.0, radius=185.4, shadow=True)

ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
FLAT_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256, 512, 1024]
MACOS_SIZES = [16, 32, 64, 128, 256, 512, 1024]


def queen_body():
    """The queen's SVG as it stands, less its outer <svg> element: her <defs> and her shapes, unchanged."""
    text = QUEEN.read_text(encoding="utf-8")
    match = re.search(r"<svg[^>]*>(.*)</svg>", text, re.DOTALL)
    if not match:
        raise SystemExit(f"make_icon.py: {QUEEN} holds no <svg> element")
    return match.group(1).strip("\n")


def composition(square, pixels=None):
    """The icon's SVG on a 1024-unit canvas. With pixels, the edge is held to at least one device pixel at that size."""
    origin, side, radius = square["origin"], square["side"], square["radius"]
    edge = CANVAS * EDGE_FRACTION
    if pixels:
        edge = max(edge, CANVAS / pixels)
    inset = edge / 2.0
    fraction = SMALL_QUEEN_HEIGHT_FRACTION if pixels and pixels < SMALL_PIXELS else QUEEN_HEIGHT_FRACTION
    scale = side * fraction / (QUEEN_BOTTOM - QUEEN_TOP)
    centre_x = origin + side / 2.0
    centre_y = origin + side / 2.0 - side * OPTICAL_LIFT_FRACTION
    translate_x = centre_x - QUEEN_CENTRE_X * scale
    translate_y = centre_y - (QUEEN_TOP + QUEEN_BOTTOM) / 2.0 * scale
    if pixels and pixels <= SNAPPED_PIXELS:
        units_per_pixel = CANVAS / pixels
        foot = (translate_y + QUEEN_BOTTOM * scale) / units_per_pixel
        translate_y += (round(foot) - foot) * units_per_pixel

    def number(value):
        return f"{value:.4f}".rstrip("0").rstrip(".")

    lines = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="1024" height="1024">']
    body = (f'x="{number(origin)}" y="{number(origin)}" width="{number(side)}" height="{number(side)}" '
            f'rx="{number(radius)}"')
    if square["shadow"]:
        lines.append('  <defs><filter id="shadow" x="-10%" y="-10%" width="120%" height="125%">'
                     '<feDropShadow dx="0" dy="10" stdDeviation="10" flood-color="#000000" flood-opacity="0.3"/>'
                     '</filter></defs>')
        lines.append(f'  <rect {body} fill="{WHITE}" filter="url(#shadow)"/>')
    else:
        lines.append(f'  <rect {body} fill="{WHITE}"/>')
    lines.append(f'  <rect x="{number(origin + inset)}" y="{number(origin + inset)}" '
                 f'width="{number(side - edge)}" height="{number(side - edge)}" rx="{number(radius - inset)}" '
                 f'fill="none" stroke="{EDGE}" stroke-width="{number(edge)}"/>')
    lines.append(f'  <g transform="translate({number(translate_x)} {number(translate_y)}) scale({number(scale)})">')
    lines.append(queen_body())
    lines.append("  </g>")
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def render(square, pixels):
    """The composition rendered by resvg at pixels x pixels, as an RGBA image."""
    png = resvg_py.svg_to_bytes(svg_string=composition(square, pixels), width=pixels, height=pixels)
    image = Image.open(io.BytesIO(bytes(png)))
    image.load()
    return image.convert("RGBA")


def png_bytes(image):
    buffer = io.BytesIO()
    image.save(buffer, format="PNG", optimize=False)
    return buffer.getvalue()


def outputs():
    """Every file this script writes, by its path under assets/icon/, as bytes."""
    files = {
        "icon.svg": composition(SQUARE_FLAT).encode("utf-8"),
        "icon-macos.svg": composition(SQUARE_MACOS).encode("utf-8"),
    }
    flat = {size: render(SQUARE_FLAT, size) for size in FLAT_SIZES}
    macos = {size: render(SQUARE_MACOS, size) for size in MACOS_SIZES}
    for size, image in flat.items():
        files[f"png/icon-{size}.png"] = png_bytes(image)
    for size, image in macos.items():
        files[f"png/icon-macos-{size}.png"] = png_bytes(image)

    # Pillow takes each size from the frames it is handed, scaling none.
    ico = io.BytesIO()
    flat[256].save(ico, format="ICO", sizes=[(size, size) for size in ICO_SIZES],
                   append_images=[flat[size] for size in ICO_SIZES if size != 256])
    files["Icon.ico"] = ico.getvalue()
    files["Icon.icns"] = icns({size: files[f"png/icon-macos-{size}.png"] for size in MACOS_SIZES})
    return files


# The .icns entries, each a PNG: the type and the pixels it holds. The 1x and 2x types of one size hold the same
# picture (16 at 2x is the 32-pixel render), so the system draws every point size from a render of its own pixels.
ICNS_TYPES = [(b"icp4", 16), (b"ic11", 32), (b"icp5", 32), (b"ic12", 64), (b"icp6", 64), (b"ic07", 128),
              (b"ic13", 256), (b"ic08", 256), (b"ic14", 512), (b"ic09", 512), (b"ic10", 1024)]


def icns(pngs):
    """An .icns of every type in ICNS_TYPES from the PNGs by size: the 'icns' header, then each entry's type, its length
    with its 8-byte header and its PNG, big-endian, as the format has it. Pillow's writer leaves out the 16 and 32 1x
    types."""
    entries = b"".join(kind + (8 + len(pngs[size])).to_bytes(4, "big") + pngs[size] for kind, size in ICNS_TYPES)
    return b"icns" + (8 + len(entries)).to_bytes(4, "big") + entries


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="write nothing; exit 1 when a committed file differs")
    arguments = parser.parse_args()
    differing = []
    for name, data in outputs().items():
        path = OUT / name
        if arguments.check:
            if not path.exists() or path.read_bytes() != data:
                differing.append(name)
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    if differing:
        print("make_icon.py: these differ from what the script renders: " + ", ".join(differing), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
