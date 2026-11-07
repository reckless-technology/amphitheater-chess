# The built-in opening book

`amphitheater-book.bin` is the window's built-in book: Trebuchet's own Polyglot book with 1.d4 added as a first move.
`make_book.py` writes it from `trebuchet-book-r4.bin` beside it (`--check` holds the committed file to it; CTest
`book`).

`trebuchet-book-r4.bin` is Trebuchet's book, built entirely from Trebuchet's self-play by Trebuchet's
`tools/build_book.py` - every move screened and rescored by the engine, near-ties settled by self-play playouts, the
scores backed up over the positions reached - with replies to plausible moves Trebuchet would not choose itself. About
14,000 positions; no external book or game data went into it. It is the file `books/trebuchet-book-r4.bin` of the
trebuchet repository (github.com/reckless-technology/trebuchet) at its tag v1.0.0, byte for byte (SHA-256
6e1bd5f8ed85d9de3065dc729fc999bd68346a86d8a698bd4e20ed84346456db).

Trebuchet's book opens 1.e4 (weight 115) and 1.c4 (54) and has no 1.d4 from the starting position, which Trebuchet
scored outside its builder's keep margin; but it holds full lines after 1.d4 - about 2,100 positions, built as replies
for the side facing it. The built-in book adds the one entry that opens them: 1.d4 at weight 42, about a fifth of the
first moves (42 of 211).

Copyright 2026 Jonny Reckless, who holds the sole copyright of trebuchet and its book and ships it here under this
product's own licence.

The window carries the built-in book inside its binary (`books/built-in.bin`, src/app/CMakeLists.txt) and opens it at
a first launch: File > Built-in book.
