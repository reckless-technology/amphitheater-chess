Reckless Amphitheater is a chess GUI and engine host for Windows, macOS and Linux: play against UCI engines, analyse a
position with several at once, and run matches and tournaments between them with clocks, opening books, pondering,
adjudication and PGN. amphitheater-cli runs the same matches and tournaments from the command line. The installers carry
five chess engines, ready to play the moment the host first opens: Pawnstar, Trebuchet, Pawnstar-Go, Pawnstar-RS and
Stoopid Chess, each a separate program under the GNU General Public License version 3, with its source a click away
in THIRD-PARTY-NOTICES.txt.

## What's changed

- Stoopid Chess moves from 4.1.0 to 5.0.0. It fixes three search bugs, so its play changes slightly (a repetition found
  across a null move, the fifty-move draw missed while in check, and the history table capped against the countermove),
  and it now offers only the Hash option, which is the one setting Amphitheater ever sets, in place of the 33
  evaluation-tuning options it listed before.
- Pawnstar, Pawnstar-Go and Pawnstar-RS stay at 2.3.1 and Trebuchet at 1.1.0, each at its latest release.
