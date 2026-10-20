#!/usr/bin/env python3
"""The book maker's test library, written beside this script: what tests/match/BookMakerTest.cpp reads.

    python3 tests/data/library/make_library.py

    folder/one.pgn               two games, on disk
    folder/deeper/two.PGN        one game, a folder down, its extension in capitals
    folder/notes.txt             not games: passed over
    folder/more.zip              one game deflated (three.pgn) and one stored (sub/four.pgn), and a text file
    deflate/<name>.deflate       raw DEFLATE streams of deflate/text.txt: stored blocks (level 0), fixed-Huffman
                                 blocks (Z_FIXED) and dynamic-Huffman blocks (level 9), for Inflate's own tests

The streams and the archive are Python's zlib's and zipfile's, a second implementation the decoder is held to.
"""
import zipfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent

ONE = """[Event "one"]

1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 *

[Event "two"]

1. e4 c5 2. Nf3 d6 *
"""
TWO = """[Event "three"]

1. d4 d5 2. c4 e6 *
"""
THREE = """[Event "four"]

1. e4 e5 2. Nf3 Nf6 *
"""
FOUR = """[Event "five"]

1. c4 e5 *
"""


def deflate(data, level, strategy=zlib.Z_DEFAULT_STRATEGY):
    """A raw DEFLATE stream of the data."""
    compressor = zlib.compressobj(level, zlib.DEFLATED, -15, 9, strategy)
    return compressor.compress(data) + compressor.flush()


def main():
    folder = HERE / "folder"
    (folder / "deeper").mkdir(parents=True, exist_ok=True)
    (folder / "one.pgn").write_bytes(ONE.encode())
    (folder / "deeper" / "two.PGN").write_bytes(TWO.encode())
    (folder / "notes.txt").write_bytes(b"not games\n")
    with zipfile.ZipFile(folder / "more.zip", "w") as archive:
        archive.writestr(zipfile.ZipInfo("three.pgn", (2026, 10, 20, 0, 0, 0)), THREE, zipfile.ZIP_DEFLATED)
        archive.writestr(zipfile.ZipInfo("sub/", (2026, 10, 20, 0, 0, 0)), b"")
        archive.writestr(zipfile.ZipInfo("sub/four.pgn", (2026, 10, 20, 0, 0, 0)), FOUR, zipfile.ZIP_STORED)
        archive.writestr(zipfile.ZipInfo("readme.txt", (2026, 10, 20, 0, 0, 0)), "not games", zipfile.ZIP_DEFLATED)

    streams = HERE / "deflate"
    streams.mkdir(exist_ok=True)
    # Text that repeats, so the compressors use back-references, and more than one stored block's 65,535 bytes.
    text = "".join(f"{n}. e4 e5 {n + 1}. Nf3 Nc6 game {n * 7919 % 1000}\n" for n in range(4000)).encode()
    (streams / "text.txt").write_bytes(text)
    for name, stream in (("stored", deflate(text, 0)), ("fixed", deflate(text, 9, zlib.Z_FIXED)),
                         ("dynamic", deflate(text, 9))):
        (streams / f"{name}.deflate").write_bytes(stream)
    print(f"wrote the library in {HERE}")


if __name__ == "__main__":
    main()
