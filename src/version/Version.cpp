// The one translation unit that reads the version the configure writes (cmake/AmphitheaterVersion.cmake), so a new
// commit recompiles this file alone.

#include "Version.h"

#include "AmphitheaterVersion.h"

namespace amphitheater
{

std::string VersionText()
{
    return AMPHITHEATER_VERSION_STRING;
}

} // namespace amphitheater
