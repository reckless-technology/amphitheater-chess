# Backlog: an SPRT and paired-game (pentanomial) output in `amphitheater-cli`

**Status: backlog, not started.** Jonny, 2026-11-22: "add it to the backlog for amphitheater in its repo as a note but
do not start work on it." Start only on his word. Tracked as issue
[#72](https://github.com/reckless-technology/amphitheater-chess/issues/72); close it with the pull request that builds this.

## Why it is here

On 2026-11-22 the organisation began moving its chess match testing off lin-0, its only self-hosted Linux CI machine,
onto free GitHub-hosted runners. The first piece of that is `reckless-technology/pawnstar`'s `.github/workflows/match.yml`,
which splits a match into fixed-game chunks across matrix jobs and pools them with `pawnstar/tools/pool_matches.py` and
`pawnstar/tools/opening_icc.py`. Every engine runs those matches through **fastchess**, pinned at 1.8.2-alpha (commit
`f618e34`), which works on the hosted runners. Nothing needs this item, so it is for whenever Amphitheater's own runner
should replace fastchess there.

## What `amphitheater-cli match` already has (surveyed 2026-11-22, not re-checked since)

- `-concurrency <n>`, `-pgnout <file>` with per-move comments, `-resign`, `-draw`, `-maxmoves`, and arguments in the
  cutechess/fastchess style, so match scripts carry over (`src/cli/Cli.cpp`, about lines 745-790).
- Opening slices: `-openings file= format=epd|pgn|polyglot order= plies= start=<n>` with `-srand <n>`
  (`OrderOpenings(..., start - 1)`, `Cli.cpp` about line 1114). Whether a chunk stops at the end of its slice was not
  verified.
- `src/match/Statistics.h`: a W-D-L score line and an Elo estimate with a 95 % margin.

## What it lacks

1. **Pentanomial output.** Results counted per colour-reversed pair, as `Ptnml(0-2) [LL, LD, DD/WL, WD, WW]`, which
   is what `pool_matches.py` reads from a fastchess log. Pairs are the unit, because the two games of a pair share an
   opening and are not independent. Today a pool would have to rebuild pairs from the PGN.
2. **An SPRT.** `-sprt elo0= elo1= alpha= beta=` with the log-likelihood ratio on the pentanomial model (fastchess and
   fishtest use the generalised SPRT on normalised or logistic Elo; say which, and test it against fastchess on the
   same pentanomial counts). Also a periodic `-ratinginterval`-style report, and stopping when the LLR crosses a bound.
3. **Output that `pawnstar/tools/pool_matches.py` accepts unchanged**, so the hosted workflows could swap runners
   without touching their pooling.

## How to test it when it is built

- Unit tests in `tests/` on hand-worked pentanomial counts and LLR values, with fixtures that have the expected
  figures computed independently (as `pawnstar/test/pool_matches_test.py` does for its pooling).
- The same match played by fastchess and by `amphitheater-cli` from the same openings at fixed depth, on deterministic
  engines, must give identical pair results.
