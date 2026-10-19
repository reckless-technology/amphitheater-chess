#include "PolyglotBook.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace uci_host::chess
{

namespace
{

/** Where each part of the table starts. */
constexpr int kCastleOffset    = 768;
constexpr int kEnPassantOffset = 772;
constexpr int kTurnOffset      = 780;

/** The bytes of one entry. */
constexpr std::size_t kEntrySize = 16;

/**
 * Read a big-endian unsigned integer.
 * @param bytes The bytes.
 * @param offset Where it starts.
 * @param size How many bytes it has.
 * @return The value.
 */
std::uint64_t ReadBigEndian(const std::vector<std::uint8_t> &bytes, std::size_t offset, std::size_t size)
{
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < size; ++index)
    {
        value = (value << 8) | bytes[offset + index];
    }
    return value;
}

/**
 * Append a big-endian unsigned integer.
 * @param bytes The bytes, appended to.
 * @param value The value.
 * @param size How many bytes to write.
 */
void WriteBigEndian(std::vector<std::uint8_t> &bytes, std::uint64_t value, std::size_t size)
{
    for (std::size_t index = size; index-- > 0;)
    {
        bytes.push_back(static_cast<std::uint8_t>(value >> (8 * index)));
    }
}

} // namespace

std::uint64_t PolyglotKey(const Position &position)
{
    std::uint64_t key = 0;
    for (int index = 0; index < 64; ++index)
    {
        const Square square{index};
        const Piece  piece = position.PieceOn(square);
        if (piece == kNone)
        {
            continue;
        }
        // Black pawn 0, white pawn 1, black knight 2 ... white king 11.
        const int kind = (piece - kPawn) * 2 + (position.ColorOn(square) == kWhite ? 1 : 0);
        key ^= kPolyglotRandom64[64 * kind + 8 * square.Rank() + square.File()];
    }
    const CastlingRights rights   = position.Castling();
    const bool           flags[4] = {rights.MayWhiteCastleKingside(), rights.MayWhiteCastleQueenside(),
                                     rights.MayBlackCastleKingside(), rights.MayBlackCastleQueenside()};
    for (int right = 0; right < 4; ++right)
    {
        if (flags[right])
        {
            key ^= kPolyglotRandom64[kCastleOffset + right];
        }
    }
    // The en passant file counts when a pawn of the side to move stands beside the pawn that made the double step.
    if (const auto target = position.EnPassantSquare())
    {
        const Color mover         = position.SideToMove();
        const int   pushed_rank   = mover == kWhite ? 4 : 3;
        const int   file          = target->File();
        bool        is_capturable = false;
        for (const int beside : {file - 1, file + 1})
        {
            if (beside < 0 || beside > 7)
            {
                continue;
            }
            const Square square{beside, pushed_rank};
            is_capturable |= position.PieceOn(square) == kPawn && position.ColorOn(square) == mover;
        }
        if (is_capturable)
        {
            key ^= kPolyglotRandom64[kEnPassantOffset + file];
        }
    }
    if (position.SideToMove() == kWhite)
    {
        key ^= kPolyglotRandom64[kTurnOffset];
    }
    return key;
}

Result<PolyglotBook> PolyglotBook::FromBytes(const std::vector<std::uint8_t> &bytes)
{
    if (bytes.size() % kEntrySize != 0)
    {
        return Error{"a Polyglot book is a whole number of 16-byte entries, not " + std::to_string(bytes.size()) +
                     " bytes"};
    }
    PolyglotBook book;
    book.entries_.reserve(bytes.size() / kEntrySize);
    for (std::size_t offset = 0; offset < bytes.size(); offset += kEntrySize)
    {
        PolyglotEntry entry;
        entry.key    = ReadBigEndian(bytes, offset, 8);
        entry.move   = static_cast<std::uint16_t>(ReadBigEndian(bytes, offset + 8, 2));
        entry.weight = static_cast<std::uint16_t>(ReadBigEndian(bytes, offset + 10, 2));
        entry.learn  = static_cast<std::uint32_t>(ReadBigEndian(bytes, offset + 12, 4));
        if (!book.entries_.empty() && entry.key < book.entries_.back().key)
        {
            return Error{"a Polyglot book's entries are sorted by key; entry " + std::to_string(offset / kEntrySize) +
                         " is out of order"};
        }
        book.entries_.push_back(entry);
    }
    return book;
}

