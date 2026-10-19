"""The approved mockup of the main window, made viewable: main-window-mockup.html beside this script is a template
whose pieces and monospaced font are placeholders, and this fills them with the real assets - the twelve Staunton pieces
(assets/pieces/staunton) as data URIs and Monaspace Xenon from the toolkit submodule - into one self-contained page.

    python docs/design/build-mockup.py [out.html]

The page is written to build/design/main-window-mockup.html unless another path is given; it is never committed (it
carries the font, half a megabyte). The window's own render of the same state is main-window.png beside this script.
"""

import base64
import json
import pathlib
import sys

here = pathlib.Path(__file__).resolve().parent
root = here.parent.parent
pieces_dir = root / "assets" / "pieces" / "staunton"
font = root / "external" / "reckless-toolkit" / "assets" / "fonts" / "monaspace" / "MonaspaceXenon-Regular.otf"


def main(out):
    pieces = {}
    for svg in sorted(pieces_dir.glob("*.svg")):
        pieces[svg.stem] = "data:image/svg+xml;base64," + base64.b64encode(svg.read_bytes()).decode("ascii")
    if len(pieces) != 12:
        raise SystemExit(f"build-mockup.py: twelve pieces expected in {pieces_dir}, found {len(pieces)}")
    if not font.exists():
        raise SystemExit(f"build-mockup.py: {font} is missing - fetch the toolkit: git submodule update --init")
    page = (here / "main-window-mockup.html").read_text(encoding="utf-8")
    page = page.replace("{{PIECES_JSON}}", json.dumps(pieces))
    page = page.replace("{{MONO_FONT}}", base64.b64encode(font.read_bytes()).decode("ascii"))
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(page, encoding="utf-8", newline="\n")
    print(f"{out}: {len(pieces)} pieces, {len(page) // 1024} KB")


if __name__ == "__main__":
    main(pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / "build" / "design" / "main-window-mockup.html")
