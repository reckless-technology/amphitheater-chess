#include "Process.h"

namespace amphitheater::uci
{

namespace
{

/** The longest line kept whole; a longer one is pushed in pieces of this length. */
constexpr std::size_t kMaximumLineLength = 1 << 20;

} // namespace

void ChildProcess::ReadLines(ProcessStream stream, const std::function<long(char *, std::size_t)> &read)
{
    std::string line;
    char        buffer[4096];
    const auto  push = [&]() {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        output_.Push({stream, std::move(line), std::chrono::steady_clock::now()});
        line.clear();
    };
    while (true)
    {
        const long count = read(buffer, sizeof buffer);
        if (count <= 0)
        {
            break;
        }
        for (long index = 0; index < count; ++index)
        {
            if (buffer[index] == '\n')
            {
                push();
            }
            else
            {
                line += buffer[index];
                if (line.size() >= kMaximumLineLength)
                {
                    push();
                }
            }
        }
    }
    if (!line.empty())
    {
        push();
    }
    if (--open_streams_ == 0)
    {
        output_.Close();
    }
}

} // namespace amphitheater::uci
