# Reckless Amphitheater

A chess GUI and UCI engine host for Windows, macOS and Linux, in C++20. This repository holds its toolkit-free core -
the rules of chess, UCI engines as child processes, games, matches and tournaments between them - `amphitheater-cli`, a
command-line match and tournament runner on that core, and the window, `Amphitheater`, drawn with Skia on
reckless-toolkit (a git submodule at `external/reckless-toolkit`), which drives the same libraries.

Source-available and free, not open source: anyone may read the code here, but [LICENSE](LICENSE) reserves every right
and grants no licence to use, copy, modify or distribute it; the releases are licensed to their users under
[EULA.txt](EULA.txt).
Everything in it is Jonny Reckless's own work, the piece set in `assets/pieces/staunton/` for the GUI among it (twelve
SVGs, the generator that wrote them and a preview); the tests fetch GoogleTest, which no product ships. The window
links the toolkit's third-party parts (Skia and the libraries it bundles, two typefaces), each under the licence
[THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt) reproduces. The installers also carry Jonny's five chess engines,
each a separate GPLv3 program the host only talks to over UCI pipes - aggregation, not linking - with the GPL's text
and each engine's source (its public repository at the release's tag) in the notices.

## What is here

| Library | Directory | What it does |
|---|---|---|
| `amphitheater_chess` | `src/chess/` | The board and its legal move generator (adapted from pawnstar), FEN (strict and tolerant), SAN and UCI notation, games and the rules that end them, PGN reading and writing, Polyglot opening books, time controls and clocks |
| `amphitheater_uci` | `src/uci/` | A child process with pipes and reader threads (Windows and POSIX), the UCI protocol parsed and formatted, and the engine session |
| `amphitheater_match` | `src/match/` | Players, the game runner (clocks, legality, adjudication, books, pondering, PGN with per-move comments), openings, the round-robin schedule, statistics, and tournaments played in parallel |
| `amphitheater_cli_core`, `amphitheater-cli` | `src/cli/` | The command line: `match`, `tournament`, `perft`, `analyse` |
| `amphitheater_gui`, `Amphitheater` | `src/app/` | The window on the toolkit: the board, the moves, the game's card, the analysis, the evaluation graph, the engine log, the menus, and the controllers behind them |

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

The window opens a book a third way, to read: File > Open book lists the book's moves for the position shown under
the game's card (`BookPanel`), the heaviest first, each with a bar and the share of the position's weight it holds and
its weight, and draws the five heaviest faintly on the board in blue, the heavier the clearer, under an engine's
amber arrow; a move under a twentieth of the weight gets no arrow. A press on a move in the list plays it as a move on
the board does: on along the line shown, a variation leaving it, or a person's move in a game. While a book is open the
card takes only the height its lines need. The book open is kept in `layout.json` and opened again at the next
launch; File > Close book takes it away.

The window carries a book of its own (`assets/books`, embedded as `books/built-in.bin`): Zenith's, about 14,000
positions built entirely from Zenith's self-play, with replies to openings Zenith does not choose itself, and 1.d4
added as a first move (`assets/books/make_book.py`) - Zenith's lines after it were there already - so the book opens
1.e4, 1.c4 and 1.d4, the last about a fifth of the time. A first launch opens it, and File > Built-in book opens it
again. File > Open book takes a person's own book too: a Polyglot `.bin`,
or a PGN file, read as a book of its games' openings (`chess/PgnBook.h`) - every move each game and each of its
variations plays in its first 40 plies, weighted by the number of games that play it in that position.

