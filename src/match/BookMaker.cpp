#include "BookMaker.h"

#include "Openings.h"
#include "ZipArchive.h"

#include "chess/PgnBook.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>

namespace amphitheater::match
{

namespace
{

/** The text the reader takes at once from a file of games: about this many bytes, cut where a game starts. */
constexpr std::size_t kStretch = 8 << 20;

/**
 * @param path A path.
 * @return It in UTF-8.
 */
std::string Utf8(const std::filesystem::path &path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

/**
 * @param text A UTF-8 path.
 * @return The path.
 */
std::filesystem::path PathOf(const std::string &text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

/**
 * @param name A file's name.
 * @param extension An extension in lower case, ".pgn".
 * @return Whether the name ends with it, in any case.
 */
bool HasExtension(std::string_view name, std::string_view extension)
{
    if (name.size() < extension.size())
    {
        return false;
    }
    const std::string_view end = name.substr(name.size() - extension.size());
    return std::equal(end.begin(), end.end(), extension.begin(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == static_cast<unsigned char>(b);
    });
}

/**
 * An archive's PGN files added to the sources.
 * @return Why the archive could not be read; empty where it was.
 */
std::string AddArchive(const std::filesystem::path &file, std::vector<GameSource> &sources)
{
    const auto archive = ZipArchive::Open(file);
    if (!archive)
    {
        return archive.ErrorMessage();
    }
    for (const ZipEntry &entry : archive->entries())
    {
        if (HasExtension(entry.name, ".pgn"))
        {
            sources.push_back({file, entry.name, Utf8(file) + ": " + entry.name});
        }
    }
    return {};
}

/**
 * @param source A file of games.
 * @return Its text, or why it could not be read.
 */
Result<std::string> ReadSource(const GameSource &source)
{
    if (!source.entry)
    {
        return ReadTextFile(Utf8(source.file));
    }
    const auto archive = ZipArchive::Open(source.file);
    if (!archive)
    {
        return Error{archive.ErrorMessage()};
    }
    for (const ZipEntry &entry : archive->entries())
    {
        if (entry.name == *source.entry)
        {
            return archive->Read(entry);
        }
    }
    return Error{"'" + *source.entry + "' is no longer in '" + Utf8(source.file) + "'"};
}

/**
 * Where the next stretch of a text of games ends: about kStretch bytes on, at the start of the next game's tags.
 * @param text The text.
 * @param from Where the stretch starts.
 * @return Where it ends: a game's first tag, or the text's end.
 */
std::size_t StretchEnd(std::string_view text, std::size_t from)
{
    if (text.size() - from <= kStretch)
    {
        return text.size();
    }
    const std::size_t next = text.find("\n[Event ", from + kStretch);
    return next == std::string_view::npos ? text.size() : next + 1;
}

} // namespace

Result<std::vector<GameSource>> FindGameSources(const std::string &path, std::vector<std::string> *problems)
{
    const std::filesystem::path root = PathOf(path);
    std::error_code             error;
    std::vector<GameSource>     sources;
    if (std::filesystem::is_directory(root, error))
    {
        std::vector<std::filesystem::path> files;
        for (auto walk = std::filesystem::recursive_directory_iterator(
                 root, std::filesystem::directory_options::skip_permission_denied, error);
             !error && walk != std::filesystem::recursive_directory_iterator(); walk.increment(error))
        {
            if (walk->is_regular_file(error))
            {
                files.push_back(walk->path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const auto &file : files)
        {
            const std::string name = Utf8(file.filename());
            if (HasExtension(name, ".pgn"))
            {
                sources.push_back({file, std::nullopt, Utf8(file)});
            }
            else if (HasExtension(name, ".zip"))
            {
                if (const std::string why = AddArchive(file, sources); !why.empty() && problems != nullptr)
                {
                    problems->push_back(why);
                }
            }
        }
    }
    else if (!std::filesystem::is_regular_file(root, error))
    {
        return Error{"'" + path + "' is neither a file nor a folder"};
    }
    else if (HasExtension(path, ".zip"))
    {
        if (const std::string why = AddArchive(root, sources); !why.empty())
        {
            return Error{why};
        }
    }
    else
    {
        sources.push_back({root, std::nullopt, path});
    }
    if (sources.empty())
    {
        return Error{"'" + path + "' holds no PGN file"};
    }
    return sources;
}

Result<BookMakerSummary> MakeBook(const BookMakerSettings                              &settings,
                                  const std::function<bool(const BookMakerProgress &)> &progress)
{
    if (settings.moves_per_side < 1 || settings.least_games < 1)
    {
        return Error{"a book takes at least one move of each side, from at least one game"};
    }
    if (settings.out.empty())
    {
        return Error{"no file to write the book to"};
    }
    BookMakerSummary summary;
    const auto       sources = FindGameSources(settings.games, &summary.problems);
    if (!sources)
    {
        return Error{sources.ErrorMessage()};
    }
    chess::PgnBookBuilder builder(2 * settings.moves_per_side);
    BookMakerProgress     state;
    state.sources_total = sources->size();
    for (const GameSource &source : *sources)
    {
        state.source = source.name;
        if (progress && !progress(state))
        {
            return Error{"stopped"};
        }
        const auto text = ReadSource(source);
        if (!text)
        {
            summary.problems.push_back(text.ErrorMessage());
            ++state.sources_done;
            continue;
        }
        std::size_t unread = 0;
        for (std::size_t from = 0; from < text->size();)
        {
            const std::size_t        end = StretchEnd(*text, from);
            std::vector<std::string> errors;
            builder.Add(std::string_view(*text).substr(from, end - from), &errors);
            unread += errors.size();
            from        = end;
            state.games = builder.games();
            if (progress && !progress(state))
            {
                return Error{"stopped"};
            }
        }
        if (unread > 0)
        {
            summary.problems.push_back(source.name + ": " + std::to_string(unread) + " games could not be read");
        }
        ++state.sources_done;
    }
    summary.sources = sources->size();
    summary.games   = builder.games();
    if (summary.games == 0)
    {
        return Error{"no game could be read from '" + settings.games + "'"};
    }
    std::vector<chess::PolyglotEntry> entries = builder.Entries(settings.least_games);
    if (entries.empty())
    {
        return Error{"no move is played in " + std::to_string(settings.least_games) + " games or more"};
    }
    summary.moves = entries.size();
    std::set<std::uint64_t> positions;
    for (const auto &entry : entries)
    {
        positions.insert(entry.key);
    }
    summary.positions                     = positions.size();
    const std::vector<std::uint8_t> bytes = chess::PolyglotBook::Encode(std::move(entries));
    std::ofstream                   file(PathOf(settings.out), std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file)
    {
        return Error{"the book could not be written to '" + settings.out + "'"};
    }
    return summary;
}

} // namespace amphitheater::match
