Reckless Amphitheater is a chess GUI and engine host for Windows, macOS and Linux: play against UCI engines, analyse a
position with several at once, and run matches and tournaments between them with clocks, opening books, pondering,
adjudication and PGN. amphitheater-cli runs the same matches and tournaments from the command line. The installers carry
five chess engines, ready to play the moment the host first opens: Pawnstar, Trebuchet, Pawnstar-Go, Pawnstar-RS and
Stoopid Chess, each a separate program under the GNU General Public License version 3, with its source a click away
in THIRD-PARTY-NOTICES.txt.

## What's changed

- Zenith is now Trebuchet: the engine was renamed because another chess program uses the name, and the host now
  carries Trebuchet 1.0.0. Its numbering restarted with the new name, so 1.0.0 is newer than the Zenith 3.0.0 it
  replaces.
- Trebuchet 1.0.0 is no stronger than Zenith 3.0.0: its network and opening book are the same bytes and its bench
  signature has not moved. The one change in what it does is that a castling right the board cannot support, which a
  FEN could claim with no rook on the corner, is now dropped as the FEN is read; before, the search could castle
  illegally from such a position and corrupt its board. No legal position changes.
- Stoopid Chess moves from 3.0.0 to 4.1.0. Its releases since are about its own graphical interface, which the host
  does not carry: the installers bring its engine alone, whose search and evaluation have not changed, so it plays as
  it did. Pawnstar, Pawnstar-Go and Pawnstar-RS stay at 2.1.0, every engine now at its latest release.
- An engine list from an earlier release comes across at the first start: the bundled Zenith becomes Trebuchet in the
  same place in the list with your settings kept, a name you gave it stays, and if you removed it, it stays removed.
  An engine of your own is left as it is, and the PGNs and logs you have saved still say Zenith.
- An open window with nothing moving no longer repaints itself about forty times a second, which took about a fifth of
  a processor core on the Windows machine it was measured on, and now takes almost none.
- The window draws with the faster routines Skia carries for processors with AVX2, no longer spins between frames on
  Linux, and on a display scaled to 125 % or 175 % opens at the window size it kept, where it could come up a pixel
  short and lose it.
