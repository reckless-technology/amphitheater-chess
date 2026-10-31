Reckless Amphitheater is a chess GUI and engine host for Windows, macOS and Linux: play against UCI engines, analyse a
position with several at once, and run matches and tournaments between them with clocks, opening books, pondering,
adjudication and PGN. amphitheater-cli runs the same matches and tournaments from the command line. The installers carry
five chess engines, ready to play the moment the host first opens: Pawnstar, Zenith, Pawnstar-Go, Pawnstar-RS and
Stoopid Chess, each a separate program under the GNU General Public License version 3, with its source a click away
in THIRD-PARTY-NOTICES.txt.

## What's changed

- Every bundled engine moves to a new release, and their opening books with them: Pawnstar, Pawnstar-Go and
  Pawnstar-RS go from 1.2.0 to 2.1.0, and Zenith from 2.1.0 to 3.0.0. Stoopid Chess stays at 3.0.0. The three
  Pawnstars now carry an opening book built from the Lichess open database under its CC0 public-domain dedication,
  in place of the one earlier releases carried; each download's THIRD-PARTY-NOTICES.txt names every engine's
  version, copyright and where to get its source.
- amphitheater-cli gains **screenbook**, which screens an opening book against an engine's own judgement: a move the
  engine rates far below its best at that position is marked with a `?`, so the book stops offering moves that
  engine would decline. A marked move is still replayed, so the book keeps its prepared reply to a move the engine
  would not play itself, and a position left with nothing it would play is given the engine's own choice instead.

      amphitheater-cli screenbook -engine cmd=<engine> -book <book> [-depth 12] [-margin 100] [-marks <file>]

- The host reads and writes the engines' text book format, so a book can be read, checked and written back
  unchanged, byte for byte.
