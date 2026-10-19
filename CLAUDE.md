# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Reckless UCI Host is a chess GUI and UCI engine host for Windows, macOS and Linux, in C++20. This repository holds its
toolkit-free core and a command-line runner; the README is the long-form reference (the libraries, the threading
model, the command line, the tests).

**The plan: the GUI comes on reckless-toolkit.** It will be drawn with Skia on the shared toolkit being extracted
from the vst-plugins repository, with the piece artwork already in `assets/pieces/cburnett/` (SVGs, drawn as vector
paths; BSD-3-Clause, `THIRD-PARTY-NOTICES.txt`). Nothing here depends on the toolkit or on vst-plugins: keep it that
way, so the libraries stay testable on their own. The GUI's needs shape the APIs: an `Engine` is polled from one
thread (the toolkit has no thread-safe post, so the frame callback drains `PollEvent`); a tournament reports through
a thread-safe event queue; `CallbackPlayer` is the human side (its callback blocks on the game's thread until the GUI
hands it a move); `EngineConfig` is plain data the GUI will save with the toolkit's JSON.

## Architecture

- `src/chess/` (`uci_host_chess`): the rules. `Position.h`, `Move.h`, `Bitboard.h`, `Square.h`, `AttackTables.h`,
  `Attacks.h`, `CastlingRights.h`, `Pins.h`, `StackList.h`, `ChessTypes.h` are **adapted from pawnstar** (commit
  `c41099798db594ae2c379cba94d917c9c28d9c04`, each header says so): the board, the legal move generator, copy-make,
  magic attacks and Zobrist keys. pawnstar is GPLv3 with Jonny its sole copyright holder, who relicenses his own code
  into this proprietary product; nothing from any other engine is copied. The departures are listed in `Position.h`:
  a validating FEN reader (strict and tolerant, `Result` errors), int counters, castling advancing the halfmove clock,
  `RepetitionKey` (en passant only when the capture is legal), no null move. Classical chess only. On top, the host's
  own: `Notation` (SAN and UCI), `Game` (rules outcomes: mate, stalemate, insufficient material, threefold, fifty),
  `Pgn` (reader with variations, NAGs, comments, error recovery per game; writer with the Seven Tag Roster and 80
  columns), `TimeControl` and `Clock`, `Perft`, `Result`/`Status`.
- `src/uci/` (`uci_host_uci`): `ChildProcess` (`ProcessWindows.cpp`: CreateProcessW, a handle list, a kill-on-close
  job object; `ProcessPosix.cpp`: posix_spawn, a process group, SIGPIPE ignored), `ThreadSafeQueue`, `UciProtocol`
  (every info field, every option type, `go`/`position`/`setoption` formatting), `Engine` (the session: handshake,
  options, isready, searches, ponder, quit-then-kill), `EngineConfig`, `Utf8Arguments`.
- `src/match/` (`uci_host_match`): `Player`/`CallbackPlayer`/`EnginePlayer`, `GameRunner` (clocks with a margin,
  flag falls - a draw when the opponent cannot mate - illegal moves, crashes, stalls, `Adjudicator`, `GameRecord` with
  its PGN and per-move comments `{+0.35/18 1.2s}`), `Openings` (EPD, PGN), `Schedule` (circle method), `Statistics`
  (Elo with a 95 % margin from the trinomial variance, LOS, standings with Sonneborn-Berger, crosstable), `Tournament`
  (worker threads, each with its own engines; events in a queue).
- `src/cli/`: `RunCli` (in a library the tests call in-process), `main.cpp`, `Version.cpp` (the one file that reads
  the generated version header).
- `tests/`: one GoogleTest binary per library, the fake engine (`tests/fake_engine/FakeEngine.cpp`, its behaviours
  scripted by arguments), `tests/data/` (pawnstar's perft EPD, assorted PGN), the integration suite (a real engine via
  `UCI_HOST_TEST_ENGINE`, skipped otherwise).

## Commands

```
cmake --preset debug && cmake --build --preset debug && ctest --preset debug     # also release, coverage, asan
cmake --preset release-universal                                                 # macOS: arm64 + x86_64
powershell -ExecutionPolicy Bypass -File tools\coverage-windows.ps1              # Windows coverage, red under 90 %
ctest --preset debug -R match                                                    # one suite (CTest name)
build/debug/tests/uci_host_match_test --gtest_filter='GameRunner.*'              # one test
UCI_HOST_TEST_ENGINE=/path/to/engine ctest --preset release -R integration      # against a real engine
build/release/src/cli/uci-host-cli perft startpos 6
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
- **Docs** describe the code as it is (no history, no changelog voice), in British spelling; no third party is named
  in docs, comments or test data, beyond tools, platforms and eponymous terms.
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
- `*.sh` and the hooks stay LF (`.gitattributes`); the piece SVGs are kept byte for byte (`-text`).
