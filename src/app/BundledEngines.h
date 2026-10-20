#pragma once
/// @file BundledEngines.h The engines the installers carry beside the host - Jonny Reckless's five UCI engines, each a
/// separate GPLv3 program the host only talks to over UCI - and how they reach a person's engine list.
///
/// The installer lays them out under a folder named engines (in the install folder on Windows and in the portable
/// Linux archive, in the bundle's Contents/Resources on macOS, /usr/lib/reckless-amphitheater/engines from the Debian
/// package), with bundled.json beside them: each engine's id, name, version and licence, the defaults it is seeded
/// with, the options its handshake declared when it was packaged, and its builds in order of preference, each with what
/// it asks of the CPU (tools/bundled_engines.py writes it). At every start the host reads it and seeds the engine list
/// (SeedBundledEngines): an engine new to the list is added with the manifest's defaults, marked bundled; one the list
/// holds already follows the install to its new path and version with the person's own settings kept; one the person
/// removed stays removed; the person's own engines are never touched.

#include "EngineCatalogue.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace amphitheater::app
{

/** What this machine's processor can run of the levels a bundled build may ask for. */
struct CpuLevels
{
    bool is_x86_64_v2           = false; ///< x86-64-v2: SSE4.2, SSSE3, POPCNT, CMPXCHG16B and the rest of the level.
    bool is_x86_64_v3           = false; ///< x86-64-v3: AVX2, BMI1, BMI2, FMA, F16C, MOVBE, LZCNT, the OS saving AVX.
    bool is_x86_64_v3_fast_pext = false; ///< x86-64-v3 with a PEXT that is not microcoded.
};

/** @return This machine's levels: CPUID's on x86-64, none elsewhere (an arm64 build asks for nothing). */
CpuLevels DetectCpuLevels();

/**
 * Whether a build's requirement is met.
 * @param cpu The machine's levels.
 * @param requirement "" (nothing), "x86-64-v2", "x86-64-v3" or "x86-64-v3-fast-pext"; any other is never met.
 * @return Whether the build can run here.
 */
bool MeetsRequirement(const CpuLevels &cpu, const std::string &requirement);

/** One bundled engine as this machine runs it: the build chosen for its CPU, and what it is seeded with. */
struct BundledEngine
{
    std::string                                      id;          ///< Its id, the folder it lies in ("pawnstar").
    std::string                                      name;        ///< The name the list shows ("Pawnstar").
    std::string                                      version;     ///< Its release's version ("1.0.0").
    std::string                                      licence;     ///< Its licence's SPDX id ("GPL-3.0").
    std::string                                      engine_name; ///< The name its handshake gave when packaged.
    std::string                                      author;      ///< The author its handshake gave.
    std::vector<std::pair<std::string, std::string>> options;     ///< The UCI options it is seeded with, in order.
    std::vector<uci::EngineOption>                   declared;    ///< The options its handshake declared.
    std::filesystem::path                            executable;  ///< The build this machine runs.
};

/** @return The running program's own file; empty where the platform will not say. */
std::filesystem::path RunningExecutable();

/**
 * Where the bundled engines lie for a host at a place: beside it (Windows, the portable Linux archive), in its bundle's
 * Resources (macOS), or /usr/lib/reckless-amphitheater/engines for the Debian package's host in
 * /opt/reckless-technology/bin.
 * @param executable The host's own file.
 * @return The folder holding bundled.json; empty where there is none (a build tree's host carries no engines).
 */
std::filesystem::path BundledEnginesFolder(const std::filesystem::path &executable);

/**
 * The engines of a manifest this machine can run: each engine's first build whose requirement the CPU meets and whose
 * file is there; an engine with none left out.
 * @param folder The folder holding bundled.json.
 * @param cpu The machine's levels.
 * @param error Why the manifest could not be read, when it could not (may be null).
 * @return The engines, in the manifest's order; none for a manifest that cannot be read.
 */
std::vector<BundledEngine> ReadBundledEngines(const std::filesystem::path &folder, const CpuLevels &cpu,
                                              std::string *error = nullptr);

/**
 * Seed an engine list with the bundled engines, by these rules:
 *  - an engine the list marks bundled with the same id has its program moved to the bundled build's path, and on a new
 *    version its version, the name and author its handshake gave and its declared options moved to the new release's;
 *    everything a person sets - the name shown, the options, arguments, pondering, analysis, depth, book - is kept;
 *  - an engine whose id the list records as removed by the person is not added again;
 *  - an engine of the list not marked bundled whose program is the bundled build itself is marked bundled, as above;
 *  - any other bundled engine is added at the list's end with the manifest's name and options, marked bundled;
 *  - every other engine of the list - the person's own, and a bundled one this install no longer carries - is left as
 *    it is.
 * @param catalogue The list.
 * @param bundled The engines this install carries (ReadBundledEngines).
 * @return Whether the list changed, so it is to be saved.
 */
bool SeedBundledEngines(EngineCatalogue &catalogue, const std::vector<BundledEngine> &bundled);

} // namespace amphitheater::app