A book can be made from a library of games, a quicker way to one than self-play for whoever has a collection of good
games (`match::BookMaker`): File > Make book from games, or `amphitheater-cli makebook`, reads every game of a PGN file,
of a zip archive's PGN files, or of a folder's PGN files and archives and those of the folders under it, and takes each
game's first moves of each side - seven by default - as book moves, each weighted by the number of games that play it
in its position (scaled down alike where the most passes the format's 65,535). A move played in fewer games than asked
is left out, so a large library's one-off moves can be kept from the book. The book is written as a Polyglot `.bin`
and, in the window, opened. Archives are read by the host's own zip reader and DEFLATE decoder (`match/ZipArchive.h`,
`match/Inflate.h`): stored and deflated entries; Zip64 archives and encrypted entries are refused. The window's file
chooser picks files, so a folder is chosen by any PGN file in it, or typed.

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
Ninja, clang-format 22.1.8 and gcovr, the toolkit submodule, a `.venv` with `tools/requirements.txt` (the icon's
renderer, below, and the suites' Python), the libraries the window links on Linux (X11, FreeType, fontconfig), the git
hooks and the editor's extensions; `-Check` / `--check` only reports. In VS Code these are the "setup environment" and "check environment"
tasks.

The window is built on the toolkit, a private repository at `external/reckless-toolkit`, which only the organisation
can read: clone with `git clone --recurse-submodules`, or `git submodule update --init` after a plain clone. Without
it, as anyone outside the organisation is, `-DAMPHITHEATER_GUI=OFF` builds the core libraries, `amphitheater-cli` and
their suites from a plain clone (`cmake --preset release -DAMPHITHEATER_GUI=OFF`); the window cannot be built without
the submodule. The app lands at
`build/<preset>/src/app/Amphitheater.exe` on Windows, `build/<preset>/src/app/Amphitheater.app` on macOS and
`build/<preset>/src/app/amphitheater` on Linux.

A plain build - `make`, or `cmake --build build` - builds the app, `Amphitheater`, and what it links, and nothing else
(with `-DAMPHITHEATER_GUI=OFF`, `amphitheater-cli` in its place). `amphitheater_everything` builds every target below;
the presets build it, and so does `ctest`, which builds it before it runs a test, so a plain build and then `ctest`
work. To build one target, name it:

| Target | What it builds |
|---|---|
| `Amphitheater` | the app, the window (needs the toolkit submodule): the default target |
| `amphitheater_everything` | every target below |
| `amphitheater-cli` | the command line: matches, tournaments, perft, analysis, test suites, opening books |
| `amphitheater_render_main` | the window drawn to a PNG with no display, for the review images in `docs/design/` |
| `amphitheater_chess_test`, `amphitheater_uci_test`, `amphitheater_match_test`, `amphitheater_cli_test`, `amphitheater_window_test`, `amphitheater_integration_test` | the suites, one per library (below) |
| `amphitheater_fake_engine` | the scripted engine the suites play against |

```
cmake -S . -B build && cmake --build build       # the app; with the Makefile generator, make -C build
cmake --build build --target amphitheater-cli    # make -C build amphitheater-cli
cmake --build build --target amphitheater_everything && ctest --test-dir build
```

`make help` lists many more: the libraries these link (`amphitheater_chess`, `amphitheater_gui` and the rest), the
toolkit's own libraries, GoogleTest's, and one `reckless_embedded_<folder>_<file>` library for each file the window
carries inside itself - `reckless_embedded_cburnett_wK_svg`, `reckless_embedded_books_amphitheater_book_bin` and so on,
each piece, font, the icon and the opening book: the toolkit compiles each file once however many binaries embed it.
None of them needs building by hand.

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
| `chess` | `amphitheater_chess_test` | perft, FEN, SAN and UCI notation, games and draw rules, PGN, Polyglot keys and books, time controls and clocks |
| `uci` | `amphitheater_uci_test` | the protocol's parsers and formatters; the child process and the engine session against the fake engine |
| `match` | `amphitheater_match_test` | games between fake engines (every way one ends, pondering, books), the schedule, statistics, openings, tournaments |
| `cli` | `amphitheater_cli_test` | the command line in-process: every subcommand and the arguments it refuses |
| `cli_perft`, `cli_match` | `amphitheater-cli` | the executable end to end |
| `integration` | `amphitheater_integration_test` | a real engine, named by `AMPHITHEATER_TEST_ENGINE`; skipped when unset |
| `gui` | `amphitheater_window_test` | the window with no window: the pieces, the board's geometry and a person's moves on it, the lists, the graph, the controller, the window drawn through Skia |
| `app_smoke` | `Amphitheater --smoke` | the app opens its window and draws thirty frames (label `window`: a display is needed) |
| `packaging` | `tests/packaging/test_bundled_engines.py` | the engines' pins, a hash that differs refused, the stage's layout and manifest, the handshake and the smoke test, the pins' update |
| `notices` | `tools/third_party_notices.py --check` | `THIRD-PARTY-NOTICES.txt` as the build's fetches and the engines' pins write it |
| `icon` | `tools/make_icon.py --check` | the committed icon as the script renders it from the artwork; skipped where the Python lacks Pillow or resvg-py |
| `pieces_cburnett` | `assets/pieces/cburnett/convert_pieces.py --check` | the committed cburnett pieces as the script writes them from the Commons originals; skipped where the Python lacks svgelements or skia-pathops |
| `book` | `assets/books/make_book.py --check` | the built-in book as the script writes it from Zenith's, with 1.d4 added |
| `render_main`, `render_flipped`, `render_newgame`, `render_engines`, `render_tournament`, `render_standings`, `render_about` | `amphitheater_render_main` | the window and its dialogs drawn to PNGs with no display |

```
AMPHITHEATER_TEST_ENGINE=/path/to/engine ctest --preset release -R integration --output-on-failure
AMPHITHEATER_PERFT_BUDGET=200000000 build/release/tests/amphitheater_chess_test --gtest_filter='Perft.*'
```

## The window

`Amphitheater` is the GUI, on the toolkit's app shell (`AppRun`, `AppRoot`: the window, its loop, the zoom below the
layout's least and the tooltips), its child window on Skia and its views. The layout follows the approved mockup
(`docs/design/main-window-mockup.html`, viewable through `docs/design/build-mockup.py`) with the board given the room:
the menu bar; the board, as large as the height above the foot allows, its squares whole pixels (80 at the 1380 x 1120
design size); beside it two narrow columns - the moves, a number and two moves in SAN, down the whole height, and the
players' column, a player's strip level with the board's top and the other level with its bottom (the running clock
lit amber), the plates |< < > >| Flip Analyse over the lower one and the game's card between, its values wrapped to its
width; across the foot, at the full width, the analysis, the evaluation graph and the engine log in Monaspace Xenon,
its long lines wrapped to its width at a space and indented under their text; the status bar. A larger window gives
the board the room first and the columns what width it leaves; under 1240 x 1000 design pixels the whole layout is
zoomed down rather than squeezed. The window may be made as small as half the design size.

