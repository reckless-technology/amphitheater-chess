#!/usr/bin/env python3
"""The window's built-in book, amphitheater-book.bin, written beside this script from Zenith's own book beside it.

    python3 assets/books/make_book.py [--check]

Zenith's book (zenith-book-r4.bin, kept byte for byte) opens 1.e4 (weight 115) and 1.c4 (54) and holds no 1.d4 from
the starting position: Zenith scored it outside its builder's keep margin. It does hold full lines after 1.d4 - about
2,100 positions, built as replies for the side facing it - so one entry opens them: 1.d4 from the starting position,
its weight 42 giving it 42 / (115 + 54 + 42) = 19.9 % of the first moves. Every other entry is Zenith's, in its order;
the file stays sorted by key, as the format asks.

--check writes nothing and exits 1 when the committed book differs from what the script writes.
"""
import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SOURCE = HERE / "zenith-book-r4.bin"
TARGET = HERE / "amphitheater-book.bin"

START_KEY = 0x463B96181691FC9C  # the starting position's Polyglot key, the format's own test value
ENTRY = struct.Struct(">QHHI")  # key, move, weight, learn


def move_code(uci):
    """A move packed as Polyglot packs it: to file, to row, from file, from row, three bits each from the lowest."""
    from_file, from_row = ord(uci[0]) - ord("a"), int(uci[1]) - 1
    to_file, to_row = ord(uci[2]) - ord("a"), int(uci[3]) - 1
    return to_file | to_row << 3 | from_file << 6 | from_row << 9


ADDED = [(START_KEY, move_code("d2d4"), 42)]  # 1.d4, about a fifth of the first moves


def build():
    """The built-in book's bytes: Zenith's entries with ADDED among them, sorted by key, each added move after
    Zenith's own for its position."""
    data = SOURCE.read_bytes()
    entries = [ENTRY.unpack_from(data, offset) for offset in range(0, len(data), ENTRY.size)]
    for key, move, weight in ADDED:
        if any(entry[0] == key and entry[1] == move for entry in entries):
            sys.exit(f"make_book.py: Zenith's book already holds move {move:#06x} at {key:#018x}")
        last = max((index for index, entry in enumerate(entries) if entry[0] <= key), default=-1)
        entries.insert(last + 1, (key, move, weight, 0))
    if any(entries[index][0] > entries[index + 1][0] for index in range(len(entries) - 1)):
        sys.exit("make_book.py: the entries are not sorted by key")
    return b"".join(ENTRY.pack(*entry) for entry in entries)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="write nothing; exit 1 when the committed book differs")
    arguments = parser.parse_args()
    book = build()
    if arguments.check:
        if not TARGET.exists() or TARGET.read_bytes() != book:
            print(f"{TARGET.name} differs from what make_book.py writes: run it again", file=sys.stderr)
            return 1
        print(f"{TARGET.name} is as make_book.py writes it")
        return 0
    TARGET.write_bytes(book)
    print(f"wrote {TARGET.name}: {len(book) // ENTRY.size} entries")
    return 0


if __name__ == "__main__":
    sys.exit(main())
