# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Reckless Amphitheater is a chess GUI and UCI engine host for Windows, macOS and Linux, in C++20. This repository holds its
toolkit-free core and a command-line runner; the README is the long-form reference (the libraries, the threading
model, the command line, the tests).

**Licence:** closed-source and free - `LICENSE` (proprietary) for the repository, `EULA.txt` for the releases'
users. The window links the proprietary toolkit; its third-party parts are in `THIRD-PARTY-NOTICES.txt`, written by
`tools/third_party_notices.py <build-dir>` (CTest `notices` checks it). The installers carry Jonny's five GPLv3 chess
engines as separate programs - aggregation, never linking: the host only talks to them over UCI pipes and no engine
code may come into this tree - and the notices give each one's version, copyright, the GPL's text once and its
corresponding source (the public repository at the tag, and the release's source archive where it has one).

**The bundled engines** (README, "The bundled engines" and "Packages and releases"): pinned in
`packaging/engines.json` (tag, asset, SHA-256 per platform; `tools/update-engines.sh`/`.ps1` moves them to the latest
releases, then the notices are written again), laid out by `packaging/stage.py` with the manifest `bundled.json`, and
seeded into a person's engine list at every start by `src/app/BundledEngines.h`, whose rules the README spells out and
`tests/app/BundledEnginesTest.cpp` holds. On macOS the bundled engines are arm64 only (the host is universal). No
engine may ship that needs more than the system: the stage refuses a Windows engine importing a DLL Windows lacks (a
compiler's runtime) unless a pinned companion lies beside it (none is needed: every pinned release links its runtime
statically), and starts every engine with only the system on its PATH.

**The GUI is built on reckless-toolkit**, a git submodule at `external/reckless-toolkit` (private; CI reads it with
the deploy key in the secret `RECKLESS_TOOLKIT_DEPLOY_KEY`, through `.github/actions/toolkit`), added with the
toolkit's GUI on, its suites off and no audio device layer. Only `src/app/` uses it: the libraries under `src/chess`,
`src/uci`, `src/match` and `src/cli` depend on nothing of the toolkit or of vst-plugins, so they stay testable on their
own, and `-DAMPHITHEATER_GUI=OFF` builds them with no submodule. The toolkit's include namespace is flat (`"View.h"`), so
the host's headers are included by their directory (`"chess/Position.h"`) and never take a toolkit header's name. A
bump of the submodule points at a commit on the toolkit's `main` (the changes job checks).

**The pieces** come in two sets, each `assets/pieces/<set>/{w,b}{K,Q,R,B,N,P}.svg`, chosen under View > Pieces:
Colin M.L. Burnett's (`cburnett`, the default), written by `convert_pieces.py` beside them from the Wikimedia Commons
originals in `source/` (BSD, `source/LICENSE.txt`, credited in the notices; svgelements and skia-pathops in
`tools/requirements.txt`; CTest `pieces_cburnett` checks them), and Jonny's own Staunton set (`staunton`), written by
`gen_pieces.py` beside it (`preview.png` shows it). Regenerate rather than edit by hand. Each is a 100 x 100
viewBox using only `<path>` with the commands M, L, Q, C and Z (absolute), `<circle>`, one `<linearGradient>` in
`<defs>` (x1/y1/x2/y2 in the object's bounding box, two `<stop>`s with `stop-color`), and the presentation attributes
`fill` (a colour, `none` or `url(#id)`), `stroke`, `stroke-width`, `stroke-linejoin`, `stroke-linecap` and
`stroke-opacity`: the subset the toolkit's SVG reader must draw for the GUI.

**The app's icon** is Jonny's pencil sketch of the Colosseum (`assets/art/amphitheater-colosseum.jpg`), unaltered, on a
rounded square, rendered at every size from the full-resolution artwork by `tools/make_icon.py` (resvg-py and Pillow,
pinned in `tools/requirements.txt`, in the `.venv`) into `assets/icon/` and committed: the build needs no Python and
never scales one bitmap. The app's CMake takes `Icon.ico` and `Icon.icns` from there rather than
the toolkit's `reckless_bundle_icon`; the installers, the macOS uninstaller, the Debian package's hicolor icons and
the Linux window's 256 take the same files. Change the composition in the script and run it again (`--check` holds the
committed files to it); never edit a render.

The GUI's needs shape the APIs: an `Engine` is polled from one thread (the toolkit has no thread-safe post, so the
frame callback drains `PollEvent`); a tournament reports through a thread-safe event queue; `CallbackPlayer` is the
human side (its callback blocks on the game's thread until the GUI hands it a move); `EngineConfig` is plain data the
GUI will save with the toolkit's JSON.

## Architecture

- `src/chess/` (`amphitheater_chess`): the rules. `Position.h`, `Move.h`, `Bitboard.h`, `Square.h`, `AttackTables.h`,
  `Attacks.h`, `CastlingRights.h`, `Pins.h`, `StackList.h`, `ChessTypes.h` are **adapted from pawnstar** (commit
  `c41099798db594ae2c379cba94d917c9c28d9c04`, each header says so): the board, the legal move generator, copy-make,
  magic attacks and Zobrist keys. pawnstar is GPLv3 with Jonny its sole copyright holder, so its code may go out under
  either licence this product chooses; nothing from any other engine is copied. The departures are listed in
  `Position.h`: a validating FEN reader (strict and tolerant, `Result` errors), int counters, castling advancing the
  halfmove clock, `RepetitionKey` (en passant only when the capture is legal), no null move. Classical chess only. On
  top, the host's own: `Notation` (SAN and UCI), `Game` (rules outcomes: mate, stalemate, insufficient material,
  threefold, fifty), `Pgn` (reader with variations, NAGs, comments, error recovery per game; writer with the Seven Tag
  Roster and 80 columns), `PolyglotBook` (Polyglot .bin books: the format's 781-key table in `PolyglotRandom.cpp`,
  generated from its description; keys checked against its published test keys), `TimeControl` and `Clock`, `Perft`,
  `BoardSetup` (a position set up square by square, read back strictly), `GameTree` (a game with its variations,
  read from and written to `PgnGame`), `Result`/`Status`.
