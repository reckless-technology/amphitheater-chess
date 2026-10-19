# Reckless UCI Host

A chess GUI and UCI engine host for Windows, macOS and Linux, in C++20. This repository holds its toolkit-free core -
the rules of chess, UCI engines as child processes, games, matches and tournaments between them - and `uci-host-cli`,
a command-line match and tournament runner on that core. The GUI comes next, drawn with Skia on reckless-toolkit; it
will drive the same libraries.

Proprietary: see [LICENSE](LICENSE). Everything in it is Jonny Reckless's own work, the piece set in
`assets/pieces/staunton/` for the GUI among it (twelve SVGs, the generator that wrote them and a preview); the tests
fetch GoogleTest, which no product ships.

## What is here

| Library | Directory | What it does |
|---|---|---|
| `uci_host_chess` | `src/chess/` | The board and its legal move generator (adapted from pawnstar), FEN (strict and tolerant), SAN and UCI notation, games and the rules that end them, PGN reading and writing, time controls and clocks |
| `uci_host_uci` | `src/uci/` | A child process with pipes and reader threads (Windows and POSIX), the UCI protocol parsed and formatted, and the engine session |
| `uci_host_match` | `src/match/` | Players, the game runner (clocks, legality, adjudication, PGN with per-move comments), openings, the round-robin schedule, statistics, and tournaments played in parallel |
| `uci_host_cli_core`, `uci-host-cli` | `src/cli/` | The command line: `match`, `tournament`, `perft`, `analyse` |

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
Ninja, clang-format 22.1.8 and gcovr, the git hooks and the editor's extensions; `-Check` / `--check` only reports. In
VS Code these are the "setup environment" and "check environment" tasks.

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
| `chess` | `uci_host_chess_test` | perft, FEN, SAN and UCI notation, games and draw rules, PGN, time controls and clocks |
| `uci` | `uci_host_uci_test` | the protocol's parsers and formatters; the child process and the engine session against the fake engine |
| `match` | `uci_host_match_test` | games between fake engines (every way one ends), the schedule, statistics, openings, tournaments |
| `cli` | `uci_host_cli_test` | the command line in-process: every subcommand and the arguments it refuses |
| `cli_perft`, `cli_match` | `uci-host-cli` | the executable end to end |
| `integration` | `uci_host_integration_test` | a real engine, named by `UCI_HOST_TEST_ENGINE`; skipped when unset |

```
UCI_HOST_TEST_ENGINE=/path/to/engine ctest --preset release -R integration --output-on-failure
UCI_HOST_PERFT_BUDGET=200000000 build/release/tests/uci_host_chess_test --gtest_filter='Perft.*'
```

## The command line

The arguments follow the conventions cutechess-cli and fastchess share, so their scripts carry over with little
change; the output's score and Elo lines read the same way.

```
uci-host-cli match -engine cmd=./engine-a name=A -engine cmd=./engine-b name=B -each tc=40/60+0.6 option.Hash=64
                   -games 2 -rounds 50 -repeat -concurrency 4 -openings file=book.epd order=random
                   -resign movecount=3 score=600 -draw movenumber=40 movecount=8 score=10 -pgnout games.pgn
uci-host-cli tournament -engine cmd=./a -engine cmd=./b -engine cmd=./c -each st=0.5 -double -concurrency 3
uci-host-cli perft startpos 6
uci-host-cli perft "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1" 4 -divide
uci-host-cli analyse -engine cmd=./engine -fen startpos -moves e2e4 e7e5 -time 10
```

Engines take `cmd=`, `name=`, `dir=`, `arg=` (repeatable), `option.<name>=<value>`, and their own `tc=`, `st=`,
`depth=` and `nodes=`; `-each` gives keys to every engine, an engine's own keys winning. A time control is
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
