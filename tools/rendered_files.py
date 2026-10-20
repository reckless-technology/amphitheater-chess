"""How tools/make_icon.py writes the files it renders, or with --check holds the committed ones to them.

A committed file passes the check when its bytes are the rendered file's, or - for a PNG, an .ico or an .icns - when
every picture in it has the rendered picture's size and mode and its pixels agree: no channel value more than
PIXEL_LIMIT apart, and at most PIXEL_SHARE of them more than PIXEL_TOLERANCE apart. A compressor or a rasteriser
rounding differently on another platform moves a few values by a step; a change of composition moves many by more.
"""

import io
import sys

PIXEL_TOLERANCE = 2
PIXEL_SHARE = 0.005
PIXEL_LIMIT = 48


def require_imaging():
    """Pillow imported, or the script exits 77 - CTest's skip - where the Python running it has none."""
    try:
        import PIL  # noqa: F401
    except ImportError:
        print("Pillow is not installed in this Python: pip install -r tools/requirements.txt", file=sys.stderr)
        sys.exit(77)


def _pictures(data):
    """The pictures in a PNG, an .ico or an .icns, each decoded to RGBA; None for any other file."""
    from PIL import Image

    if data[:8] == b"\x89PNG\r\n\x1a\n":
        image = Image.open(io.BytesIO(data))
        image.load()
        return [(image.mode, image.convert("RGBA"))]
    if data[:4] == b"\x00\x00\x01\x00":
        icon = Image.open(io.BytesIO(data))
        pictures = []
        for size in sorted(icon.info.get("sizes", [])):
            icon.size = size
            icon.load()
            pictures.append(("ico", icon.convert("RGBA")))
        return pictures
    if data[:4] == b"icns":
        pictures, offset = [], 8
        while offset < len(data):
            kind, length = data[offset:offset + 4], int.from_bytes(data[offset + 4:offset + 8], "big")
            for mode, image in _pictures(data[offset + 8:offset + length]) or []:
                pictures.append((kind.decode("ascii") + mode, image))
            offset += length
        return pictures
    return None


def _agree(a, b):
    """Whether two RGBA pictures of one size agree within the tolerances."""
    from PIL import ImageChops

    if a.size != b.size:
        return False
    histogram = ImageChops.difference(a, b).histogram()  # 256 counts a channel, by the size of the difference
    counts = [sum(histogram[band * 256 + value] for band in range(4)) for value in range(256)]
    worst = max(value for value in range(256) if counts[value])
    apart = sum(counts[PIXEL_TOLERANCE + 1:])
    return worst <= PIXEL_LIMIT and apart <= PIXEL_SHARE * sum(counts)


def same(committed, rendered):
    """Whether a committed file is the rendered one: the same bytes, or the same pictures within the tolerances."""
    if committed == rendered:
        return True
    old, new = _pictures(committed), _pictures(rendered)
    if old is None or new is None or len(old) != len(new):
        return False
    return all(mode_a == mode_b and _agree(a, b) for (mode_a, a), (mode_b, b) in zip(old, new))


def write_or_check(script, out, files, check, stale=()):
    """Write each file under out, or with check compare each with the committed one; return the exit code.

    script: the script's name, for its messages. files: the rendered files by their paths under out, as bytes.
    stale: paths under out the script no longer writes, removed when writing and refused when checking."""
    differing = []
    for name, data in files.items():
        path = out / name
        if check:
            if not path.exists() or not same(path.read_bytes(), data):
                differing.append(name)
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    for name in stale:
        path = out / name
        if path.exists():
            if check:
                differing.append(name + " (no longer written)")
            else:
                path.unlink()
    if differing:
        print(f"{script}: these differ from what the script renders: " + ", ".join(differing), file=sys.stderr)
        return 1
    return 0