The engine log sits under a header of its own, ENGINE LOG behind a disclosure triangle. It opens seven rows tall; a
drag on the header makes it taller or shorter, in whole rows, from two rows up to as tall as leaves the board squares
of 40 design pixels, the board giving it the room. A click on the header, or View > Engine log (Ctrl+L, Command-L on a
Mac), folds it away to the header alone and gives the board its room back, and opens it again at the height it had.
The log's height and whether it shows are kept in `reckless-technology/amphitheater/layout.json` in the user's
settings folder (`LayoutSettings.h`), and the next launch opens with them.

View offers five sizes, 50%, 75%, 100%, 125% and 150% of the 1380 x 1120 design size (`WindowSizes.h`): a size chosen
makes the window that size, its top left kept unless the window would then cross an edge of its screen's work area,
when it is moved in until all of it stands there, and the layout is drawn at the design size at exactly that scale
(`AppRoot::SetFixedZoom`). The size drawn is ticked; dragging the frame to any other size ticks none and the layout
follows the window again as above. A size the window's screen cannot take whole is dim rather than shrunk to fit, so a
ticked size is always the one drawn. Larger and Smaller step to the next size that fits and stop at the ends. The size
chosen is kept in `reckless-technology/amphitheater/window.json` in the user's settings folder, and the next launch opens
at it, held to its screen; with none kept - a first launch, or a window last left at a dragged size - the window opens
at the design size, held to its screen. The moves
and the log are a whole number of rows tall, so a list kept on the move shown or the newest line never leaves part of a
row at its top. The last move lights both its squares, each shade in its own tint of the accent.

**Scores are from White's side everywhere the host shows or writes one**: + means White is better, - means Black is,
whichever engine reported it and whichever side it plays. UCI has an engine report its score from the side to move in
the position it searches, so the host turns each one (`uci::WhitesView`): a score with Black to move changes sign, a
mate in n for Black becomes "#-n", and a bound swaps with the sign - a lower bound on Black's score is an upper bound on
White's, shown before the score as "<" (at most) or ">" (at least). A ponder search's side to move is the one in the
position after the expected reply, which the host sent it. This holds for the analysis panel's lines, the evaluation
graph, the PGN comments and the command line's analysis; the engine log is the exchange byte for byte, the engines' own
words untouched. Adjudication alone judges each engine by its own score, as its settings are written: a side resigns
when its engine has said at most -score of its own position.

The board draws its pieces from SVGs carried inside the binary, with the toolkit's SVG reader, at whatever size the
squares are. Two sets are carried (`PieceSet.h`): Colin M.L. Burnett's, the default - the drawings Wikimedia Commons
offers under the BSD licence, among others, written in the reader's SVG by
`assets/pieces/cburnett/convert_pieces.py` from the originals beside it and credited in `THIRD-PARTY-NOTICES.txt` - and
Jonny's own Staunton set. View > Pieces chooses between them, and the choice is kept in `layout.json` beside the
engine log's. The board takes a person's moves by click or by drag, marks the legal targets and offers a picker for a
promotion.

The app's icon is Jonny's pencil sketch of the Colosseum with its red Rt roundel
(`assets/art/amphitheater-colosseum.jpg`, 2048 pixels square), unaltered - neither cropped nor retouched - on a rounded
square with a hairline light-grey edge. `tools/make_icon.py` renders it at every size the platforms carry, each size
resampled once from the full-resolution artwork by a Lanczos filter, never one picture stepped down through the sizes
and never sharpened, the square's shape, edge and shadow drawn by resvg's wheel, with Pillow (`tools/requirements.txt`,
pinned) into `assets/icon/`, which is committed, so the build needs no Python: `Icon.ico` (16 to 256) in the Windows
executable, its installer and uninstaller; `Icon.icns` (16 to 1024, on Apple's grid: an 824-unit rounded square with
its drop shadow) in the macOS bundle and the uninstaller app; the hicolor sizes 16 to 512 in the Debian package; the
256 as the Linux window's icon and as the About window's mark. `--check` holds the committed files to what it renders.
`docs/design/icon-sizes.png` shows every size side by side.

