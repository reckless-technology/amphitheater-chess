#pragma once
/// @file EngineConfig.h What the host needs to know to start an engine and set it up.
///
/// Plain data, kept free of any file format: the GUI will save it with the toolkit's JSON, the command line builds it
/// from its arguments.

#include "chess/TimeControl.h"

#include <string>
#include <utility>
#include <vector>

namespace uci_host::uci
{

/** How long the host waits for an engine before giving up on it. */
struct EngineTimeouts
{
    chess::Milliseconds startup{10000}; ///< From starting the process to its "uciok".
    chess::Milliseconds ready{10000};   ///< From "isready" to its "readyok".
    chess::Milliseconds quit{2000};     ///< From "quit" to the process exiting, before it is killed.
};

/** An engine: how to start it, and the options to give it. */
struct EngineConfig
{
    std::string              name;              ///< The name to show; empty for the name the engine gives itself.
    std::string              executable;        ///< The program, UTF-8 (a relative path is from the host's directory).
    std::string              working_directory; ///< Where it runs; empty for the host's own directory.
    std::vector<std::string> arguments;         ///< Arguments to start it with.
    std::vector<std::pair<std::string, std::string>> options; ///< UCI options to set after the handshake, in order.
    std::string notes;        ///< Free text for the operator: how the engine behaves, what it needs.
    bool does_ponder = false; ///< Whether the engine may think on the opponent's time (its Ponder option set true).
    EngineTimeouts timeouts;  ///< How long to wait for it.
};

} // namespace uci_host::uci
