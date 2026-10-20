#pragma once
/// @file Inflate.h A raw DEFLATE stream decoded (RFC 1951): stored, fixed-Huffman and dynamic-Huffman blocks - what a
/// zip archive's deflated entries hold.

#include "chess/Result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace amphitheater::match
{

/**
 * Decode a raw DEFLATE stream (no zlib or gzip header).
 * @param compressed The stream.
 * @param expected_size The decoded size where it is known (a zip entry's), to reserve; 0 where it is not.
 * @param most_size The most the decoded bytes may come to: a stream that would decode to more is refused.
 * @return The decoded bytes, or why the stream could not be decoded.
 */
Result<std::vector<std::uint8_t>> Inflate(std::span<const std::uint8_t> compressed, std::size_t expected_size = 0,
                                          std::size_t most_size = std::size_t{1} << 32);

} // namespace amphitheater::match
