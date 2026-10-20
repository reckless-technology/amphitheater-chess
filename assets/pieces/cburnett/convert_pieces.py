#!/usr/bin/env python3
"""Colin M.L. Burnett's chess pieces (source/, from Wikimedia Commons, BSD licence: source/LICENSE.txt) written in the
SVG the window's reader draws: a 100 x 100 viewBox, and nothing but <path> elements of absolute M, L, Q, C and Z
commands with the presentation attributes fill, stroke, stroke-width, stroke-linejoin and stroke-linecap.

    python assets/pieces/cburnett/convert_pieces.py [--check]

The originals are 45 x 45, styled by CSS on their groups, transformed, and drawn with relative commands, arcs and an
even-odd fill. Each element is resolved by svgelements - its style and its group's, its transform - scaled by 100/45,
its arcs written as cubic curves and every command made absolute. A filled path of more than one contour is filled by
its even-odd rule in the originals and by the non-zero rule in the reader, so its fill is written as the outline
skia-pathops gives the same area under the non-zero rule, and its stroke, unchanged, as a path of its own after it.
The drawings are otherwise the originals': nothing is redrawn.

--check writes nothing and exits 1 when a committed file differs from what the script writes, 77 where this Python
lacks svgelements or skia-pathops (tools/requirements.txt): CTest's pieces_cburnett test.
"""

import argparse
import sys
from pathlib import Path

try:
    import pathops
    from svgelements import SVG, Arc, Close, Color, CubicBezier, Line, Matrix, Move, Path as SvgPath, \
        QuadraticBezier, Shape
except ImportError:
    print("svgelements or skia-pathops is not installed in this Python: pip install -r tools/requirements.txt",
          file=sys.stderr)
    sys.exit(77)

HERE = Path(__file__).resolve().parent
SOURCE = HERE / "source"
SCALE = 100.0 / 45.0
PIECES = [colour + piece for colour in "wb" for piece in "KQRBNP"]


def number(value):
    """A coordinate to three places, with no trailing zeros and no negative zero."""
    text = f"{value:.3f}".rstrip("0").rstrip(".")
    return "0" if text in ("-0", "") else text


def point(p):
    """A point as the path data writes it."""
    return f"{number(p.x)},{number(p.y)}"


def colour_of(value):
    """A paint as the reader takes it: #rrggbb, or none."""
    if value is None:
        return "none"
    colour = Color(value)
    if colour.value is None or colour.alpha == 0:
        return "none"
    return colour.hex[:7].lower()


def path_data(path):
    """A resolved svgelements path as absolute M, L, Q, C and Z commands, arcs as cubic curves."""
    parts = []
    for segment in path:
        if isinstance(segment, Move):
            parts.append("M" + point(segment.end))
        elif isinstance(segment, Close):
            parts.append("Z")
        elif isinstance(segment, Line):
            parts.append("L" + point(segment.end))
        elif isinstance(segment, QuadraticBezier):
            parts.append("Q" + point(segment.control) + " " + point(segment.end))
        elif isinstance(segment, CubicBezier):
            parts.append("C" + point(segment.control1) + " " + point(segment.control2) + " " + point(segment.end))
        elif isinstance(segment, Arc):
            for curve in segment.as_cubic_curves():
                parts.append("C" + point(curve.control1) + " " + point(curve.control2) + " " + point(curve.end))
        else:
            raise ValueError(f"a segment the converter does not know: {type(segment).__name__}")
    return " ".join(parts)


def contours(path):
    """How many contours a path has: its moves."""
    return sum(1 for segment in path if isinstance(segment, Move))


def nonzero_fill(path):
    """The outline that fills, under the non-zero rule, what a path fills under the even-odd rule."""
    pen = pathops.Path(fillType=pathops.FillType.EVEN_ODD)
    is_open = False
    for segment in path:
        if isinstance(segment, Move):
            if is_open:
                pen.close()
            pen.moveTo(segment.end.x, segment.end.y)
            is_open = True
        elif isinstance(segment, Close):
            pen.close()
            is_open = False
        elif isinstance(segment, Line):
            pen.lineTo(segment.end.x, segment.end.y)
        elif isinstance(segment, QuadraticBezier):
            pen.qCurveTo((segment.control.x, segment.control.y), (segment.end.x, segment.end.y))
        elif isinstance(segment, CubicBezier):
            pen.curveTo((segment.control1.x, segment.control1.y), (segment.control2.x, segment.control2.y),
                        (segment.end.x, segment.end.y))
        elif isinstance(segment, Arc):
            for curve in segment.as_cubic_curves():
                pen.curveTo((curve.control1.x, curve.control1.y), (curve.control2.x, curve.control2.y),
                            (curve.end.x, curve.end.y))
    if is_open:
        pen.close()
    simplified = pathops.simplify(pen, fix_winding=True)
    parts = []
    for verb, points in simplified:
        if verb == pathops.PathVerb.MOVE:
            parts.append("M" + ",".join(number(v) for v in points[0]))
        elif verb == pathops.PathVerb.LINE:
            parts.append("L" + ",".join(number(v) for v in points[0]))
        elif verb == pathops.PathVerb.QUAD:
            parts.append("Q" + " ".join(",".join(number(v) for v in p) for p in points))
        elif verb == pathops.PathVerb.CUBIC:
            parts.append("C" + " ".join(",".join(number(v) for v in p) for p in points))
        elif verb == pathops.PathVerb.CLOSE:
            parts.append("Z")
        else:
            raise ValueError(f"a pathops verb the converter does not know: {verb}")
    return " ".join(parts)


def element_attributes(shape, fill, stroke):
    """The presentation attributes the reader takes, for a fill and a stroke."""
    attributes = [f'fill="{fill}"']
    if stroke != "none":
        width = shape.stroke_width if shape.stroke_width is not None else 1.0
        attributes.append(f'stroke="{stroke}"')
        attributes.append(f'stroke-width="{number(width * SCALE)}"')
        for name in ("stroke-linejoin", "stroke-linecap"):
            value = shape.values.get(name)
            if value and value != "miter" and value != "butt":
                attributes.append(f'{name}="{value}"')
    return " ".join(attributes)


def convert(source):
    """One piece, in the reader's SVG.

    source: the original's file."""
    svg = SVG.parse(str(source), reify=True)
    lines = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100">']
    for element in svg.elements():
        if not isinstance(element, Shape):
            continue
        path = SvgPath(element)
        path *= Matrix.scale(SCALE)
        path.reify()
        fill = colour_of(element.fill)
        stroke = colour_of(element.stroke)
        if fill == "none" and stroke == "none":
            continue
        is_even_odd = element.values.get("fill-rule", "nonzero") == "evenodd"
        if fill != "none" and is_even_odd and contours(path) > 1:
            lines.append(f'  <path d="{nonzero_fill(path)}" {element_attributes(element, fill, "none")}/>')
            if stroke != "none":
                lines.append(f'  <path d="{path_data(path)}" {element_attributes(element, "none", stroke)}/>')
            continue
        lines.append(f'  <path d="{path_data(path)}" {element_attributes(element, fill, stroke)}/>')
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="write nothing; exit 1 when a committed file differs")
    arguments = parser.parse_args()
    differing = []
    for name in PIECES:
        written = convert(SOURCE / f"{name}.svg").encode("utf-8")
        target = HERE / f"{name}.svg"
        if arguments.check:
            if not target.exists() or target.read_bytes() != written:
                differing.append(name)
        else:
            target.write_bytes(written)
    if differing:
        print("convert_pieces.py: these differ from what the script writes: " + ", ".join(differing), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
