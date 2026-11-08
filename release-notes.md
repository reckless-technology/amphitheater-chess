Reckless Amphitheater is a chess GUI and engine host for Windows, macOS and Linux: play against UCI engines, analyse a
position with several at once, and run matches and tournaments between them with clocks, opening books, pondering,
adjudication and PGN. amphitheater-cli runs the same matches and tournaments from the command line. The installers carry
five chess engines, ready to play the moment the host first opens: Pawnstar, Trebuchet, Pawnstar-Go, Pawnstar-RS and
Stoopid Chess, each a separate program under the GNU General Public License version 3, with its source a click away
in THIRD-PARTY-NOTICES.txt.

## What's changed

- Pawnstar, Pawnstar-Go and Pawnstar-RS move from 2.2.0 to 2.3.0, and Trebuchet from 1.0.0 to 1.1.0, each fixing how
  the engine behaves when it is short of time or told to stop. Stoopid Chess stays at 4.1.0, every engine at its
  latest release.
- **Pawnstar, Pawnstar-Go and Pawnstar-RS** no longer play a move whose search the clock cut short. With almost no time
  left, 2.2.0 could play a move it had not finished looking at, sometimes walking into mate; 2.3.0 plays the best move
  of an earlier, deeper search instead, or the best one it had finished. Fast games and time scrambles are where this
  shows.
- Pawnstar and Pawnstar-RS now answer a position with no legal move, the last position of a game ended by mate or
  stalemate. Before, they never answered, so analysing a finished game's last position left that engine's analysis
  stuck there when you moved back through the game.
- Pawnstar-Go now clears what it has learnt when told a new game begins, as the other two already did. A game in a
  match no longer depends on the one before it, and amphitheater-cli's **screenbook** now gets each score from it
  from a fresh search, as it means to, not one coloured by the search before.
- **Trebuchet** no longer misses a stop that comes straight after it starts searching. Analysis stops an engine and
  starts it again each time the position on the board changes, so stepping quickly through a game could leave
  Trebuchet searching an old position for ever, its analysis no longer following the board.
- Trebuchet cut short before it finishes its first, shallowest search now plays a legal move. It used to answer
  `0000`, no move at all, which a game scores as a loss.
- Trebuchet writes each line it sends whole, so the host can no longer find a reply joined onto a line of analysis.
