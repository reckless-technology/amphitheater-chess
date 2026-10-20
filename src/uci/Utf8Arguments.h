#pragma once
/// @file Utf8Arguments.h A program's command-line arguments as UTF-8 on every platform.
///
/// main's argv is UTF-8 on Linux and macOS but the ANSI code page on Windows, where anything outside it arrives as
/// '?'. On Windows the arguments are read again from the wide command line (GetCommandLineW, CommandLineToArgvW) and
/// converted; elsewhere argv is taken as it is.

#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// shellapi.h needs windows.h before it, so it stands in a block of its own (the formatter sorts within a block).
#include <shellapi.h>
#endif

namespace amphitheater
{

/**
 * The program's arguments, its name first, in UTF-8.
 * @param argc main's argc.
 * @param argv main's argv.
 * @return The arguments.
 */
inline std::vector<std::string> Utf8Arguments(int argc, char **argv)
{
#ifdef _WIN32
    int       count = 0;
    wchar_t **wide  = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide != nullptr)
    {
        std::vector<std::string> arguments;
        for (int index = 0; index < count; ++index)
        {
            const int   size = WideCharToMultiByte(CP_UTF8, 0, wide[index], -1, nullptr, 0, nullptr, nullptr);
            std::string text(static_cast<std::size_t>(size > 0 ? size - 1 : 0), '\0');
            if (size > 1)
            {
                WideCharToMultiByte(CP_UTF8, 0, wide[index], -1, text.data(), size, nullptr, nullptr);
            }
            arguments.push_back(std::move(text));
        }
        LocalFree(wide);
        return arguments;
    }
#endif
    return std::vector<std::string>(argv, argv + argc);
}

} // namespace amphitheater