The menu bar is the platform's own, built from the window's state by `Menus.h` on the toolkit's one description of a
bar: the screen's menu bar on a Mac, the window's on Windows, drawn across the window's top on Linux (and in the render
tool's images). Every item's shortcut shows beside it, and the platform offers every key no view takes to the bar, so
the keys work as soon as the window has the keyboard, with no click into it first: Left, Right, Home and End step
through the game and F flips the board; with Command on a Mac and Control elsewhere, N starts a new game, O and S open
and save PGN, C copies the FEN and Shift+C the PGN, V pastes a FEN or a PGN, P sets up a position, Z takes back, E
manages the engines, L shows or folds the engine log, Q quits, 0 makes the window 100 %, = (or +) the next size up and -
the next size down. A list or a line that has the keyboard keeps its own keys, and every menu waits while a dialog is
open. The window's title follows the game - "White vs Black - Reckless Amphitheater", the file's name before the app's
while the game shown was opened from a file or saved to one. The lists' scroll bars are in the accent.

Help > Updates is the toolkit's update check (`update::Checker`, `app::MakeUpdateChecker`): at most once a day, while the
window shows, a plain GET of `https://recklesstechnology.com/updates/amphitheater.json` (the website writes it as a release
is promoted; until then a check finds nothing and says so), the answer kept in `reckless-technology/amphitheater-updates.json`
in the user's settings folder; its menu says what the check found, opens a newer release's page and turns checking by
the clock on and off, and the status bar says UPDATE while a newer release is out. Nothing is downloaded or installed.
A launch's check (`--smoke`) makes none, and `RECKLESS_UPDATE_CHECK=off` - every CI job sets it - switches it off.

Help > About (`AboutDialog`) shows the app's mark - its icon, from the 256-pixel render the binary carries - its name
and version, the copyright, the terms (free, proprietary, under `EULA.txt`), the engines the install carries with their
versions and licences (read from `bundled.json`, every engine whose program is there; none in a build tree), where the
third-party notices are, and recklesstechnology.com as a link to the person's browser. Close, Escape and Return close
it. On a Mac the app menu's own About item opens the same window (`MenuBar::about_id`).

