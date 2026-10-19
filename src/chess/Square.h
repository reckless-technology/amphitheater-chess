#pragma once
/// @file Square.h Square indices on the board.
///
/// Adapted from pawnstar's src/square.h (commit c41099798db594ae2c379cba94d917c9c28d9c04), with Parse added: the
/// host reads squares from untrusted text and needs to tell a square from anything else.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace uci_host::chess
{

/** A square index (location) on the board, a1 = 0 to h8 = 63 (little-endian rank-file order). */
class Square
{
  public:
    /** An uninitialised square, so arrays of squares cost nothing to make. */
    constexpr Square()
    {
    }

    /** @param index The square's index in little-endian rank-file order, 0 (a1) to 63 (h8). */
    constexpr Square(int index) : value_(static_cast<std::uint8_t>(index))
    {
    }

    /**
     * @param file The file, 0 (a) to 7 (h).
     * @param rank The rank, 0 (the first) to 7 (the eighth).
     */
    constexpr Square(int file, int rank) : value_(static_cast<std::uint8_t>(file + 8 * rank))
    {
    }

    /**
     * A square from its name, for constants in code ("e1", "E1"); text from outside goes through Parse.
     * @param name Two characters, a file letter and a rank digit; anything shorter gives square 0.
     */
    constexpr Square(const char *name)
        : value_(name[0] == '\0' || name[1] == '\0'
                     ? 0
                     : static_cast<std::uint8_t>((name[0] | 0x20) - 'a' + 8 * (name[1] - '1')))
    {
    }

    /** @return The square's index, 0 to 63. */
    constexpr operator std::uint8_t() const
    {
        return value_;
    }

    /** @return The file, 0 (a) to 7 (h). */
    constexpr std::uint8_t File() const
    {
        return value_ & 7;
    }

    /** @return The rank, 0 (the first) to 7 (the eighth). */
    constexpr std::uint8_t Rank() const
    {
        return value_ >> 3;
    }

    /** @return The square's name, "e4". */
    std::string ToString() const
    {
        return std::string{static_cast<char>('a' + File()), static_cast<char>('1' + Rank())};
    }

    /**
     * Read a square's name from untrusted text.
     * @param text Exactly two characters, a lower-case file letter a-h and a rank digit 1-8.
     * @return The square, or nothing when the text is not a square's name.
     */
    static constexpr std::optional<Square> Parse(std::string_view text)
    {
        if (text.size() != 2 || text[0] < 'a' || text[0] > 'h' || text[1] < '1' || text[1] > '8')
        {
            return std::nullopt;
        }
        return Square{text[0] - 'a', text[1] - '1'};
    }

  private:
    std::uint8_t value_; ///< The square index in little-endian rank-file order.
};

} // namespace uci_host::chess
