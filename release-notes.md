Reckless Amphitheater is a chess GUI and engine host for Windows, macOS and Linux: play against UCI engines, analyse a
position with several at once, and run matches and tournaments between them with clocks, opening books, pondering,
adjudication and PGN. amphitheater-cli runs the same matches and tournaments from the command line. The installers carry
five chess engines, ready to play the moment the host first opens: Pawnstar, Trebuchet, Pawnstar-Go, Pawnstar-RS and
Stoopid Chess, each a separate program under the GNU General Public License version 3, with its source a click away
in THIRD-PARTY-NOTICES.txt.

## What's changed

- Pawnstar, Pawnstar-Go and Pawnstar-RS move from 2.1.0 to 2.2.0, which fixes a crash or hang on a FEN claiming a
  castling right with no king and rook at home: such a right is now dropped as the FEN is read. No legal position
  changes, so they play as they did and their bench signature has not moved. Trebuchet stays at 1.0.0 and Stoopid Chess
  at 4.1.0, every engine at its latest release.
- The host's own games, matches and analysis were not affected, because they send an engine a FEN they have rebuilt
  with such rights dropped. The one way to reach the fault from here was amphitheater-cli's **analyse**, which passes
  its `-fen` to the engine as typed.
