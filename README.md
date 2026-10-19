# Reckless UCI Host

A chess GUI and UCI engine host for Windows, macOS and Linux, in C++20. This repository holds its toolkit-free core -
the rules of chess, UCI engines as child processes, games, matches and tournaments between them - `uci-host-cli`, a
command-line match and tournament runner on that core, and the window, `RecklessUciHost`, drawn with Skia on
reckless-toolkit (a git submodule at `external/reckless-toolkit`), which drives the same libraries.

Proprietary: see [LICENSE](LICENSE). Everything in it is Jonny Reckless's own work, the piece set in
`assets/pieces/staunton/` for the GUI among it (twelve SVGs, the generator that wrote them and a preview); the tests
fetch GoogleTest, which no product ships.

## What is here

| Library | Directory | What it does |
|---|---|---|
| `uci_host_chess` | `src/chess/` | The board and its legal move generator (adapted from pawnstar), FEN (strict and tolerant), SAN and UCI notation, games and the rules that end them, PGN reading and writing, Polyglot opening books, time controls and clocks |
| `uci_host_uci` | `src/uci/` | A child process with pipes and reader threads (Windows and POSIX), the UCI protocol parsed and formatted, and the engine session |
| `uci_host_match` | `src/match/` | Players, the game runner (clocks, legality, adjudication, books, pondering, PGN with per-move comments), openings, the round-robin schedule, statistics, and tournaments played in parallel |
| `uci_host_cli_core`, `uci-host-cli` | `src/cli/` | The command line: `match`, `tournament`, `perft`, `analyse` |
| `uci_host_gui`, `RecklessUciHost` | `src/app/` | The window on the toolkit: the board, the moves, the game's card, the analysis, the evaluation graph, the engine log, the menus, and the controllers behind them |

The suites are in `tests/` (GoogleTest), with a scriptable fake engine (`tests/fake_engine/`) that the UCI, match and
command-line suites play against: normal, slow, crashing, hanging, garbage-writing, illegal-moving.

### The chess core

`Position` is pawnstar's board (commit `c41099798db594ae2c379cba94d917c9c28d9c04`): bitboards with magic sliding
attacks, a legal move generator that handles checks, pins and the en passant discovered check directly, copy-make,
and Zobrist hashing. The host adds a FEN reader that validates what it reads (pawnstar's generator trusts its castling
rights and en passant square), integer counters, castling that advances the halfmove clock as the fifty-move rule
counts it, and a repetition key that counts an en passant square only when the capture is legal. Classical chess only.

Its proof is pawnstar's perft suite, `tests/data/standard-perft.epd` (the Ethereal engine's standard.epd): 128
positions, every one to the deepest depth within a node budget (a million nodes a position in a debug build, twenty
million optimised), and the start position to depth 6 (119,060,324) in an optimised build. Every legal move of
those positions and their children round-trips through SAN and UCI notation.

### Opening books

Polyglot books (`.bin`) are read whole and searched by binary search on the position's key, the format's own Zobrist
hash of 781 constants (`chess/PolyglotBook.h`; the reference is the format's description, "PolyGlot book format",
whose test keys the suite checks). As the format specifies, the en passant file counts when a pawn of the side to move
stands beside the pawn that made the double step, legal capture or not; castling is stored as the king taking its own
rook. A book chooses at random by weight (seeded with `std::mt19937_64`, so a match plays the same again) or the
heaviest move. A book serves two ways: as an engine's own book, whose moves the host plays for it while the book has
one and the game is shorter than its depth (`book=`, `bookdepth=`), and as a match's opening source, walked from the
starting position to a number of plies, one line for each pair of colour-swapped games. Book moves are unclocked and
carry a `{book}` comment in the PGN.

### Pondering

An engine set to ponder (`ponder`, or `EngineConfig::does_ponder`) has its Ponder option set. After it answers
"bestmove X ponder Y" and X is played, the host sends the game with Y after it and "go ponder" with the clocks as they
then stand, and the engine thinks while its opponent does - an engine, or a person at the GUI, since the opponent's
side never waits on it. If the reply is Y the host sends "ponderhit" and the engine's clock starts then; otherwise
"stop", the ponder search's answer thrown away, then the usual search, the wait for the stop charged to the engine
(which loses on time if it never answers). Pondering is never charged to the pondering engine's clock. An engine that
names no ponder move does not ponder, and a game that ends while an engine ponders stops its search; one that will not
stop is started afresh for the next game.

### Threads