Game > New game plays a game on the board: either side the person at the window or any configured engine, under a
time control (a preset or the command line's syntax), from the starting position or the position shown. The game is
the core's `GameRunner` on a thread of its own (`PlaySession`): an engine is an `EnginePlayer`, the person a
`CallbackPlayer` whose chooser waits for the board's move, the deadline or a stop. Once a frame the window takes in
the game's progress and the engines' lines - into the log, and into each engine's block in the analysis panel
(`SearchTracker`: thinking or pondering and since when, its lines in SAN, its figures) - and runs the clocks. Take
back removes the person's last move and the engine's reply and resumes the game with both clocks as they were recorded
at the move it goes back to (`PlaySnapshot::clocks`, a reading after every move, carried into the resumed game so a
second take-back finds its own);
Stop the game abandons it and keeps it on the board; a finished game's result and reason show over the board.

The game shown is kept with its variations (`chess::GameTree`): every line played from its start, each move with its
comments and NAGs. The board, the moves list and the graph show one line of it at a time. With no game being played, a
move on the board goes on from the position shown: the line's own next move steps on along it, and any other adds a
variation there - or enters the one already there - and shows the line through it; the line it left is kept. Under
each move's row the moves list shows a row for each variation leaving the line at that move, its first moves dimmed,
"main line:" before the main line's while a variation is shown; a press on the row enters the variation at its first
move. Game > Promote variation makes the variation shown its line's main continuation, the old one becoming its first
variation; Game > Delete variation takes it off and shows the line it left, at the position where it left; Game > Take
back takes the line's last move off. A game opened from a file shows its main line with its variations under it, and
File > Save PGN writes every line, the comments and NAGs in their places.

Analyse (the plate, or Engines > Analyse) runs the engines marked for analysis in the engine list - every configured
engine where none is marked - in infinite mode on the position the board shows, with three lines each where an engine
offers MultiPV (`AnalysisSession`). Each engine is started on a worker thread - its process, its handshake, MultiPV and
UCI_AnalyseMode - then handed to the window's thread, which polls its events once a frame and never waits: stepping to
another position stops the search under way and starts the next when its "bestmove" comes. The panel shows each
engine's block, the board the first engine's best move as an amber arrow, and the graph a series for each engine, its
score for each position analysed. Engines are quit on worker threads too (`EngineReaper`), so the window never waits on
one.

An engine's block, in a game or under Analyse, shows the engine's lines under its name, its chip and its figures. An
engine that gives several lines at once (MultiPV) shows each one's latest, the best first; one that gives a line at a
time - every bundled engine - shows its newest line lit and, under it and dimmed, the best line it gave at each depth
before, so the block shows how its thought has gone (`SearchTracker`, which keeps the last six depths). The panel shares
its rows alike among the blocks, as many whole lines each as its height leaves after their headers: five for one engine
alone, two each for the two engines of a game.

Game > Analyse the game goes over the line shown with one engine (`match::GameAnalyser`, on a thread of its own): every
position in turn, for the seconds a move or the depth chosen, a position the game ends in scored by the rules. Each
position's score is written on the move that led to it as the [%eval] command a PGN comment carries - so the graph
draws it, Save PGN keeps it and the file opened again shows it - and each move is judged by what it cost its mover in
winning chances, on the curve lichess publishes (2 / (1 + e^(-0.00368208 cp)) - 1, a mate a win): more than 0.1 is an
inaccuracy, marked ?!, more than 0.2 a mistake (?) and more than 0.3 a blunder (??), the glyph shown beside the move in
the moves list. Where a move was a mistake or a blunder and the engine would have played another, its line, eight
plies at most, is added as a variation with its score. The status bar counts the positions done, and then what the
analysis found; Game > Stop the game analysis ends it after the position under way. Analyse stops while it runs, and a
game opened or played in its place sets it aside.

Engines > Test suite opens an EPD test suite (`match::TestSuite`): each line a position with the move it asks for,
`bm`, or the move it forbids, `am`, and its `id` as its name; a line with neither, or one that cannot be read, is
skipped and counted. Its dialog (`TestSuiteDialog`) lists the positions with what each asks for, and runs them against
a configured engine for the seconds or the depth chosen on each, from a new game: the list fills in the engine's move
and whether it solved the position, in green or red, and the score under it counts the positions solved. A position
pressed twice is shown on the board.

The evaluation graph (`EvalGraph`, its data in `Evaluations.h`) draws each engine's scores as a series of its own, all
from White's side on one scale with the zero line: in a game between engines White's engine's score at each of its
moves and Black's at each of its, so the two interleave ply by ply, the first in amber and the second in blue, a legend
naming each in its colour; a game against a person shows the engine's series alone, Analyse one series per analysing
engine (in the order amber, blue, violet, green), and a game opened from a PGN the series its comments give - the
host's own comments a series per side, named by the White and Black tags, or `[%eval]` commands one series of the whole
game. The scale is one pawn, or the largest score's whole pawns up to ten; a mate is held at the scale's edge and marked
there by a small triangle pointing off it. The caption gives the move shown and its score ("12...b4  -0.27", "#5").
When two copies of one engine play each other, each side's lines, block and series carry its side after its name -
"Pawnstar (White)" - and each side's search is followed on its own, so neither copy's scores are read against the
other's positions.

Engines > Manage engines keeps the engine list: Add... chooses a program and runs its handshake on a worker thread,
which fills the engine's name, author and every option it declares; Detect runs it again. The selected engine's
editor sets the name to show, the program's arguments, pondering, whether Analyse runs it, a depth limit for its
games, its own Polyglot book and the book's depth, and each declared option as a control of its kind - a check box,
a spin's line held to its range, a combo's list, a button pressed at every start, a string's line - a value back at
its default no longer configured. The list is JSON, `reckless-technology/amphitheater/engines.json` in the user's settings
folder (`%APPDATA%` on Windows, `~/Library/Application Support` on macOS, `~/.config` on Linux). As the app starts it
copies into `reckless-technology/amphitheater/` each file it lacks from `reckless-technology/uci-host/`, where earlier
builds kept the engine list and the window's size (`SettingsFolder.h`); nothing there is overwritten, and the earlier
folder is left as it is.

File > Open PGN shows a file's game, or lists a file's many games - number, players, result, event, date, length - to
pick one from (a game that cannot be read is skipped and counted); a `.pgn` named on the command line opens with the
window. File > Save PGN writes the game shown with every variation: a game read from a file with its tags while its main
line is unchanged, a game played on the board with the tags and comments the core records - each move's score (from
White's side), depth and time - and anything else with the Seven Tag Roster from what the card knows. Edit > Copy FEN and Copy PGN put the
position shown or the game on the clipboard; Paste FEN or PGN takes a position, a game, or a list of games to pick
from. The window reads and writes the clipboard through the toolkit's app platform.

Edit > Set up position (`PositionEditor.h`) opens a board of its own on the position shown, turned as the window's is.
A click on a square puts the piece chosen from the palette - the twelve, White's over Black's - there, and a click
where that piece already stands takes it off; a right-click empties a square; a piece dragged to another square moves
there, and dragged off the board is taken off. Beside the board are the side to move, the four castling rights - each
offered only while its king and rook stand on their squares - the en passant square, offered only behind a pawn of the
side not to move that could just have made a double step, and the move number; Start position, Clear board and Flip
board; and under it the position's FEN, which a FEN typed or pasted into and Return reads (an EPD's four fields will
do). The setup is `chess::BoardSetup`, its FEN read back strictly as any FEN is, so the dialog says what keeps it from
being a legal position - a king missing, the side not to move in check, a pawn on the last rank - and Set up waits
until it is one, then shows it on the window's board as a pasted FEN does.

