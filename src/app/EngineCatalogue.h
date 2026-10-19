#pragma once
/// @file EngineCatalogue.h The engines a person has configured: each one's program, its UCI options, its own opening
/// book, a depth limit and pondering, with the options it declared at its last handshake - saved as JSON in the user's
/// settings folder, reckless-technology/uci-host/engines.json.

#include "uci/EngineConfig.h"
#include "uci/UciProtocol.h"

#include "Json.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace uci_host::app
{

/** One configured engine. */
struct EngineEntry
{
    uci::EngineConfig              config;         ///< How to start it, its options and whether it ponders.
    std::string                    author;         ///< The author its handshake gave.
    std::string                    engine_name;    ///< The name its handshake gave (the config's name may override it).
    std::vector<uci::EngineOption> declared;       ///< The options it declared at its last handshake.
    std::string                    book_path;      ///< Its own Polyglot book; empty for none.
    int                            book_depth = 0; ///< Book moves only before this ply; 0 for no limit.
    std::optional<int>             depth;          ///< A depth limit for its searches in games; none for none.
    bool                           is_analysing = true; ///< Whether it is among the engines Analyse runs.

    /** @return The name to show: the configured one, else the handshake's, else the program's file name. */
    std::string DisplayName() const;

    /**
     * An option's value as configured, else as declared by default.
     * @param name The option's name (UCI's names compare without regard to case).
     * @return The value; empty for an option neither configured nor declared.
     */
    std::string OptionValue(const std::string &name) const;

    /**
     * Configure an option, replacing its value where it is configured already.
     * @param name The option's name.
     * @param value Its value.
     */
    void SetOption(const std::string &name, const std::string &value);

    /**
     * Configure an option no longer: it is left at the engine's default.
     * @param name The option's name.
     */
    void ClearOption(const std::string &name);

    /**
     * Whether an option is configured.
     * @param name The option's name.
     * @return Whether it is.
     */
    bool HasOption(const std::string &name) const;

    /** @return A line of what is set: "Threads 1, Hash 64 MB, ponder, book". */
    std::string Summary() const;
};

/** The configured engines, and the file they are kept in. */
class EngineCatalogue
{
  public:
    /** @return reckless-technology/uci-host/engines.json in the user's settings folder; empty where there is none. */
    static std::filesystem::path DefaultFile();

    /**
     * The catalogue as JSON.
     * @return The document.
     */
    reckless::core::json::Value ToJson() const;

    /**
     * A catalogue from JSON; an engine with no executable is skipped, a field of the wrong kind left at its default.
     * @param document The document.
     * @param error Why it is not a catalogue, when it is not one (may be null).
     * @return The catalogue, or nothing.
     */
    static std::optional<EngineCatalogue> FromJson(const reckless::core::json::Value &document,
                                                   std::string                       *error = nullptr);

    /**
     * Read a file; a missing file is an empty catalogue.
     * @param file The file.
     * @param error Why the file could not be read, when it could not (may be null).
     * @return The catalogue, or nothing when the file is there but unreadable.
     */
    static std::optional<EngineCatalogue> Load(const std::filesystem::path &file, std::string *error = nullptr);

    /**
     * Write the catalogue to a file, whole, its folder made first.
     * @param file The file.
     * @return Whether it was written.
     */
    bool Save(const std::filesystem::path &file) const;

    std::vector<EngineEntry> engines; ///< The engines, in the order a person sees them.
};

} // namespace uci_host::app
