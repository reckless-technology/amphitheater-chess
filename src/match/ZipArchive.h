#pragma once
/// @file ZipArchive.h A zip archive's files listed from its central directory and read one at a time, stored or
/// deflated (Inflate.h) - the archives a library of games comes in. Zip64 archives, encrypted entries and other
/// compression methods are refused with a reason.

#include "chess/Result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace amphitheater::match
{

/** A file in a zip archive. */
struct ZipEntry
{
    std::string   name;                ///< Its path in the archive, as the archive spells it ('/' between folders).
    std::uint16_t method          = 0; ///< 0 stored, 8 deflated.
    std::uint64_t compressed_size = 0; ///< Its bytes in the archive.
    std::uint64_t size            = 0; ///< Its bytes once read.
    std::uint64_t header_offset   = 0; ///< Where its local header starts.
};

/** A zip archive on disk. */
class ZipArchive
{
  public:
    /**
     * Open an archive and read its central directory.
     * @param path The archive.
     * @return It, or why it could not be read.
     */
    static Result<ZipArchive> Open(const std::filesystem::path &path);

    /** @return Its files, folders left out, in the directory's order. */
    const std::vector<ZipEntry> &entries() const
    {
        return entries_;
    }

    /**
     * Read one of its files.
     * @param entry The file, one of entries().
     * @return Its bytes, or why they could not be read.
     */
    Result<std::string> Read(const ZipEntry &entry) const;

  private:
    std::filesystem::path path_;    ///< The archive.
    std::vector<ZipEntry> entries_; ///< Its files.
};

} // namespace amphitheater::match
