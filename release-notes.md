Reckless Amphitheater is a chess GUI and engine host for Windows, macOS and Linux: play against UCI engines, analyse a
position with several at once, and run matches and tournaments between them with clocks, opening books, pondering,
adjudication and PGN. amphitheater-cli runs the same matches and tournaments from the command line. The installers carry
five chess engines, ready to play the moment the host first opens: Pawnstar, Trebuchet, Pawnstar-Go, Pawnstar-RS and
Stoopid Chess, each a separate program under the GNU General Public License version 3, with its source a click away
in THIRD-PARTY-NOTICES.txt.

## What's changed

- Pawnstar, Pawnstar-Go and Pawnstar-RS move from 2.3.0 to 2.3.1, which no longer plays a move it never searched. In a
  very fast game, a clock that ran out while the engine was still ordering the moves at the start of its search could
  leave it with nothing finished to play and an arbitrary move chosen; it now finishes a quick first look at every move
  before the deeper one, so it always has a searched move to play. Pawnstar went from 24 such moves to none, Pawnstar-Go from 57
  and Pawnstar-RS from 110, in games at one second plus a hundredth of a second.
- Trebuchet stays at 1.1.0 and Stoopid Chess at 4.1.0, each at its latest release.
