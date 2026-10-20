#!/usr/bin/env python3
"""The app's icon: Jonny's sketch of the Colosseum (assets/art/amphitheater-colosseum.jpg, drawn unaltered - the
drawing and its red Rt roundel exactly as the artwork has them) on a rounded square, rendered into assets/icon/ in every
form the platforms carry it. The results are committed, so a build needs no Python.

    python tools/make_icon.py [--check]

Two compositions on a 1024-unit canvas:

    Windows and Linux: the artwork filling a square with modestly rounded corners (9 % of the side) across the canvas.
    macOS: Apple's icon grid - an 824-unit rounded square centred in the canvas at the standard corner radius (185.4),
           under the subtle drop shadow Apple's template carries, the artwork filling it.

On both the square has a hairline light-grey edge (#D0D0D0, 1/128 of the canvas, and never under one device pixel) so
it keeps its shape on a white background. Every size, 16 to 1024, shows the whole artwork: each is resampled straight
from the 2048-pixel source to its own pixels by Pillow's Lanczos filter - never one bitmap stepped down through the
sizes, and never sharpened or cropped. The square's shape, its edge and the shadow are drawn as SVG by resvg (the
resvg-py wheel, pinned with Pillow in tools/requirements.txt, which needs nothing of the system on any platform), and
Pillow composes and packs the renders:

    png/icon-<N>.png        N = 16 20 24 32 40 48 64 128 256 512 1024 (Windows and Linux; icon-1024.png the master)
    png/icon-macos-<N>.png  N = 16 32 64 128 256 512 1024
    Icon.ico                16 to 256, a PNG each: the executable, the installer and its uninstaller
    Icon.icns               16 to 1024: the app bundle and the macOS uninstaller

The Linux package takes the hicolor sizes from here (packaging/linux/make-deb.sh), and the window's _NET_WM_ICON the
256 (src/app/CMakeLists.txt). --check renders into memory and exits 1 when a committed file differs from the render
(tools/rendered_files.py says how closely), 77 where this Python lacks resvg-py or Pillow: CTest's icon test.
"""

import argparse
import io
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # rendered_files, beside this script, under python -I too
import rendered_files  # noqa: E402

rendered_files.require_imaging()
try:
    import resvg_py
except ImportError:
    print("resvg-py is not installed in this Python: pip install -r tools/requirements.txt", file=sys.stderr)
    sys.exit(77)
from PIL import Image  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
ART = ROOT / "assets" / "art" / "amphitheater-colosseum.jpg"
OUT = ROOT / "assets" / "icon"

CANVAS = 1024.0
EDGE = "#D0D0D0"
EDGE_FRACTION = 1.0 / 128.0

# Windows and Linux: the whole canvas, corners rounded at 9 % of the side.
SQUARE_FLAT = dict(origin=0.0, side=1024.0, radius=92.0, shadow=False)
# macOS: Apple's grid, the 824-unit body at the standard radius, with the template's drop shadow (black at 30 %,
# 10 units down, blurred by a deviation of 10).
SQUARE_MACOS = dict(origin=100.0, side=824.0, radius=185.4, shadow=True)

ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
FLAT_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256, 512, 1024]
MACOS_SIZES = [16, 32, 64, 128, 256, 512, 1024]

# The source's own edge pixels repeated this far round it, so the Lanczos filter's reach past a square whose edge falls
# between two pixels reads the drawing's paper rather than nothing.
PADDING = 512


def number(value):
    """A length in the SVG, to four places with no trailing zeros."""
    return f"{value:.4f}".rstrip("0").rstrip(".")