Result<PolyglotBook> PolyglotBook::Load(const std::string &path)
{
    std::ifstream file(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!file)
    {
        return Error{"cannot open '" + path + "'"};
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto                            book = FromBytes(bytes);
    if (!book)
    {
        return Error{"'" + path + "': " + book.ErrorMessage()};
    }
    return book;
}

std::vector<std::uint8_t> PolyglotBook::Encode(std::vector<PolyglotEntry> entries)
{
    std::stable_sort(entries.begin(), entries.end(),
                     [](const PolyglotEntry &left, const PolyglotEntry &right) { return left.key < right.key; });
    std::vector<std::uint8_t> bytes;
    bytes.reserve(entries.size() * kEntrySize);
    for (const auto &entry : entries)
    {
        WriteBigEndian(bytes, entry.key, 8);
        WriteBigEndian(bytes, entry.move, 2);
        WriteBigEndian(bytes, entry.weight, 2);
        WriteBigEndian(bytes, entry.learn, 4);
    }
    return bytes;
}

std::uint16_t PolyglotBook::EncodeMove(Move move)
{
    Square to = move.To();
    if (move.MoveType() == Move::kCastling)
    {
        to = Square{to.File() == 6 ? 7 : 0, to.Rank()}; // The king takes its own rook.
    }
    int promotion = 0;
    switch (move.PromotedPiece())
    {
    case kKnight:
        promotion = 1;
        break;
    case kBishop:
        promotion = 2;
        break;
    case kRook:
        promotion = 3;
        break;
    case kQueen:
        promotion = 4;
        break;
    default:
        break;
    }
    return static_cast<std::uint16_t>(to.File() | (to.Rank() << 3) | (move.From().File() << 6) |
                                      (move.From().Rank() << 9) | (promotion << 12));
}

std::vector<PolyglotEntry> PolyglotBook::Entries(std::uint64_t key) const
{
    const auto first =
        std::lower_bound(entries_.begin(), entries_.end(), key,
                         [](const PolyglotEntry &entry, std::uint64_t value) { return entry.key < value; });
    std::vector<PolyglotEntry> found;
    for (auto entry = first; entry != entries_.end() && entry->key == key; ++entry)
    {
        found.push_back(*entry);
    }
    return found;
}

std::vector<std::pair<Move, std::uint16_t>> PolyglotBook::Moves(const Position &position) const
{
    std::vector<std::pair<Move, std::uint16_t>> moves;
    const MoveList                              legal = position.GenerateLegalMoves();
    for (const auto &entry : Entries(PolyglotKey(position)))
    {
        for (const Move &move : legal)
        {
            if (EncodeMove(move) == entry.move)
            {
                moves.emplace_back(move, entry.weight);
                break;
            }
        }
    }
    return moves;
}

std::optional<Move> PolyglotBook::Choose(const Position &position, BookSelection selection,
                                         std::mt19937_64 &generator) const
{
    const auto    moves = Moves(position);
    std::uint64_t total = 0;
    for (const auto &[move, weight] : moves)
    {
        total += weight;
    }
    if (total == 0)
    {
        return std::nullopt;
    }
    if (selection == BookSelection::kBestWeight)
    {
        const auto best = std::max_element(
            moves.begin(), moves.end(), [](const auto &left, const auto &right) { return left.second < right.second; });
        return best->first;
    }
    std::uint64_t pick = generator() % total;
    for (const auto &[move, weight] : moves)
    {
        if (pick < weight)
        {
            return move;
        }
        pick -= weight;
    }
    return moves.back().first;
}

} // namespace uci_host::chess
