#pragma once
// What the suites share: the environment read without the C runtime's deprecated getenv, and the fake engine's
// configuration.

#include <cstdlib>
#include <optional>
#include <string>

namespace amphitheater::testing
{

// An environment variable's value, or nothing when it is unset or empty.
inline std::optional<std::string> EnvironmentVariable(const char *name)
{
#ifdef _WIN32
    char       *value  = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
    {
        return std::nullopt;
    }
    std::string text(value);
    std::free(value);
#else
    const char *value = std::getenv(name);
    if (value == nullptr)
    {
        return std::nullopt;
    }
    std::string text(value);
#endif
    if (text.empty())
    {
        return std::nullopt;
    }
    return text;
}

} // namespace amphitheater::testing