- `src/uci/` (`amphitheater_uci`): `ChildProcess` (`ProcessWindows.cpp`: CreateProcessW, a handle list, a kill-on-close
  job object; `ProcessPosix.cpp`: posix_spawn, a process group, SIGPIPE ignored), `ThreadSafeQueue`, `UciProtocol`
  (every info field, every option type, `go`/`position`/`setoption` formatting), `Engine` (the session: handshake,
  options, isready, searches, ponder, quit-then-kill), `EngineConfig`, `Utf8Arguments`.
- `src/match/` (`amphitheater_match`): `Player`/`CallbackPlayer`/`EnginePlayer`, `GameRunner` (clocks with a margin,
  flag falls - a draw when the opponent cannot mate - illegal moves, crashes, stalls, `Adjudicator`, each side's own
  Polyglot book (`BookSettings`), `GameRecord` with its PGN and per-move comments `{+0.35/18 1.2s}` - the score from
  White's side, `MoveNote::WhiteScore` - and `{book}`), `Openings` (EPD, PGN, a Polyglot book walked to a depth), `GameAnalysis` (an engine over every
  position of a game on its own thread; each move judged by the winning chances it cost), `TestSuite` (an EPD suite's
  `bm`/`am` positions, run against an engine on its own thread),
  `Schedule` (circle method), `Statistics` (Elo with a 95 % margin from the trinomial variance, LOS, standings with
  Sonneborn-Berger, crosstable), `Tournament` (worker threads, each with its own engines; events in a queue).
- **Scores:** an engine's UCI score is from the side to move in the position it searches; everything the host shows
  or writes turns it to White's side first (`uci::WhitesView`; `EvalPointOf`, `WhiteScoreText` in the window), the
  sign and the bounds flipped with Black to move. `MoveNote::score` keeps the engine's own word, which `Adjudicator`
  judges; the engine log is never rewritten.