An `Engine` is driven by one thread, its owner. Its process's output is read on two reader threads (standard output
and standard error) into a thread-safe queue, which the owner drains: `NextEvent` waits up to a timeout, `PollEvent`
never waits, `Search` blocks until "bestmove", a deadline or the engine's exit. The GUI will poll from its frame
callback; a game runner, on its own thread, blocks in `Search`.

A `Tournament` shares its schedule among as many worker threads as its concurrency. Each worker keeps its own engine
processes - one per engine it has played with, restarted after a crash or a timeout - so no engine is ever touched by
two threads. Progress goes into a thread-safe queue of events for the owner to drain, closed after the last one.

Every wait has a deadline. An engine that crashes loses by disconnection, one that hangs loses on time (or by a
stall with no clock), one that writes garbage loses nothing by it, one that plays an illegal move or none loses, and
one that ignores "quit" is killed. On Windows each engine runs in a job object that kills it when the host goes,
however the host goes; on Linux and macOS each runs in a process group of its own and reads the end of its input.

## Building

Set up a machine with `tools/setup-dev.ps1` (Windows) or `tools/setup-dev.sh` (Linux, macOS) - the compiler, CMake,
Ninja, clang-format 22.1.8 and gcovr, the toolkit submodule, a `.venv` with Pillow for the app's icon (Windows and
macOS), the libraries the window links on Linux (X11, FreeType, fontconfig), the git hooks and the editor's
extensions; `-Check` / `--check` only reports. In VS Code these are the "setup environment" and "check environment"
tasks.

The window is built on the toolkit, a private repository at `external/reckless-toolkit`: clone with
`git clone --recurse-submodules`, or `git submodule update --init` after a plain clone. `-DUCI_HOST_GUI=OFF` builds
the core, the command line and their suites alone, with no submodule. The app lands at
`build/<preset>/src/app/RecklessUciHost.exe` on Windows, `build/<preset>/src/app/RecklessUciHost.app` on macOS and
`build/<preset>/src/app/RecklessUciHost` on Linux.

```
cmake --preset release && cmake --build --preset release && ctest --preset release
cmake --preset debug   && cmake --build --preset debug   && ctest --preset debug
cmake --preset release-universal                        # macOS: arm64 + x86_64, for macOS 13.3 on
cmake --preset coverage && cmake --build --preset coverage && ctest --preset coverage   # gcc / clang
cmake --preset asan && cmake --build --preset asan && ctest --preset asan                # gcc / clang
powershell -ExecutionPolicy Bypass -File tools\coverage-windows.ps1     # Windows coverage, red under 90 %
```

On Windows, build from a Visual Studio developer prompt (MSVC, its CMake and Ninja); the binaries link the static
runtime.

### Tests

| CTest | Binary | What it holds |
|---|---|---|
| `chess` | `uci_host_chess_test` | perft, FEN, SAN and UCI notation, games and draw rules, PGN, Polyglot keys and books, time controls and clocks |
| `uci` | `uci_host_uci_test` | the protocol's parsers and formatters; the child process and the engine session against the fake engine |
| `match` | `uci_host_match_test` | games between fake engines (every way one ends, pondering, books), the schedule, statistics, openings, tournaments |
| `cli` | `uci_host_cli_test` | the command line in-process: every subcommand and the arguments it refuses |
| `cli_perft`, `cli_match` | `uci-host-cli` | the executable end to end |
| `integration` | `uci_host_integration_test` | a real engine, named by `UCI_HOST_TEST_ENGINE`; skipped when unset |
| `gui` | `uci_host_window_test` | the window with no window: the pieces, the board's geometry and a person's moves on it, the lists, the graph, the controller, the window drawn through Skia |
| `app_smoke` | `RecklessUciHost --smoke` | the app opens its window and draws thirty frames (label `window`: a display is needed) |

```
UCI_HOST_TEST_ENGINE=/path/to/engine ctest --preset release -R integration --output-on-failure
UCI_HOST_PERFT_BUDGET=200000000 build/release/tests/uci_host_chess_test --gtest_filter='Perft.*'
```

## The window

`RecklessUciHost` is the GUI, on the toolkit's app shell (`AppRun`, `AppRoot`: the window, its loop, the zoom below the
layout's least and the tooltips), its child window on Skia and its views. The layout is the approved mockup's
(`docs/design/main-window-mockup.html`, viewable through `docs/design/build-mockup.py`): the menu bar; the board
column - a player's strip above and below the board, with the running clock lit amber, and the plates |< < > >| Flip
Analyse - beside the moves and the game's card; across the foot the analysis, the evaluation graph and the engine log in
Monaspace Xenon; the status bar. The board grows with the window; under 1240 x 1000 design pixels the whole layout is
zoomed down rather than squeezed.

