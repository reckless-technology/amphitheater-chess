#include "BundledEngines.h"

#include "JsonFile.h"

#include <algorithm>
#include <cstdint>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#if defined(_M_X64) || defined(__x86_64__)
#define AMPHITHEATER_X86_64 1
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

namespace amphitheater::app
{

namespace
{

namespace json = reckless::core::json;

/** The manifest's format number. */
constexpr int kManifestFormat = 1;

/**
 * A path from UTF-8 text.
 * @param text The text.
 * @return The path.
 */
std::filesystem::path PathFromUtf8(const std::string &text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

/**
 * A path as UTF-8 text, as the engine list keeps a program.
 * @param path The path.
 * @return The text.
 */
std::string Utf8FromPath(const std::filesystem::path &path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

/**
 * Whether two paths name one file: the same text once normalised, or, both being there, the same file.
 * @param a One path.
 * @param b The other.
 * @return Whether they are one.
 */
bool SameFile(const std::filesystem::path &a, const std::filesystem::path &b)
{
    if (a.lexically_normal() == b.lexically_normal())
    {
        return true;
    }
    std::error_code ignored;
    return std::filesystem::equivalent(a, b, ignored);
}

#if defined(AMPHITHEATER_X86_64)

/** One CPUID leaf's four registers. */
struct CpuidRegisters
{
    std::uint32_t eax = 0; ///< EAX.
    std::uint32_t ebx = 0; ///< EBX.
    std::uint32_t ecx = 0; ///< ECX.
    std::uint32_t edx = 0; ///< EDX.
};

/**
 * CPUID.
 * @param leaf The leaf (EAX).
 * @param subleaf The subleaf (ECX).
 * @return The registers.
 */
CpuidRegisters Cpuid(std::uint32_t leaf, std::uint32_t subleaf)
{
    CpuidRegisters out;
#if defined(_MSC_VER)
    int registers[4] = {};
    __cpuidex(registers, static_cast<int>(leaf), static_cast<int>(subleaf));
    out.eax = static_cast<std::uint32_t>(registers[0]);
    out.ebx = static_cast<std::uint32_t>(registers[1]);
    out.ecx = static_cast<std::uint32_t>(registers[2]);
    out.edx = static_cast<std::uint32_t>(registers[3]);
#else
    __cpuid_count(leaf, subleaf, out.eax, out.ebx, out.ecx, out.edx);
#endif
    return out;
}

/** @return Whether the operating system saves the AVX registers (XCR0's SSE and AVX state bits). */
bool DoesOsSaveAvxState()
{
#if defined(_MSC_VER)
    const std::uint64_t xcr0 = _xgetbv(0);
#else
    std::uint32_t low  = 0;
    std::uint32_t high = 0;
    __asm__("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
    const std::uint64_t xcr0 = (static_cast<std::uint64_t>(high) << 32) | low;
#endif
    return (xcr0 & 0x6) == 0x6;
}

/**
 * Whether a bit of a register is set.
 * @param value The register.
 * @param bit The bit.
 * @return Whether it is.
 */
constexpr bool Bit(std::uint32_t value, int bit)
{
    return ((value >> bit) & 1u) != 0;
}

#endif

} // namespace

CpuLevels DetectCpuLevels()
{
    CpuLevels levels;
#if defined(AMPHITHEATER_X86_64)
    const CpuidRegisters vendor_leaf = Cpuid(0, 0);
    if (vendor_leaf.eax < 7)
    {
        return levels;
    }
    const CpuidRegisters basic    = Cpuid(1, 0);
    const CpuidRegisters extended = Cpuid(7, 0);
    const CpuidRegisters amd      = Cpuid(0x80000001, 0);
    // x86-64-v2: CMPXCHG16B, SSE3, SSSE3, SSE4.1, SSE4.2, POPCNT (and LAHF-SAHF in 64-bit mode).
    levels.is_x86_64_v2 = Bit(basic.ecx, 0) && Bit(basic.ecx, 9) && Bit(basic.ecx, 13) && Bit(basic.ecx, 19) &&
                          Bit(basic.ecx, 20) && Bit(basic.ecx, 23) && Bit(amd.ecx, 0);
    // x86-64-v3: AVX with the OS saving its state, F16C, FMA, MOVBE, AVX2, BMI1, BMI2 and LZCNT.
    const bool is_v3 = levels.is_x86_64_v2 && Bit(basic.ecx, 12) && Bit(basic.ecx, 22) && Bit(basic.ecx, 27) &&
                       Bit(basic.ecx, 28) && Bit(basic.ecx, 29) && Bit(extended.ebx, 3) && Bit(extended.ebx, 5) &&
                       Bit(extended.ebx, 8) && Bit(amd.ecx, 5) && DoesOsSaveAvxState();
    // AMD's family 17h (Zen 1 and 2) has PEXT, microcoded at hundreds of cycles: a PEXT build runs slower there than
    // the portable one. The family is the base family plus the extended family when the base is 0xF.
    const bool          is_amd    = vendor_leaf.ebx == 0x68747541 && vendor_leaf.edx == 0x69746E65 &&
                                    vendor_leaf.ecx == 0x444D4163; // "AuthenticAMD"
    const std::uint32_t base      = (basic.eax >> 8) & 0xF;
    const std::uint32_t family    = base == 0xF ? base + ((basic.eax >> 20) & 0xFF) : base;
    levels.is_x86_64_v3           = is_v3;
    levels.is_x86_64_v3_fast_pext = is_v3 && !(is_amd && family == 0x17);
#endif
    return levels;
}

bool MeetsRequirement(const CpuLevels &cpu, const std::string &requirement)
{
    if (requirement.empty())
    {
        return true;
    }
    if (requirement == "x86-64-v2")
    {
        return cpu.is_x86_64_v2;
    }
    if (requirement == "x86-64-v3")
    {
        return cpu.is_x86_64_v3;
    }
    if (requirement == "x86-64-v3-fast-pext")
    {
        return cpu.is_x86_64_v3_fast_pext;
    }
    return false;
}

std::filesystem::path RunningExecutable()
{
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    const DWORD  length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
    {
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path(buffer);
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
    {
        return {};
    }
    buffer.resize(std::char_traits<char>::length(buffer.c_str()));
    std::error_code             ignored;
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(buffer, ignored);
    return resolved.empty() ? std::filesystem::path(buffer) : resolved;
#else
    std::error_code             error;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path() : self;
#endif
}

std::filesystem::path BundledEnginesFolder(const std::filesystem::path &executable)
{
    if (executable.empty())
    {
        return {};
    }
    const std::filesystem::path        folder = executable.parent_path();
    std::vector<std::filesystem::path> candidates;
#if defined(__APPLE__)
    candidates.push_back(folder.parent_path() / "Resources" / "engines"); // .app/Contents/MacOS -> Resources
#endif
    candidates.push_back(folder / "engines");
#if !defined(_WIN32) && !defined(__APPLE__)
    if (folder == std::filesystem::path("/opt/reckless-technology/bin"))
    {
        candidates.push_back("/usr/lib/reckless-amphitheater/engines");
    }
#endif
    for (const std::filesystem::path &candidate : candidates)
    {
        std::error_code ignored;
        if (std::filesystem::is_regular_file(candidate / "bundled.json", ignored))
        {
            return candidate;
        }
    }
    return {};
}

std::vector<BundledEngine> ReadBundledEngines(const std::filesystem::path &folder, const CpuLevels &cpu,
                                              std::string *error)
{
    std::vector<BundledEngine> engines;
    std::string                why;
    const auto                 document = reckless::core::ReadJsonFile(folder / "bundled.json", &why);
    if (!document || !document->IsObject() || document->IntOr("format", 0) != kManifestFormat)
    {
        if (error != nullptr)
        {
            *error = Utf8FromPath(folder / "bundled.json") + ": " +
                     (document ? "not a manifest of format " + std::to_string(kManifestFormat) : why);
        }
        return engines;
    }
    if (const json::Array *list = (*document)["engines"].AsArray())
    {
        for (const json::Value &object : *list)
        {
            BundledEngine engine;
            engine.id      = object.StringOr("id", "");
            engine.name    = object.StringOr("name", "");
            engine.version = object.StringOr("version", "");
            engine.licence = object.StringOr("licence", "");
            if (engine.id.empty() || engine.version.empty())
            {
                continue;
            }
            engine.engine_name = object.StringOr("engine_name", "");
            engine.author      = object.StringOr("author", "");
            engine.declared    = DeclaredOptionsFromJson(object["declared"]);
            if (const json::Array *options = object["options"].AsArray())
            {
                for (const json::Value &option : *options)
                {
                    const std::string name = option.StringOr("name", "");
                    if (!name.empty())
                    {
                        engine.options.emplace_back(name, option.StringOr("value", ""));
                    }
                }
            }
            if (const json::Array *builds = object["builds"].AsArray())
            {
                for (const json::Value &build : *builds)
                {
                    const std::string relative = build.StringOr("executable", "");
                    if (relative.empty() || !MeetsRequirement(cpu, build.StringOr("requires", "")))
                    {
                        continue;
                    }
                    const std::filesystem::path executable = (folder / PathFromUtf8(relative)).lexically_normal();
                    std::error_code             ignored;
                    if (std::filesystem::is_regular_file(executable, ignored))
                    {
                        engine.executable = executable;
                        break;
                    }
                }
            }
            if (!engine.executable.empty())
            {
                engines.push_back(std::move(engine));
            }
        }
    }
    return engines;
}

bool SeedBundledEngines(EngineCatalogue &catalogue, const std::vector<BundledEngine> &bundled)
{
    bool is_changed = false;
    for (const BundledEngine &engine : bundled)
    {
        const std::string executable = Utf8FromPath(engine.executable);
        auto found = std::find_if(catalogue.engines.begin(), catalogue.engines.end(),
                                  [&engine](const EngineEntry &entry) { return entry.bundled_id == engine.id; });
        if (found == catalogue.engines.end())
        {
            if (std::find(catalogue.removed_bundled.begin(), catalogue.removed_bundled.end(), engine.id) !=
                catalogue.removed_bundled.end())
            {
                continue; // the person removed it: it stays removed
            }
            // A person's own entry for the very program: taken as the bundled engine rather than listed twice.
            found = std::find_if(catalogue.engines.begin(), catalogue.engines.end(), [&engine](const EngineEntry &e) {
                return !e.IsBundled() && SameFile(PathFromUtf8(e.config.executable), engine.executable);
            });
            if (found != catalogue.engines.end())
            {
                found->bundled_id = engine.id;
                is_changed        = true;
            }
        }
        if (found == catalogue.engines.end())
        {
            EngineEntry added;
            added.config.name       = engine.name;
            added.config.executable = executable;
            added.config.options    = engine.options;
            added.engine_name       = engine.engine_name;
            added.author            = engine.author;
            added.declared          = engine.declared;
            added.bundled_id        = engine.id;
            added.bundled_version   = engine.version;
            catalogue.engines.push_back(std::move(added));
            is_changed = true;
            continue;
        }
        EngineEntry &entry = *found;
        if (entry.config.executable != executable)
        {
            entry.config.executable = executable;
            is_changed              = true;
        }
        if (entry.bundled_version != engine.version)
        {
            // A new release: its facts follow it - its version, the name and author it gives and the options it
            // declares - and every setting of the person's stays.
            entry.bundled_version = engine.version;
            entry.engine_name     = engine.engine_name;
            entry.author          = engine.author;
            if (!engine.declared.empty())
            {
                entry.declared = engine.declared;
            }
            is_changed = true;
        }
    }
    return is_changed;
}

} // namespace amphitheater::app
