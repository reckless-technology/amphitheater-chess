#pragma once
/// @file Cli.h The command-line runner: matches, tournaments, perft and analysis, as one function.
///
/// amphitheater-cli's main hands its UTF-8 arguments to RunCli, which the tests call in-process too. The arguments
/// follow the conventions cutechess-cli and fastchess share (-engine, -each, -tc, -games, -openings, -resign, -draw,
/// -pgnout), so a script written for either reads here with little change:
///
///   amphitheater-cli match      -engine cmd=A [name=..] -engine cmd=B -each tc=40/60+0.6 -games 10 -concurrency 2
///   amphitheater-cli tournament -engine cmd=A -engine cmd=B -engine cmd=C -each st=0.1 -rounds 2 -double
///   amphitheater-cli perft      <fen|startpos> <depth> [-divide]
///   amphitheater-cli analyse    -engine cmd=A -fen <fen|startpos> [-moves e2e4 ...] -time 5 [-depth N] [-multipv N]
///
/// UsageText has the whole syntax.

#include "chess/Result.h"
#include "match/BookMaker.h"
#include "match/Tournament.h"
#include "uci/EngineConfig.h"
#include "version/Version.h"

#include <atomic>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace amphitheater::cli
{

/** @return The full usage text. */
std::string UsageText();

using amphitheater::VersionText; // The host's version, MAJOR.MINOR.BUILD (version/Version.h).

/** What "match" and "tournament" were asked for, read from the arguments. */
struct TournamentOptions
{
    match::TournamentSettings settings;         ///< The tournament.
    bool                      is_debug = false; ///< Whether to print every engine line.
    std::vector<std::string>  warnings;         ///< Openings skipped, and the like.
};

/**
 * Read the arguments of "match" or "tournament" (and load the opening book they name).
 * @param arguments The arguments after the subcommand.
 * @param is_match Whether it is a match (exactly two engines).
 * @return The options, or what is wrong with the arguments.
 */
Result<TournamentOptions> ParseTournamentArguments(const std::vector<std::string> &arguments, bool is_match);

/** What "analyse" was asked for. */
struct AnalyseOptions
{
    uci::EngineConfig        engine;      ///< The engine.
    std::string              fen;         ///< The position; empty for the starting position.
    std::vector<std::string> moves;       ///< Moves from it, in UCI notation.
    chess::Milliseconds      time{5000};  ///< How long to think.
    std::optional<int>       depth;       ///< A depth to stop at instead.
    int                      multipv = 1; ///< Lines to show (MultiPV, where the engine has it).
};

/**
 * Read the arguments of "analyse".
 * @param arguments The arguments after the subcommand.
 * @return The options, or what is wrong with them.
 */
Result<AnalyseOptions> ParseAnalyseArguments(const std::vector<std::string> &arguments);

/** What "epd" was asked for: a test suite run against an engine. */
struct EpdOptions
{
    uci::EngineConfig   engine;     ///< The engine.
    std::string         file;       ///< The suite's EPD file.
    chess::Milliseconds time{1000}; ///< How long it thinks on each position.
    std::optional<int>  depth;      ///< A depth to search each position to instead.
};

/**
 * Read the arguments of "epd".
 * @param arguments The arguments after the subcommand.
 * @return The options, or what is wrong with them.
 */
Result<EpdOptions> ParseEpdArguments(const std::vector<std::string> &arguments);

/**
 * Read the arguments of "makebook": an opening book made from a library of games.
 * @param arguments The arguments after the subcommand.
 * @return The settings, or what is wrong with them.
 */
Result<match::BookMakerSettings> ParseMakeBookArguments(const std::vector<std::string> &arguments);

/**
 * Run the command line.
 * @param arguments The arguments, the program's name first.
 * @param out Where results go.
 * @param err Where errors and warnings go.
 * @param stop When set (by Ctrl+C), a match stops after abandoning the games in progress; may be null.
 * @return The exit code: 0 done, 1 failed, 2 the arguments were wrong.
 */
int RunCli(const std::vector<std::string> &arguments, std::ostream &out, std::ostream &err,
           const std::atomic<bool> *stop = nullptr);

} // namespace amphitheater::cli
