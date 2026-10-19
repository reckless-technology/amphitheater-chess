// The one translation unit that reads the version the configure writes (cmake/UciHostVersion.cmake), so a new commit
// recompiles this file alone.

#include "Cli.h"
#include "UciHostVersion.h"

namespace uci_host::cli
{

std::string VersionText()
{
    return UCI_HOST_VERSION_STRING;
}

} // namespace uci_host::cli
