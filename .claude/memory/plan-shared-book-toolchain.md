# Plan: one book toolchain, driving engines over UCI

**Status: approved 2026-10-31 and being built.** Written after a day spent repairing
`pawnstar/tools/filter_book.cpp` and finding that the repair was unnecessary in the first place.

**This gates the next Amphitheater release.** Jonny, 2026-10-31: *"Let's not do another amphitheater release
until we have completed the refactoring of the opening book code from pawnstar and zenith."* So the two draft
releases that exist (`v1.0.0` and `v1.0.11`) stay drafts, and nothing here is a tidy-up to be fitted in around
other work - it is the thing the release is waiting on.

## The problem

Three repositories build opening books, and each wrote its own version of the same three steps -
count what gets played, screen the moves by an engine's evaluation, write a book file:

| Repository | What it builds | Input | Screened by |
|---|---|---|---|
| `pawnstar` | `tools/make_book.cpp` -> the suite's text book | Lichess PGN dump | - |
| `pawnstar` | `tools/filter_book.cpp` -> marks `?` on weak moves | its own text book | Pawnstar's NNUE, **linked in** |
| `zenith` | `tools/build_book.py` -> Polyglot `.bin` | Zenith self-play | Zenith, over its own subcommands |
| `amphitheater-chess` | `src/match/BookMaker` -> Polyglot `.bin` | PGN libraries, folders, zips | nothing |

Only `filter_book` links an engine, and that is the one that broke. Its `Game` member carries the
net's weights by value, so `sizeof(Game)` is 12.05 MB; two `Game`s as locals overflowed the stack and
the tool had been unrunnable since the 8-bucket architecture landed. Nobody found out until a book
needed filtering, because nothing else builds against it.

## The insight the repair produced

Every time `filter_book`'s arithmetic had to be checked, it was checked by driving the engine over
UCI and comparing:

```sh
printf 'setoption name Threads value 1\nucinewgame\nposition fen <fen>\ngo depth 12\n'      # the reference
printf 'setoption name Threads value 1\nucinewgame\nposition fen <fen>\ngo depth 12 searchmoves <m>\n'
```

Those two commands reproduced the tool's numbers exactly - `+46` and `-60` for `f8g7` after
1.e4 g6 2.f4, and `+102` for the restricted `e2e4` after 1.Nf3 g6. **The screener never needed to
link an engine.** Linking it is what embedded 12 MB on the stack, and what forces one copy of the
screener per engine.

Three facts make a UCI screener work, and all three are verified, not assumed:

1. **`ucinewgame` clears the transposition table** (`handle_ucinewgame`, and its comment explains why
   `SetPosition` deliberately does not). That is the per-search isolation the screener needs: without
   it the parent search's fail-low bounds leak into the next search and a verdict depends on what the
   engine looked at before it. Finding that internally cost a day; over UCI it is one command.
2. **`setoption name Threads value 1`** makes a search reproducible. Under Lazy SMP the helper threads
   race through the shared table and the score is not reproducible.
3. **`go depth N searchmoves <move>`** scores one move at the same depth as an unrestricted search.
   This is the only way to get a comparable number: an unrestricted search and a single-move search
   of the *same move* can differ by 88 cp, because a root holding twenty-odd moves prunes and reorders
   nothing like a root holding one.

## Where it goes

**`amphitheater-chess`**, as subcommands of `amphitheater-cli`. It is already the program this
describes: it hosts UCI engines, reads and writes Polyglot (`src/chess/PolyglotBook`), reads PGN books
(`src/chess/PgnBook`), builds books from PGN libraries (`src/match/BookMaker`), chooses match openings
from EPD, PGN or a Polyglot walk (`src/match/Openings`), and already exposes `makebook` on the command
line. A `screenbook` beside `makebook` is an addition, not an architecture. It is public and GPLv3, so
nothing is hidden by living there.

## What does NOT move

- **Zenith's self-play playouts and DAG score backup.** Settling near-ties by playing them out, and
  backing scores up over the positions reached, is Zenith's method, not a generic book operation.
  `zenith/tools/build_book.py` keeps it.
- **An engine's judgement.** The screener supplies the method; which moves are weak is whatever the
  `--engine` says. Same split as `nnue-training`: shared method, engine-specific judgement.
- **The books themselves.** Each engine keeps and ships its own.