Match > New match and Tournament > New tournament take the engines and amphitheater-cli's options - the time control,
games and rounds, a double round robin, repeated openings, concurrency, an opening file, the adjudications, a PGN
file, and any other the command line has, typed as it takes them - and write them as the command line's own
arguments, which its own reader turns into the tournament's settings (`cli::ParseTournamentArguments`), so the window
and the command line agree on every option and every check. The tournament plays on the core's worker threads
(`CompetitionSession`), reporting every move (`TournamentSettings::does_report_moves`, a `kMoveMade` event): the board
follows the lowest-numbered game in progress, or the one chosen under Watch a game, with its clocks, its engines'
blocks and their lines in the log; the card carries the match's score with its Elo, margin and LOS, or the
tournament's leader. Tournament > Standings and crosstable shows both in the toolkit's lists, followed as games
finish. Stop abandons the games in progress; the workers finish their moves in the background.

`amphitheater_render_main <out.png> [scale] [width height] [view]` draws the window to a PNG with the mockup's state, with
no window and no display; the view is `flipped`, `empty` (a fresh window), `newgame`, `engines` (the engine manager),
`tournament` (a new tournament's setup), `standings` (the standings and crosstable), `about` (Help > About, for
an install carrying the five pinned engines), `logfolded` (the engine log folded away), `setup` (Edit > Set up
position on the sample's position), `book` (a sample opening book open over the sample game) or `makebook` (File >
Make book from games). The review images in `docs/design/` are made with it at 1380 x 1120: `main-window.png`,
`main-window-flipped.png`, `main-window-log-folded.png`, `new-game.png`, `engine-manager.png`, `new-tournament.png`,
`standings.png`, `about.png`, `position-editor.png`, `book.png` and `make-book.png`.

## The command line

The arguments follow the conventions cutechess-cli and fastchess share, so their scripts carry over with little
change; the output's score and Elo lines read the same way.

```
amphitheater-cli match -engine cmd=./engine-a name=A -engine cmd=./engine-b name=B -each tc=40/60+0.6 option.Hash=64
                   -games 2 -rounds 50 -repeat -concurrency 4 -openings file=book.epd order=random
                   -resign movecount=3 score=600 -draw movenumber=40 movecount=8 score=10 -pgnout games.pgn
amphitheater-cli tournament -engine cmd=./a -engine cmd=./b -engine cmd=./c -each st=0.5 -double -concurrency 3
amphitheater-cli match -engine cmd=./a book=book.bin bookdepth=16 ponder -engine cmd=./b ponder -each tc=60+0.5
                   -games 2 -rounds 20 -repeat -openings file=book.bin format=polyglot plies=8
amphitheater-cli perft startpos 6
amphitheater-cli perft "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1" 4 -divide
amphitheater-cli analyse -engine cmd=./engine -fen startpos -moves e2e4 e7e5 -time 10
amphitheater-cli epd -engine cmd=./engine -file suite.epd -time 0.5
amphitheater-cli makebook -games ./classical-games -out classical.bin -moves 7 -least 2
```

Engines take `cmd=`, `name=`, `dir=`, `arg=` (repeatable), `option.<name>=<value>`, their own `tc=`, `st=`,
`depth=` and `nodes=`, a Polyglot `book=` with its `bookdepth=`, and `ponder`; `-each` gives keys to every engine, an
engine's own keys winning. A time control is
`[moves/]time[+increment]`, times in seconds or minutes:seconds, sessions separated by commas (`40/120,20/60,15+30`).
A match prints each game as it ends and the score after it - wins, losses, draws, the score fraction, the Elo
difference with its 95 % margin, the likelihood of superiority and the draw ratio; a tournament ends with its standings
(points, then Sonneborn-Berger) and a crosstable. `amphitheater-cli --help` has the whole syntax. Ctrl+C stops a match,
abandoning the games in progress. `epd` runs an EPD test suite as Engines > Test suite does, for `-time` seconds (one
by default) or to `-depth` on each position, printing a line a position - its number, name, the engine's move and
whether it solved it - and the score at the end, `Solved 12 of 20 (60.0 %)`. `makebook` makes an opening book from
a library of games, as File > Make book from games does: `-games` a PGN file, a zip archive or a folder, `-out` the
`.bin` to write, `-moves` the moves of each side (7 by default) and `-least` the fewest games a move is kept for (1);
it prints each file as it reads it and what it made at the end.

