#include "Inflate.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>

namespace amphitheater::match
{

namespace
{

constexpr int kMostBits = 15; ///< The longest code DEFLATE allows.

/** The bits of a stream, least significant first, as DEFLATE packs them. */
class BitReader
{
  public:
    /** @param bytes The stream. */
    explicit BitReader(std::span<const std::uint8_t> bytes) : bytes_(bytes)
    {
    }

    /**
     * @param count How many bits, up to 16.
     * @return The next bits, the first in the lowest place; none past the stream's end.
     */
    std::optional<int> Bits(int count)
    {
        int value = 0;
        for (int index = 0; index < count; ++index)
        {
            if (position_ >= bytes_.size())
            {
                return std::nullopt;
            }
            value |= ((bytes_[position_] >> bit_) & 1) << index;
            if (++bit_ == 8)
            {
                bit_ = 0;
                ++position_;
            }
        }
        return value;
    }

    /** The rest of the byte under way skipped: a stored block starts on a byte. */
    void AlignToByte()
    {
        if (bit_ != 0)
        {
            bit_ = 0;
            ++position_;
        }
    }

    /** @return The whole bytes not yet read, from a byte boundary. */
    std::span<const std::uint8_t> Rest() const
    {
        return position_ <= bytes_.size() ? bytes_.subspan(position_) : std::span<const std::uint8_t>();
    }

    /** @param count Whole bytes passed over. */
    void Skip(std::size_t count)
    {
        position_ += count;
    }

  private:
    std::span<const std::uint8_t> bytes_;        ///< The stream.
    std::size_t                   position_ = 0; ///< The byte under way.
    int                           bit_      = 0; ///< The next bit in it.
};

/** A canonical Huffman code, decoded a bit at a time as RFC 1951's reference decoder does. */
struct Huffman
{
    std::array<int, kMostBits + 1> counts{}; ///< How many symbols have each length.
    std::vector<int>               symbols;  ///< The symbols, by length and then by value.

    /**
     * Build the code from each symbol's length.
     * @param lengths Each symbol's code length; 0 for a symbol not used.
     * @return Whether the lengths make a code that is not over-full.
     */
    bool Build(std::span<const int> lengths)
    {
        counts.fill(0);
        for (const int length : lengths)
        {
            ++counts[static_cast<std::size_t>(length)];
        }
        int left = 1;
        for (int length = 1; length <= kMostBits; ++length)
        {
            left = left * 2 - counts[static_cast<std::size_t>(length)];
            if (left < 0)
            {
                return false;
            }
        }
        std::array<int, kMostBits + 2> offsets{};
        for (int length = 1; length <= kMostBits; ++length)
        {
            offsets[static_cast<std::size_t>(length) + 1] =
                offsets[static_cast<std::size_t>(length)] + counts[static_cast<std::size_t>(length)];
        }
        symbols.assign(lengths.size(), 0);
        for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol)
        {
            if (lengths[symbol] != 0)
            {
                symbols[static_cast<std::size_t>(offsets[static_cast<std::size_t>(lengths[symbol])]++)] =
                    static_cast<int>(symbol);
            }
        }
        return true;
    }