## Phases

Each phase ends in something checkable, and no phase lands without its check passing.

**1. A UCI client with the three guarantees.** ~~Write one.~~ **Already written, and this is the main
reason the work belongs here.** `src/uci/Engine.h` is a UCI session with a cross-platform `ChildProcess`
behind it, and it has every piece the screener needs:

| What the screener needs | What `src/uci/` already has |
|---|---|
| force one thread | `Engine::SetOption("Threads", "1")` |
| clear the table before each search | `Engine::NewGame()` - sends `ucinewgame`, waits for `readyok` |
| set a position | `Engine::SetPosition(fen, moves)` |
| a fixed-depth search | `GoParameters::depth`, and a blocking `Engine::Search` with a deadline |
| **score one move at that depth** | `GoParameters::search_moves` - already there, already formatted by `FormatGo` |
| the score and the chosen move | `SearchInfo::score` and `BestMove`, both parsed |

So phase 1 is a thin adapter over an existing, tested, cross-platform client, not a client. What is left to
write is the one thing it does not have: a probe that an engine honours `searchmoves`, so an engine that
ignores it is refused rather than silently scoring something else.
*Check:* against `pawnstar`, reproduce the three hand measurements above to the centipawn.

**2. The text book format.** Read and write the suite's book: one line per opening, coordinate moves,
`#` comment to end of line, a trailing `?` marking a move that is replayed but never chosen. Polyglot
is already done.
*Check:* read `pawnstar/pawnstar.book` and write it back byte for byte.

**3. The screener.** Per position: `go depth N` for the engine's chosen move, then
`go depth N searchmoves <chosen>` for the reference, then `go depth N searchmoves <m>` per book move.
Mark a move more than the margin below the reference. Where every move at a position is marked, append
a line ending in the engine's own choice - **carrying the marks on the prefix**, because writing the
prefix bare puts every marked move along the way back into the selectable set and undoes the marking
that created the line. Cache a verdict per (position, move) and a reference per position.
*Check:* the acceptance test below.

**4. Retire `pawnstar/tools/filter_book.cpp`.** Only once phase 3 reproduces its output. Keep its file
comment's findings - they are the reason the tool works - by moving them into the new one.

## The acceptance test, which is the point of the whole exercise

**Given the book this repository's engines ship, `--depth 12 --margin 100` must reproduce the
committed file byte for byte.** Without that, consolidation is a rewrite with no way to tell whether
it changed the answer.

And a second test the existing engines make possible, which is stronger than it looks:

**Screening the same book with `pawnstar`, `pawnstar-rs` and `pawnstar-go` must give three identical
files.** The three ports are bit-exact with each other at fixed depth, single-threaded - the bench
node count, 5,617,906 at depth 13, is the standing check - so three identical books prove the screener
takes nothing from the engine but its scores. If they differ, the screener is reading something it
should not: a thread count, a hash size, a time control, its own previous search.

## Risks, with what to do about each

| Risk | What to do |
|---|---|
| **Throughput.** Two `go depth 12`s per move through a pipe, where the linked tool had them in-process. Today's run was ~12,800 searches in about 6 minutes on 24 threads. | Run engines in parallel, one process per worker, as the linked tool ran one `Game` per worker. Measure before optimising; a book is filtered rarely. |
| **`searchmoves` support varies.** Standard UCI, but not every engine implements it. | Probe at startup and refuse. `stoopid-chess` is the one to check before it gets a book. |
| **An engine that is not deterministic** even single-threaded with the table cleared. | The three-port identity test above catches exactly this. |
| **The text format's details.** Move numbers pass through, a `#` comment is preserved, no line is a prefix of another. | Phase 2's byte-for-byte round trip is the check, and it comes before any screening. |
| **Scope creep into a book GUI.** | `screenbook` is a CLI subcommand. The window already has `File > Make book from games`; whether it gains a screening dialog is a separate decision. |

## Why this is worth doing at all

Not to remove duplication for its own sake. The reason is that `filter_book` was broken for months in
three independent ways and nothing noticed, because it is a tool in one repository that nothing builds
against and no test runs. Moved into `amphitheater-cli` it sits in a program with a test suite, a CI
job on every platform, and a second user - and the engine it screens with becomes an argument rather
than a link-time dependency, so the next architecture change cannot quietly break it.