Every score the command line writes is from White's side: each PGN move's comment, `{+0.35/18 1.2s}` (White better,
by the score its mover's engine reported, depth 18, 1.2 seconds), `{-0.35/18 1.2s}` (Black better), `{+M5/20 0.4s}`
(White mates in 5) or `{-M3/20 0.4s}` (Black mates in 3), and `{book}` for a book move; and `analyse`'s lines,
`score -0.40` or `score #-2`, with `lowerbound` or `upperbound` after a bound, from White's side too. `-debug` prints
the engines' lines exactly as they went, their scores their own.

## Versions

One version, MAJOR.MINOR.BUILD, from git alone: MAJOR.MINOR is the nearest `vMAJOR.MINOR` tag reachable from HEAD and
BUILD the commits since it (`cmake/AmphitheaterVersion.cmake`). With no such tag the build reports 0.0.0 and the configure
says so. Nothing in the tree is edited to release.

## The bundled engines

The installers carry five UCI engines, the binary of each one's latest release, unmodified: Pawnstar, Zenith,
Pawnstar-Go, Pawnstar-RS and Stoopid Chess. `packaging/engines.json` pins each one - its repository, its tag, each
platform's asset with its SHA-256 (Zenith's held to its release's SHA256SUMS, every one to GitHub's digest of the
asset), the file inside an archive, and the defaults it is seeded with. `tools/update-engines.sh` (or `.ps1`) moves the
pins to each repository's latest release with fresh hashes and writes the file alone; read the diff, run
`tools/third_party_notices.py <build-dir>` (the notices name each engine's version), and open the pull request.
Packaging downloads by pin into the tool cache and refuses an asset whose hash differs (`tools/bundled_engines.py`).

Every engine must need nothing of a machine but its system. On Windows the stage reads each engine's import table and
refuses a DLL that is neither Windows' own nor beside the engine, and it starts each engine with only the system's
folders on the PATH, so a developer's toolchain cannot hide a missing runtime; on macOS every library an engine loads
must be the system's. Every pinned release meets that: each Windows engine links its compiler's runtime statically
and imports Windows' own DLLs alone. Should a release need a library it does not carry, the pins can put one beside
it as a companion (an engine's `companions`, by platform: the archive it comes from with its hash, the file's own hash
and its licence, which the notices then reproduce), and a release refuses an engine that still lacks one; a pull
request's run leaves such an engine out with a warning (`--leave-out-unfit`) so the rest of the packaging is still
proven.

| Engine | Windows, Linux (x86-64) | macOS |
|---|---|---|
| Pawnstar | one build, AVX2 throughout: needs x86-64-v3 | arm64 |
| Zenith | two builds: x86-64-v3-pext where the CPU has AVX2 and BMI2 with a fast PEXT (not AMD's Zen 1 and 2), else x86-64-v2 | arm64 |
| Pawnstar-Go | one build, AVX2: needs x86-64-v3 | arm64 |
| Pawnstar-RS | one build, any x86-64 | arm64 |
| Stoopid Chess | one build (.NET, self-contained), any x86-64 | arm64 |

On macOS the bundled engines are arm64 only: the engines' releases build no Intel slice, while the host itself is
universal.

They lie in a folder named `engines`, one folder per engine with `bundled.json` beside them: in the install folder on
Windows (`{app}\engines\<engine>\`), inside the bundle on macOS (`Amphitheater.app/Contents/Resources/engines/`),
under `/usr/lib/reckless-amphitheater/engines/` from the Debian package (whose host is in `/opt/reckless-technology/bin`),
and beside the binary in the portable Linux zip. The manifest, which `packaging/stage.py` writes as it lays the engines
out, gives each engine's name, version, licence, seeded options, the options its handshake declared when it was packaged, and
its builds in order, each with what it asks of the CPU; every staged engine answers its handshake before the stage
ends. A build tree's host carries no engines and seeds nothing.

At every start the host reads the manifest (`src/app/BundledEngines.h`, CPUID for the x86-64 levels) and seeds the
person's engine list:

- an engine new to the list is added at its end, marked `bundled` (with its id and version), named as the manifest
  names it, with Threads 1 (where it has the option), Hash 64 MB and, for Pawnstar, Move Overhead 100 ms - never the
  engines' own Threads defaults, which oversubscribe a machine running a match two engines at a time;
- an engine the list already marks bundled follows the install: its program moves to the new path, and on a new
  version its version, the name and author it gives and its declared options move to the new release's, while every
  setting a person makes - the name shown, the options, the arguments, pondering, analysis, the depth, the book - is
  kept;
- an engine the person removed (`removed_bundled` in the list) is never added again;
- a person's own entry for the bundled program itself is marked bundled rather than listed twice;
- every other entry - the person's own engines, and a bundled one this install no longer carries or whose CPU level
  this machine lacks - is left exactly as it is;
- a list that cannot be read is neither seeded nor written over.

The engine manager shows a bundled engine as such ("bundled with the host" and its version, "(bundled)" by its author)
and removes it like any other, for good.

## Packages and releases

`packaging/stage.py <build-dir> <dist-dir> windows|macos|linux` stages a release from a release tree: the window,
`amphitheater-cli`, the engines, `EULA.txt` and `THIRD-PARTY-NOTICES.txt`. From the stage:

| Platform | Package | Made and checked by |
|---|---|---|
| Windows | `reckless-amphitheater-<v>-win64-setup.exe`: Inno Setup, per user by default (the dialog offers Program Files), under `Reckless Technology\Amphitheater`, the engines in `engines\`, signed through SSL.com eSigner | `packaging/windows/make-installer.ps1`; `test-installer.ps1` installs a test build of it, checks every file and engine at its place, upgrades over an earlier release's engine and uninstalls, leaving a sibling product's folder alone |
| macOS | `reckless-amphitheater-<v>-macos.pkg`: the app in `/Applications/Reckless Technology`, `amphitheater-cli` in `/usr/local/bin`, the documents in `/Library/Documentation/Reckless Amphitheater`, an uninstaller beside the app; every engine, the app and the tool signed with the Developer ID and the hardened runtime from the inside out (`packaging/macos/sign.sh`), the package notarised and stapled | `packaging/macos/make-installer.sh`; `test-installer.sh` expands it, holds it to the stage, starts every engine from its place in it, and runs the uninstaller's script on a stand-in for `/` |
| Linux | `reckless-amphitheater-<v>-linux-amd64.deb` and the portable `reckless-amphitheater-<v>-linux-x64.zip` | `packaging/linux/make-deb.sh` and `test-deb.sh`; `make-zip.sh`, which unzips what it made and starts every engine from there |

`.github/workflows/build.yml` makes all of them unsigned on every pull request that changes code. `release.yml` makes
them from nothing on a pushed `vMAJOR.MINOR` tag, or by hand on `main` (`gh workflow run release.yml --ref main`;
`-f dry_run=true` builds, tests, signs, notarises and packages and publishes nothing, on any branch, as 0.0.0 with no
tag), uploads them onto a draft with `release-notes.md` as its notes and pushes the version's tag. The release stays
a draft, which only the repository's collaborators see: the repository is public, so a published release here would
be public at once. A draft with all four packages on it and its tag pushed is finished, and a later run for the same
version fails rather than replace it; a draft short of either, left by a run that stopped part-way, is replaced by the
next run for that version. The Windows installer is signed whenever the eSigner secrets are set, a dry run's included,
at two of the account's signings. `promote.yml` (`gh workflow run promote.yml --ref main -f tag=vX.Y.Z`), the one way
a release reaches the public, copies a finished draft's four files to the public
`reckless-technology/amphitheater-releases`, under their names and fixed ones, with `SHA256SUMS` and the notes, and
wakes the website, which writes `updates/amphitheater.json`; it needs the secret `PUBLIC_RELEASES_TOKEN`.

## CI

`.github/workflows/build.yml`, on GitHub-hosted runners (free for a public repository) whose images are pinned by
version, `ubuntu-24.04`, `windows-2025` and `macos-15`: Linux (gcc, Release), Linux under AddressSanitizer and
UndefinedBehaviorSanitizer, Linux coverage (gcovr, red under 90 % of `src/`'s lines but the app's entry point,
`src/app/Main.cpp`, which opens a window and runs in the window suites), Windows (MSVC, Release), macOS (universal on
Apple silicon, each binary held to arm64 and x86_64 and to macOS 13.3, the x86_64 slice run under Rosetta, the app's
bundle to its signature, the icon its plist names - `assets/icon/Icon.icns` byte for byte - and a plist that asks for
no microphone), each platform's packages unsigned (above), clang-format 22.1.8, and `CI`, the one job to require.
Every job starts on a fresh machine. Each checks the toolkit submodule out with its read-only deploy key
(`.github/actions/toolkit`; a fork's pull request is given no secrets, so its run stops there), and
`.github/actions/cpp` installs what the image lacks - Ninja, the X11, FreeType and fontconfig headers and Xvfb from
apt on Linux - and Python 3.13 with `tools/requirements.txt`, so the `icon` and `pieces_cburnett` checks run rather
than skip. FetchContent fetches GoogleTest, Skia's prebuilt libraries and the toolkit's other dependencies afresh into
each build tree, every one held to its hash. The window suites (label `window`) run under `xvfb-run` on Linux and on the runners' own displays elsewhere.
