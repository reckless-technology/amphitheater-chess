#pragma once
/// @file Version.h The host's version, MAJOR.MINOR.BUILD, which the command line and the window both report.

#include <string>

namespace uci_host
{

/** @return The host's version, MAJOR.MINOR.BUILD, from the nearest vMAJOR.MINOR tag (cmake/UciHostVersion.cmake). */
std::string VersionText();

} // namespace uci_host