def frame_svg(square, pixels, part):
    """One layer of the square as SVG on the 1024-unit canvas: 'body' (the white square, with the macOS shadow), 'mask'
    (the square alone, which clips the artwork) or 'edge' (the hairline, held to at least one device pixel)."""
    origin, side, radius = square["origin"], square["side"], square["radius"]
    lines = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="1024" height="1024">']
    body = (f'x="{number(origin)}" y="{number(origin)}" width="{number(side)}" height="{number(side)}" '
            f'rx="{number(radius)}"')
    if part == "body" and square["shadow"]:
        lines.append('  <defs><filter id="shadow" x="-10%" y="-10%" width="120%" height="125%">'
                     '<feDropShadow dx="0" dy="10" stdDeviation="10" flood-color="#000000" flood-opacity="0.3"/>'
                     '</filter></defs>')
        lines.append(f'  <rect {body} fill="#FFFFFF" filter="url(#shadow)"/>')
    elif part in ("body", "mask"):
        lines.append(f'  <rect {body} fill="#FFFFFF"/>')
    else:
        edge = max(CANVAS * EDGE_FRACTION, CANVAS / pixels)
        inset = edge / 2.0
        lines.append(f'  <rect x="{number(origin + inset)}" y="{number(origin + inset)}" '
                     f'width="{number(side - edge)}" height="{number(side - edge)}" rx="{number(radius - inset)}" '
                     f'fill="none" stroke="{EDGE}" stroke-width="{number(edge)}"/>')
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def render_svg(svg, pixels):
    """An SVG rendered by resvg at pixels x pixels, as an RGBA image."""
    png = resvg_py.svg_to_bytes(svg_string=svg, width=pixels, height=pixels)
    image = Image.open(io.BytesIO(bytes(png)))
    image.load()
    return image.convert("RGBA")


def padded_art():
    """The artwork with its edge pixels repeated PADDING pixels out on every side."""
    art = Image.open(ART).convert("RGB")
    width, height = art.size
    padded = Image.new("RGB", (width + 2 * PADDING, height + 2 * PADDING))
    padded.paste(art, (PADDING, PADDING))
    padded.paste(art.crop((0, 0, width, 1)).resize((width, PADDING)), (PADDING, 0))
    padded.paste(art.crop((0, height - 1, width, height)).resize((width, PADDING)), (PADDING, height + PADDING))
    padded.paste(padded.crop((PADDING, 0, PADDING + 1, height + 2 * PADDING)).resize((PADDING, height + 2 * PADDING)),
                 (0, 0))
    padded.paste(padded.crop((PADDING + width - 1, 0, PADDING + width, height + 2 * PADDING)).resize(
        (PADDING, height + 2 * PADDING)), (PADDING + width, 0))
    return padded, width


def art_layer(art, art_side, square, pixels):
    """The artwork resampled once, by Lanczos, from the source to fill the square's body at pixels x pixels: every
    destination pixel reads the source through the one mapping of the body's square onto the drawing's."""
    scale = pixels / CANVAS
    origin, side = square["origin"] * scale, square["side"] * scale
    per_pixel = art_side / side  # source pixels per destination pixel
    box = (PADDING - origin * per_pixel, PADDING - origin * per_pixel, PADDING + (pixels - origin) * per_pixel,
           PADDING + (pixels - origin) * per_pixel)
    return art.resize((pixels, pixels), Image.LANCZOS, box=box).convert("RGBA")


def render(art, art_side, square, pixels):
    """The icon at pixels x pixels: the square (and its shadow), the artwork clipped to it, the hairline over both."""
    image = render_svg(frame_svg(square, pixels, "body"), pixels)
    picture = art_layer(art, art_side, square, pixels)
    picture.putalpha(render_svg(frame_svg(square, pixels, "mask"), pixels).getchannel("A"))
    image.alpha_composite(picture)
    image.alpha_composite(render_svg(frame_svg(square, pixels, "edge"), pixels))
    return image


def png_bytes(image):
    """An image as a PNG's bytes."""
    buffer = io.BytesIO()
    image.save(buffer, format="PNG", optimize=False)
    return buffer.getvalue()


def outputs():
    """Every file this script writes, by its path under assets/icon/, as bytes."""
    art, art_side = padded_art()
    flat = {size: render(art, art_side, SQUARE_FLAT, size) for size in FLAT_SIZES}
    macos = {size: render(art, art_side, SQUARE_MACOS, size) for size in MACOS_SIZES}
    files = {}
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
    # The vector compositions the queen's icon committed; the artwork's icon has none, being a drawing on paper.
    return rendered_files.write_or_check("make_icon.py", OUT, outputs(), arguments.check,
                                         stale=["icon.svg", "icon-macos.svg"])


if __name__ == "__main__":
    sys.exit(main())