    /**
     * @param reader The stream.
     * @return The next symbol; none for a code it does not hold or the stream's end.
     */
    std::optional<int> Decode(BitReader &reader) const
    {
        int code  = 0;
        int first = 0;
        int index = 0;
        for (int length = 1; length <= kMostBits; ++length)
        {
            const auto bit = reader.Bits(1);
            if (!bit)
            {
                return std::nullopt;
            }
            code |= *bit;
            const int count = counts[static_cast<std::size_t>(length)];
            if (code - count < first)
            {
                return symbols[static_cast<std::size_t>(index + (code - first))];
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return std::nullopt;
    }
};

constexpr std::array<int, 29> kLengthBase    = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<int, 29> kLengthExtra   = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<int, 30> kDistanceBase  = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                                33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                                1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<int, 30> kDistanceExtra = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/** The order a dynamic block lists its code-length code's lengths in. */
constexpr std::array<int, 19> kCodeLengthOrder = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

/**
 * A Huffman block's symbols decoded into the output.
 * @return Empty where the block decoded; else what is wrong.
 */
std::string DecodeBlock(BitReader &reader, const Huffman &literals, const Huffman &distances,
                        std::vector<std::uint8_t> &out, std::size_t most_size)
{
    while (true)
    {
        const auto symbol = literals.Decode(reader);
        if (!symbol)
        {
            return "a code the block does not hold, or the stream ends inside a block";
        }
        if (*symbol < 256)
        {
            if (out.size() >= most_size)
            {
                return "it decodes to more than it may";
            }
            out.push_back(static_cast<std::uint8_t>(*symbol));
            continue;
        }
        if (*symbol == 256)
        {
            return {};
        }
        const int length_index = *symbol - 257;
        if (length_index >= static_cast<int>(kLengthBase.size()))
        {
            return "a length code out of range";
        }
        const auto length_extra   = reader.Bits(kLengthExtra[static_cast<std::size_t>(length_index)]);
        const auto distance_index = distances.Decode(reader);
        if (!length_extra || !distance_index || *distance_index >= static_cast<int>(kDistanceBase.size()))
        {
            return "a distance code out of range";
        }
        const auto distance_extra = reader.Bits(kDistanceExtra[static_cast<std::size_t>(*distance_index)]);
        if (!distance_extra)
        {
            return "the stream ends inside a block";
        }
        const std::size_t length =
            static_cast<std::size_t>(kLengthBase[static_cast<std::size_t>(length_index)] + *length_extra);
        const std::size_t distance =
            static_cast<std::size_t>(kDistanceBase[static_cast<std::size_t>(*distance_index)] + *distance_extra);
        if (distance > out.size())
        {
            return "a distance back past the start";
        }
        if (out.size() + length > most_size)
        {
            return "it decodes to more than it may";
        }
        for (std::size_t index = 0; index < length; ++index)
        {
            out.push_back(out[out.size() - distance]);
        }
    }
}

/**
 * A dynamic block's two codes read from its header.
 * @return Empty where they were read; else what is wrong.
 */
std::string ReadDynamicCodes(BitReader &reader, Huffman &literals, Huffman &distances)
{
    const auto literal_count  = reader.Bits(5);
    const auto distance_count = reader.Bits(5);
    const auto length_count   = reader.Bits(4);
    if (!literal_count || !distance_count || !length_count)
    {
        return "the stream ends in a block's header";
    }
    const int literal_total  = *literal_count + 257;
    const int distance_total = *distance_count + 1;
    if (literal_total > 286 || distance_total > 30)
    {
        return "a block's header counts too many codes";
    }
    std::array<int, 19> code_lengths{};
    for (int index = 0; index < *length_count + 4; ++index)
    {
        const auto length = reader.Bits(3);
        if (!length)
        {
            return "the stream ends in a block's header";
        }
        code_lengths[static_cast<std::size_t>(kCodeLengthOrder[static_cast<std::size_t>(index)])] = *length;
    }
    Huffman code_length_code;
    if (!code_length_code.Build(code_lengths))
    {
        return "a block's code-length code is over-full";
    }
    std::vector<int> lengths;
    while (static_cast<int>(lengths.size()) < literal_total + distance_total)
    {
        const auto symbol = code_length_code.Decode(reader);
        if (!symbol)
        {
            return "a block's code lengths do not decode";
        }
        if (*symbol < 16)
        {
            lengths.push_back(*symbol);
            continue;
        }
        int repeat = 0;
        int value  = 0;
        if (*symbol == 16)
        {
            if (lengths.empty())
            {
                return "a block repeats a code length before the first";
            }
            value            = lengths.back();
            const auto extra = reader.Bits(2);
            repeat           = extra ? 3 + *extra : -1;
        }
        else
        {
            const auto extra = reader.Bits(*symbol == 17 ? 3 : 7);
            repeat           = extra ? (*symbol == 17 ? 3 : 11) + *extra : -1;
        }
        if (repeat < 0 || static_cast<int>(lengths.size()) + repeat > literal_total + distance_total)
        {
            return "a block's code lengths run past their count";
        }
        lengths.insert(lengths.end(), static_cast<std::size_t>(repeat), value);
    }
    if (lengths[256] == 0)
    {
        return "a block has no end-of-block code";
    }
    const std::span<const int> all(lengths);
    if (!literals.Build(all.first(static_cast<std::size_t>(literal_total))) ||
        !distances.Build(all.subspan(static_cast<std::size_t>(literal_total))))
    {
        return "a block's code is over-full";
    }
    return {};
}

} // namespace

Result<std::vector<std::uint8_t>> Inflate(std::span<const std::uint8_t> compressed, std::size_t expected_size,
                                          std::size_t most_size)
{
    std::vector<std::uint8_t> out;
    out.reserve(std::min(expected_size, most_size));
    BitReader reader(compressed);
    bool      is_last = false;
    while (!is_last)
    {
        const auto header = reader.Bits(3);
        if (!header)
        {
            return Error{"the stream ends before its last block"};
        }
        is_last          = (*header & 1) != 0;
        const int   type = *header >> 1;
        std::string what = {};
        if (type == 0)
        {
            reader.AlignToByte();
            const auto rest = reader.Rest();
            if (rest.size() < 4)
            {
                return Error{"the stream ends in a stored block's header"};
            }
            const std::size_t length     = rest[0] | (rest[1] << 8);
            const std::size_t complement = rest[2] | (rest[3] << 8);
            if ((length ^ 0xffff) != complement)
            {
                return Error{"a stored block's length does not match its complement"};
            }
            if (rest.size() < 4 + length)
            {
                return Error{"the stream ends inside a stored block"};
            }
            if (out.size() + length > most_size)
            {
                return Error{"it decodes to more than it may"};
            }
            out.insert(out.end(), rest.begin() + 4, rest.begin() + 4 + static_cast<std::ptrdiff_t>(length));
            reader.Skip(4 + length);
            continue;
        }
        Huffman literals;
        Huffman distances;
        if (type == 1)
        {
            std::array<int, 288> literal_lengths{};
            for (std::size_t symbol = 0; symbol < literal_lengths.size(); ++symbol)
            {
                literal_lengths[symbol] = symbol < 144 ? 8 : symbol < 256 ? 9 : symbol < 280 ? 7 : 8;
            }
            std::array<int, 30> distance_lengths{};
            distance_lengths.fill(5);
            literals.Build(literal_lengths);
            distances.Build(distance_lengths);
        }
        else if (type == 2)
        {
            what = ReadDynamicCodes(reader, literals, distances);
        }
        else
        {
            what = "a block of the reserved type";
        }
        if (what.empty())
        {
            what = DecodeBlock(reader, literals, distances, out, most_size);
        }
        if (!what.empty())
        {
            return Error{what};
        }
    }
    return out;
}

} // namespace amphitheater::match
