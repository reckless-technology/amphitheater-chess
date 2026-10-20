# The built-in opening book

`zenith-book-r4.bin` is Zenith's own Polyglot opening book, built entirely from Zenith's self-play by Zenith's
`tools/build_book.py` - every move screened and rescored by the engine, near-ties settled by self-play playouts, the
scores backed up over the positions reached - with replies to plausible moves Zenith would not choose itself, so it
answers 1.d4, 1.c4 and the like. About 14,000 positions; no external book or game data went into it.

It is the file `books/zenith-book-r4.bin` of the zenith repository (github.com/reckless-technology/zenith) as of
commit 65c7e9f, byte for byte (SHA-256 6e1bd5f8ed85d9de3065dc729fc999bd68346a86d8a698bd4e20ed84346456db). Copyright
2026 Jonny Reckless, who holds the sole copyright of zenith and its book and ships it here under this product's own
licence.

The window carries it inside its binary (`books/zenith.bin`, src/app/CMakeLists.txt) and opens it at a first launch:
File > Built-in book.
