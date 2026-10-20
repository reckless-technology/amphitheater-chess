#pragma once
/// @file BookMaker.h An opening book made from a library of games: every game of a PGN file, of the PGN files of a
/// folder and the folders under it, or of the PGN files in a zip archive (a folder's archives too), its first moves of
/// each side taken as book moves - each weighted by the number of games that play it in its position - and written as a
/// Polyglot .bin. The window's File > Make book from games and amphitheater-cli's makebook both run it.

#include "chess/Result.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace amphitheater::match
{

/** What a book is made from and how. */
struct BookMakerSettings
{
    std::string games;              ///< A .pgn file, a .zip archive or a folder of them, UTF-8.
    int         moves_per_side = 7; ///< The moves of each side each game gives the book: 7 is 14 plies.
    int         least_games    = 1; ///< The fewest games a move must be played in to be kept.
    std::string out;                ///< The .bin to write, UTF-8.
};

/** A file of games in a library: a PGN file, or a PGN file inside a zip archive. */
struct GameSource
{
    std::filesystem::path      file;  ///< The file on disk: the PGN, or the archive.
    std::optional<std::string> entry; ///< The PGN's name in the archive; none for a PGN on disk.
    std::string                name;  ///< How it is named to a person: its path, "archive.zip: games.pgn" in one.
};

/** How far a book's making has gone. */
struct BookMakerProgress
{
    std::size_t sources_done  = 0; ///< The files of games read.
    std::size_t sources_total = 0; ///< The files of games there are.
    long long   games         = 0; ///< The games read so far.
    std::string source;            ///< The file being read.
};

/** A book made. */
struct BookMakerSummary
{
    std::size_t              sources   = 0; ///< The files of games read.
    long long                games     = 0; ///< The games read.
    std::size_t              moves     = 0; ///< The book's moves, each in its position.
    std::size_t              positions = 0; ///< The positions it has a move for.
    std::vector<std::string> problems;      ///< Files that could not be read, and games that could not, by file.
};

/**
 * The files of games in a library.
 * @param path A .pgn file, a .zip archive, or a folder: its .pgn files and .zip archives and those of the folders
 *             under it, in the order of their paths.
 * @param problems Archives that could not be read, added to (may be null).
 * @return The files, or why there are none.
 */
Result<std::vector<GameSource>> FindGameSources(const std::string &path, std::vector<std::string> *problems = nullptr);

/**
 * Make a book from a library of games and write it.
 * @param settings What from, how and where to.
 * @param progress Told after each stretch of games; returning false stops the making, nothing written.
 * @return What was made, or why nothing was: no games, no move kept, the file not written, or stopped.
 */
Result<BookMakerSummary> MakeBook(const BookMakerSettings                              &settings,
                                  const std::function<bool(const BookMakerProgress &)> &progress = {});

} // namespace amphitheater::match
