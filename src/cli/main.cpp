// amphitheater-cli: the command-line runner (Cli.h). Ctrl+C stops a match after abandoning the games in progress.

#include "Cli.h"
#include "uci/Utf8Arguments.h"

#include <atomic>
#include <csignal>
#include <iostream>

namespace
{

/** Set by Ctrl+C. */
std::atomic<bool> is_stop_requested = false;

/**
 * The interrupt handler: ask the match to stop, and leave a second Ctrl+C to the system.
 * @param signal The signal.
 */
extern "C" void RequestStop(int signal)
{
    is_stop_requested = true;
    std::signal(signal, SIG_DFL);
}

} // namespace

/**
 * Run the command line.
 * @param argc The argument count.
 * @param argv The arguments (read again as UTF-8 on Windows).
 * @return The exit code.
 */
int main(int argc, char **argv)
{
    std::signal(SIGINT, RequestStop);
    return amphitheater::cli::RunCli(amphitheater::Utf8Arguments(argc, argv), std::cout, std::cerr, &is_stop_requested);
}
