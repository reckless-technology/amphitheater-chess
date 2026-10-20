#pragma once
/// @file Version.h The host's version, MAJOR.MINOR.BUILD, which the command line and the window both report.

#include <string>

namespace amphitheater
{

/** @return The host's version, MAJOR.MINOR.BUILD, from the nearest vMAJOR.MINOR tag (cmake/AmphitheaterVersion.cmake).
 */
std::string VersionText();

} // namespace amphitheater
