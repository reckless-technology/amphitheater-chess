#include "ZipArchive.h"

#include "Inflate.h"

#include <algorithm>
#include <fstream>

namespace amphitheater::match
{

namespace
{

constexpr std::uint32_t kEndOfDirectory = 0x06054b50; ///< The end of central directory record's signature.
constexpr std::uint32_t kDirectoryEntry = 0x02014b50; ///< A central directory entry's.
constexpr std::uint32_t kLocalHeader    = 0x04034b50; ///< A local file header's.
constexpr std::size_t   kEndRecordSize  = 22;         ///< The end record without its comment.

/**
 * @param bytes Bytes.
 * @param at Where the value starts.
 * @param count Its bytes, little-endian.
 * @return The value.
 */
std::uint64_t Little(const std::string &bytes, std::size_t at, int count)
{
    std::uint64_t value = 0;
    for (int index = count - 1; index >= 0; --index)
    {
        value = (value << 8) | static_cast<unsigned char>(bytes[at + static_cast<std::size_t>(index)]);
    }
    return value;
}

/**
 * Read bytes of a file at a place.
 * @return The bytes; fewer where the file ends first.
 */
std::string ReadAt(std::ifstream &file, std::uint64_t offset, std::size_t count)
{
    std::string bytes(count, '\0');
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset));
    file.read(bytes.data(), static_cast<std::streamsize>(count));
    bytes.resize(static_cast<std::size_t>(std::max<std::streamsize>(0, file.gcount())));
    return bytes;
}

/**
 * @param path A path.
 * @return It in UTF-8, as the messages give it.
 */
std::string Utf8(const std::filesystem::path &path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

} // namespace

Result<ZipArchive> ZipArchive::Open(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return Error{"cannot open '" + Utf8(path) + "'"};
    }
    file.seekg(0, std::ios::end);
    const std::uint64_t size = static_cast<std::uint64_t>(file.tellg());
    // The end record is the last thing in the archive, before a comment of at most 65,535 bytes.
    const std::uint64_t tail_size = std::min<std::uint64_t>(size, kEndRecordSize + 65535);
    const std::string   tail      = ReadAt(file, size - tail_size, static_cast<std::size_t>(tail_size));
    std::size_t         end       = std::string::npos;
    for (std::size_t at = tail.size() >= kEndRecordSize ? tail.size() - kEndRecordSize + 1 : 0; at-- > 0;)
    {
        if (Little(tail, at, 4) == kEndOfDirectory)
        {
            end = at;
            break;
        }
    }
    if (end == std::string::npos)
    {
        return Error{"'" + Utf8(path) + "' is not a zip archive"};
    }
    const std::uint64_t count            = Little(tail, end + 10, 2);
    const std::uint64_t directory_size   = Little(tail, end + 12, 4);
    const std::uint64_t directory_offset = Little(tail, end + 16, 4);
    if (count == 0xffff || directory_size == 0xffffffff || directory_offset == 0xffffffff)
    {
        return Error{"'" + Utf8(path) + "' is a Zip64 archive, which is not read"};
    }
    if (directory_offset + directory_size > size)
    {
        return Error{"'" + Utf8(path) + "' has a central directory past its end"};
    }
    const std::string directory = ReadAt(file, directory_offset, static_cast<std::size_t>(directory_size));
    ZipArchive        archive;
    archive.path_  = path;
    std::size_t at = 0;
    for (std::uint64_t index = 0; index < count; ++index)
    {
        if (at + 46 > directory.size() || Little(directory, at, 4) != kDirectoryEntry)
        {
            return Error{"'" + Utf8(path) + "' has a central directory that cannot be read"};
        }
        ZipEntry            entry;
        const std::uint64_t flags        = Little(directory, at + 8, 2);
        entry.method                     = static_cast<std::uint16_t>(Little(directory, at + 10, 2));
        entry.compressed_size            = Little(directory, at + 20, 4);
        entry.size                       = Little(directory, at + 24, 4);
        const std::size_t name_length    = static_cast<std::size_t>(Little(directory, at + 28, 2));
        const std::size_t extra_length   = static_cast<std::size_t>(Little(directory, at + 30, 2));
        const std::size_t comment_length = static_cast<std::size_t>(Little(directory, at + 32, 2));
        entry.header_offset              = Little(directory, at + 42, 4);
        if (at + 46 + name_length > directory.size())
        {
            return Error{"'" + Utf8(path) + "' has a central directory that cannot be read"};
        }
        entry.name = directory.substr(at + 46, name_length);
        at += 46 + name_length + extra_length + comment_length;
        if (entry.compressed_size == 0xffffffff || entry.size == 0xffffffff || entry.header_offset == 0xffffffff)
        {
            return Error{"'" + Utf8(path) + "' is a Zip64 archive, which is not read"};
        }
        if ((flags & 1) != 0)
        {
            entry.method = 0xffff; // encrypted: refused when it is read
        }
        if (!entry.name.empty() && entry.name.back() != '/')
        {
            archive.entries_.push_back(std::move(entry));
        }
    }
    return archive;
}

Result<std::string> ZipArchive::Read(const ZipEntry &entry) const
{
    const std::string where = "'" + entry.name + "' in '" + Utf8(path_) + "'";
    if (entry.method != 0 && entry.method != 8)
    {
        return Error{where + (entry.method == 0xffff ? " is encrypted"
                                                     : " is compressed by method " + std::to_string(entry.method) +
                                                           ", which is not read (only stored and deflated are)")};
    }
    std::ifstream     file(path_, std::ios::binary);
    const std::string header = ReadAt(file, entry.header_offset, 30);
    if (header.size() < 30 || Little(header, 0, 4) != kLocalHeader)
    {
        return Error{where + " has no local header where the directory says"};
    }
    const std::uint64_t data_offset = entry.header_offset + 30 + Little(header, 26, 2) + Little(header, 28, 2);
    const std::string   data        = ReadAt(file, data_offset, static_cast<std::size_t>(entry.compressed_size));
    if (data.size() != entry.compressed_size)
    {
        return Error{where + " runs past the archive's end"};
    }
    if (entry.method == 0)
    {
        return data;
    }
    const auto bytes = Inflate({reinterpret_cast<const std::uint8_t *>(data.data()), data.size()},
                               static_cast<std::size_t>(entry.size), static_cast<std::size_t>(entry.size));
    if (!bytes)
    {
        return Error{where + " cannot be decompressed: " + bytes.ErrorMessage()};
    }
    if (bytes->size() != entry.size)
    {
        return Error{where + " decompresses to a size other than the directory's"};
    }
    return std::string(bytes->begin(), bytes->end());
}

} // namespace amphitheater::match