- **Pondering** lives in `EnginePlayer`, driven by `Player::MoveMade`, which the runner calls on both players after
  every move with the clocks charged: the engine that has just moved starts "go ponder" on its own ponder move, and
  its next `RequestMove` sends "ponderhit" (its clock from then) or "stop" (the answer thrown away, the wait charged).
  Nothing waits on the pondering engine, so it ponders through a person's move as through an engine's; `EndGame` stops
  a ponder search left running, and an engine that will not stop is written off (`is_healthy_`) and started afresh.
- `src/cli/`: `RunCli` (in a library the tests call in-process), `main.cpp`. `src/version/Version.cpp` is the one file
  that reads the generated version header; the command line and the window both link it.
- `src/app/` (`amphitheater_gui`, `Amphitheater`, `amphitheater_render_main`): the window on the toolkit's app shell. Views
  (`MainView` lays them out as the mockup does; `BoardView`, `PlayerStrip`, `MovesList`, `GameCard`, `AnalysisPanel`,
  `EvalGraph`, `EngineLog`, `StatusBar`) show what they are given and tell what a person does; the
  controllers (`MainController`) hold the game and the engines and set the views each frame, and the menu bar
  (`Menus.h`, the toolkit's `MenuBar` with its shortcuts, handed to the platform's `SetMenuBar`) whenever what it can
  do changes. A game on the board is a
  `PlaySession`: `GameRunner` on its own thread, the person a `CallbackPlayer` waiting on a condition variable for the
  board's move (or the deadline, or the stop - `GameRunner` abandons a cancelled game whose side answers with no move),
  its progress and the engines' lines polled by the controller once a frame; a stopped session finishes its thread in
  the background (`retiring_`) so the window never waits on an engine's move. `SearchTracker` follows an engine's
  search from the lines it exchanges. `AnalysisSession` owns the analysing engines: started on worker threads
  (`std::async`), handed to the window's thread, polled with `PollEvent` once a frame, quit by `EngineReaper` on worker
  threads; a new position is a "stop" and, once "bestmove" comes, "position" and "go infinite". A match or a
  tournament is a `CompetitionSession` over `match::Tournament` with `does_report_moves` on; its dialog
  (`CompetitionDialog`) writes amphitheater-cli's arguments and reads them with `cli::ParseTournamentArguments`, so the
  window and the command line never disagree on an option - add an option to the command line, and to the dialog as an
  argument. `PositionEditor` is Edit > Set up position over `chess::BoardSetup`; `GameAnalysisDialog` is Game >
  Analyse the game, which writes `[%eval]` comments, NAGs and the engine's lines into the tree; `TestSuiteDialog` is
  Engines > Test suite (`amphitheater-cli epd` on the command line); `BookPanel` is File > Open book's list of a
  Polyglot book's moves under the card, drawn faintly on the board too. `EngineCatalogue` is the engine list
  (`reckless-technology/amphitheater/engines.json` in the user's settings folder); `WindowSizes` the View menu's window
  sizes, held by `AppRoot::SetFixedZoom` and kept in `window.json` beside it; `LayoutSettings` the engine log's height
  and whether it shows (its header, `LogHeader`, folds it and drags it), the pieces and the book open, kept in
  `layout.json`. `Theme.h` holds the
  mockup's colours and the fonts; `Commands.h` the menu and plate commands; `Showcase.cpp` the mockup's state, which
  the render tool and the suite draw. Files here never take a toolkit header's name (`View.h`, `Menu.h`, `Window.h`):
  they are included as `"app/BoardView.h"` from outside the directory.
- `tests/`: one GoogleTest binary per library, the fake engine (`tests/fake_engine/FakeEngine.cpp`, its behaviours
  scripted by arguments), `tests/data/` (pawnstar's perft EPD, assorted PGN), the integration suite (a real engine via
  `AMPHITHEATER_TEST_ENGINE`, skipped otherwise).

## Commands

```
cmake --preset debug && cmake --build --preset debug && ctest --preset debug     # also release, coverage, asan
cmake --preset release-universal                                                 # macOS: arm64 + x86_64
powershell -ExecutionPolicy Bypass -File tools\coverage-windows.ps1              # Windows coverage, red under 90 %
ctest --preset debug -R match                                                    # one suite (CTest name)
build/debug/tests/amphitheater_match_test --gtest_filter='GameRunner.*'              # one test
AMPHITHEATER_TEST_ENGINE=/path/to/engine ctest --preset release -R integration      # against a real engine
build/release/src/cli/amphitheater-cli perft startpos 6
```

On Windows everything runs natively (never WSL) in Visual Studio's developer environment: `VsDevCmd.bat -arch=x64
-host_arch=x64 -no_logo` puts cmake, ninja, cl and clang-cl on the PATH. `tools/setup-dev.ps1` / `tools/setup-dev.sh`
set a machine up (VS Code: "setup environment"; `-Check` / `--check` only reports).

- **Style:** pawnstar's `.clang-format` (Microsoft base, 120 columns), checked with clang-format **22.1.8** only - the
  PyPI wheel; Homebrew's 23 and apt's 18 restyle clean code. The pre-commit hook refuses staged C++ that is not clean.
  Read comments in the diff after formatting: a block comment past 120 columns is reflowed badly and needs re-wrapping
  by hand. A lambda returning a reference is mangled by the aligned-declaration style: return a pointer.
- **Naming:** names spelled out, no invented abbreviations; booleans start `is`/`has`/`can`/`may`/`do`/`does`;
  PascalCase functions, `snake_case_` members, `kPascalCase` constants, `#pragma once`. Doxygen-style `/** */`
  comments on declarations (`///<` for members), `@param`/`@return` included. The adapted pawnstar headers keep
  pawnstar's own comment style and public members.
- **Docs** describe the code as it is (no history, no changelog voice), in British spelling. No product is a clone or
  a model of a third-party product, or presented as one. Naming third parties in comparative or reference terms is
  fine: credits and citations, the source of data, test positions or assets, programs it was compared with, and the
  tools and dependencies. So the perft suite's source is credited and the command line is compared with other match
  runners, but Reckless Amphitheater is never described as a clone of another GUI or match runner.
- **Tests:** every new behaviour gets a test; engine behaviours go into the fake engine as a scripted argument.
  Timing-based tests use generous bounds: the CI runners are shared machines.
- MSVC builds use the static runtime in every configuration (the GUI's prebuilt Skia will need it); macOS floor 13.3.

## CI and repository rules

Every job runs on the organisation's self-hosted runners (`[self-hosted, Linux, X64]`, `[self-hosted, Windows, X64]`,
`[self-hosted, macOS, ARM64]`), never on hosted images. `build.yml`: Linux gcc, Linux sanitizers, Linux coverage
(gcovr 8.3, `--fail-under-line 90` on `src/`), Windows MSVC, macOS universal (lipo and minos checks), clang-format,
and `CI`, the aggregate to require. Documentation-only changes build nothing.

- Every commit is authored and committed as `Jonny Reckless <jonny@recklesstechnology.com>`, checked by `.githooks/`
  (pre-commit: identity and formatting; pre-push: every outgoing commit's identity). In a fresh clone:
  `git config user.name 'Jonny Reckless' && git config user.email jonny@recklesstechnology.com &&
  git config core.hooksPath .githooks`.
- Work on a branch and open a pull request; `main` holds only what built and passed.
- `build.yml` also makes every package unsigned and holds it to its stage; `release.yml` (a pushed `vMAJOR.MINOR`, or
  by hand on `main`, `-f dry_run=true` to publish nothing) builds from nothing, signs (eSigner on Windows whenever its
  secrets are set; Developer ID, notarisation and stapling on macOS) and publishes a private release; `promote.yml`
  copies one to the public `reckless-technology/amphitheater-releases` - only on Jonny's word. The signing secrets are the
  organisation's; `PUBLIC_RELEASES_TOKEN` is this repository's own.
- `*.sh` and the hooks stay LF (`.gitattributes`); the piece SVGs are kept byte for byte (`-text`).