The pieces are the Staunton SVGs, carried inside the binary and drawn by the toolkit's SVG reader at whatever size the
squares are. The board takes a person's moves by click or by drag, marks the legal targets and offers a picker for a
promotion; the keys step through the game (Left, Right, Home, End) and F flips the board.

Game > New game plays a game on the board: either side the person at the window or any configured engine, under a
time control (a preset or the command line's syntax), from the starting position or the position shown. The game is
the core's `GameRunner` on a thread of its own (`PlaySession`): an engine is an `EnginePlayer`, the person a
`CallbackPlayer` whose chooser waits for the board's move, the deadline or a stop. Once a frame the window takes in
the game's progress and the engines' lines - into the log, and into each engine's block in the analysis panel
(`SearchTracker`: thinking or pondering and since when, its lines in SAN, its figures) - and runs the clocks. Take
back removes the person's last move and the engine's reply and resumes the game from what was left on the clocks;
Stop the game abandons it and keeps it on the board; a finished game's result and reason show over the board. With
no game being played, a move on the board takes the game shown on from the position shown.

Analyse (the plate, or Engines > Analyse) runs the engines marked for analysis in the engine list - every configured
engine where none is marked - in infinite mode on the position the board shows, with three lines each where an engine
offers MultiPV (`AnalysisSession`). Each engine is started on a worker thread - its process, its handshake, MultiPV and
UCI_AnalyseMode - then handed to the window's thread, which polls its events once a frame and never waits: stepping to
another position stops the search under way and starts the next when its "bestmove" comes. The panel shows each
engine's block, the board the first engine's best move as an amber arrow, and the graph the first engine's score for
each position analysed. Engines are quit on worker threads too (`EngineReaper`), so the window never waits on one.

`uci_host_render_main <out.png> [scale] [width height] [flipped] [empty]` draws the window to a PNG with the mockup's
state, with no window and no display: the review images in `docs/design/` (`main-window.png`, `main-window-flipped.png`)
are made with it.

## The command line

The arguments follow the conventions cutechess-cli and fastchess share, so their scripts carry over with little
change; the output's score and Elo lines read the same way.

```
uci-host-cli match -engine cmd=./engine-a name=A -engine cmd=./engine-b name=B -each tc=40/60+0.6 option.Hash=64
                   -games 2 -rounds 50 -repeat -concurrency 4 -openings file=book.epd order=random
                   -resign movecount=3 score=600 -draw movenumber=40 movecount=8 score=10 -pgnout games.pgn
uci-host-cli tournament -engine cmd=./a -engine cmd=./b -engine cmd=./c -each st=0.5 -double -concurrency 3
uci-host-cli match -engine cmd=./a book=book.bin bookdepth=16 ponder -engine cmd=./b ponder -each tc=60+0.5
                   -games 2 -rounds 20 -repeat -openings file=book.bin format=polyglot plies=8
uci-host-cli perft startpos 6
uci-host-cli perft "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1" 4 -divide
uci-host-cli analyse -engine cmd=./engine -fen startpos -moves e2e4 e7e5 -time 10
```

Engines take `cmd=`, `name=`, `dir=`, `arg=` (repeatable), `option.<name>=<value>`, their own `tc=`, `st=`,
`depth=` and `nodes=`, a Polyglot `book=` with its `bookdepth=`, and `ponder`; `-each` gives keys to every engine, an
engine's own keys winning. A time control is
`[moves/]time[+increment]`, times in seconds or minutes:seconds, sessions separated by commas (`40/120,20/60,15+30`).
A match prints each game as it ends and the score after it - wins, losses, draws, the score fraction, the Elo
difference with its 95 % margin, the likelihood of superiority and the draw ratio; a tournament ends with its standings
(points, then Sonneborn-Berger) and a crosstable. `uci-host-cli --help` has the whole syntax. Ctrl+C stops a match,
abandoning the games in progress.

## Versions

One version, MAJOR.MINOR.BUILD, from git alone: MAJOR.MINOR is the nearest `vMAJOR.MINOR` tag reachable from HEAD and
BUILD the commits since it (`cmake/UciHostVersion.cmake`). With no such tag the build reports 0.0.0 and the configure
says so. Nothing in the tree is edited to release.

## CI

`.github/workflows/build.yml`, on the organisation's self-hosted runners only: Linux (gcc, Release), Linux under
AddressSanitizer and UndefinedBehaviorSanitizer, Linux coverage (gcovr, red under 90 % of `src/`'s lines), Windows
(MSVC, Release), macOS (universal, each binary held to arm64 and x86_64 and to macOS 13.3), clang-format 22.1.8, and
`CI`, the one job to require.
