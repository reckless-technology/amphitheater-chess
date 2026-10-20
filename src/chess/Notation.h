#pragma once
/// @file Notation.h Moves as text: UCI's long algebraic notation and Standard Algebraic Notation (SAN).
///
/// Both directions are judged against a position's legal moves, so a parsed move is always legal and a printed one
/// always reads back to itself.

#include "Move.h"
#include "Position.h"
#include "Result.h"

#include <string>
#include <string_view>

namespace amphitheater::chess
{

/**
 * A move in UCI's long algebraic notation: from, to and a lower-case promotion letter ("e2e4", "e7e8q"); castling
 * is the king's move ("e1g1").
 * @param move A move, or the null move.
 * @return The text; "0000" for the null move.
 */
std::string MoveToUci(Move move);

/**
 * Read a move in UCI's long algebraic notation. Castling is also accepted as the king onto its rook ("e1h1"), as
 * some engines write it.
 * @param position The position the move is made in.
 * @param text The text.
 * @return The legal move, or why the text is not one.
 */
Result<Move> ParseUciMove(const Position &position, std::string_view text);

/**
 * A move in SAN: the piece letter, the least disambiguation that makes it unique (file, then rank, then both), 'x'
 * for a capture, the destination, "=Q" for a promotion, and '+' for check or '#' for mate. Castling is O-O or O-O-O.
 * @param position The position the move is made in.
 * @param move A legal move.
 * @return The text, or "--" for the null move.
 */
std::string MoveToSan(const Position &position, Move move);

/**
 * Read a move in SAN, tolerantly: castling as O-O or 0-0 (either case), any check, mate or annotation suffix (+, #,
 * !, ?, "e.p."), a promotion with or without '=' (or in brackets), more disambiguation than needed, a missing or
 * extra 'x', a '-' between squares, and long algebraic or UCI notation ("Ng1f3", "e2-e4", "e7e8q").
 * @param position The position the move is made in.
 * @param text The text.
 * @return The legal move, or why the text is not one (illegal, ambiguous, or a promotion without its piece).
 */
Result<Move> ParseSan(const Position &position, std::string_view text);

} // namespace amphitheater::chess
