#pragma once
/// @file PolyglotBook.h Opening books in the Polyglot format (.bin).
///
/// A book is a file of 16-byte entries sorted by key, each big-endian: the position's 64-bit key, a 16-bit move, a
/// 16-bit weight and a 32-bit learning value. The key is a Zobrist hash over the format's own table of 781 constants
/// (kPolyglotRandom64): a piece on a square, each castling right, an en passant file and white to move. The en passant
/// file counts when a pawn of the side to move stands beside the pawn that has just made a double step, whether or not
/// the capture would be legal, as the format specifies. A move packs the to square, the from square and a promotion
/// piece; castling is written as the king taking its own rook (e1h1). The reference is the format's description,
/// "PolyGlot book format" (hgm.nubati.net/book_format.html), whose test keys the suite checks.

#include "Move.h"
#include "Position.h"
#include "Result.h"

#include <array>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace uci_host::chess
{

/** The Polyglot format's 781 Zobrist constants. */
extern const std::array<std::uint64_t, 781> kPolyglotRandom64;

/**
 * A position's Polyglot key.
 * @param position The position.
 * @return The key a book stores its moves under.
 */
std::uint64_t PolyglotKey(const Position &position);

/** One entry of a book. */
struct PolyglotEntry
{
    std::uint64_t key    = 0; ///< The position's key.
    std::uint16_t move   = 0; ///< The move, packed as the format packs it.
    std::uint16_t weight = 0; ///< How good the move is; a weight of 0 is never chosen.
    std::uint32_t learn  = 0; ///< The learning value, which the host ignores.
};

/** How a book chooses among a position's moves. */
enum class BookSelection
{
    kWeightedRandom, ///< At random, each move's chance its weight over the total: the format's own rule.
    kBestWeight,     ///< The move of the highest weight (the first of equals).
};

/** A Polyglot book, held in memory and searched by binary search. */
class PolyglotBook
{
  public:
    /**
     * A book from a file's bytes.
     * @param bytes The file, a whole number of 16-byte entries.
     * @return The book, or why the bytes are not one.
     */
    static Result<PolyglotBook> FromBytes(const std::vector<std::uint8_t> &bytes);

    /**
     * A book from a file.
     * @param path The file, UTF-8.
     * @return The book, or why it could not be read.
     */
    static Result<PolyglotBook> Load(const std::string &path);

    /**
     * A book's bytes, the entries sorted by key as the format requires.
     * @param entries The entries, in any order.
     * @return The file's bytes.
     */
    static std::vector<std::uint8_t> Encode(std::vector<PolyglotEntry> entries);

    /**
     * Pack a move as the format does, castling as the king taking its rook.
     * @param move A legal move.
     * @return The packed move.
     */
    static std::uint16_t EncodeMove(Move move);

    /** @return How many entries the book holds. */
    std::size_t Size() const
    {
        return entries_.size();
    }

    /**
     * Every entry for a key.
     * @param key The key.
     * @return The entries, in the book's order.
     */
    std::vector<PolyglotEntry> Entries(std::uint64_t key) const;

    /**
     * The legal moves the book holds for a position, with their weights; entries whose move is not legal there (a key
     * collision, a corrupt book) are left out.
     * @param position The position.
     * @return The moves and their weights, in the book's order.
     */
    std::vector<std::pair<Move, std::uint16_t>> Moves(const Position &position) const;

    /**
     * Choose a move for a position.
     * @param position The position.
     * @param selection How to choose.
     * @param generator The random source for kWeightedRandom (std::mt19937_64 is the same on every platform).
     * @return The move, or nothing when the book has none of weight above 0.
     */
    std::optional<Move> Choose(const Position &position, BookSelection selection, std::mt19937_64 &generator) const;

  private:
    std::vector<PolyglotEntry> entries_; ///< The entries, sorted by key.
};

} // namespace uci_host::chess
