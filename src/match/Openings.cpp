#include "Openings.h"

#include "chess/Pgn.h"
#include "chess/Position.h"
#include "uci/UciProtocol.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace uci_host::match
{

Result<Opening> ParseEpdLine(std::string_view line)
{
    const auto words = uci::SplitWords(line);
    if (words.size() < 4)
    {
        return Error{"an EPD line has at least four fields"};
    }
    std::string fen =
        std::string(words[0]) + ' ' + std::string(words[1]) + ' ' + std::string(words[2]) + ' ' + std::string(words[3]);
    // Two more fields that are whole numbers make the line a FEN, not an EPD with operations.
    const auto is_number = [](std::string_view word) {
        return !word.empty() && std::all_of(word.begin(), word.end(), [](char c) { return c >= '0' && c <= '9'; });
    };
    Opening     opening;
    std::size_t operations = 4;
    std::string halfmove   = "0";
    std::string fullmove   = "1";
    if (words.size() >= 6 && is_number(words[4]) && is_number(words[5]))
    {
        halfmove   = std::string(words[4]);
        fullmove   = std::string(words[5]);
        operations = 6;
    }
    // The operations: "opcode operand...;", operands quoted where they hold spaces.
    const std::size_t rest_start =
        operations < words.size() ? static_cast<std::size_t>(words[operations].data() - line.data()) : line.size();
    std::string_view rest = line.substr(rest_start);
    while (!rest.empty())
    {
        const auto end      = rest.find(';');
        const auto op       = rest.substr(0, end);
        const auto op_words = uci::SplitWords(op);
        if (op_words.size() >= 2)
        {
            if (op_words[0] == "hmvc")
            {
                halfmove = std::string(op_words[1]);
            }
            else if (op_words[0] == "fmvn")
            {
                fullmove = std::string(op_words[1]);
            }
            else if (op_words[0] == "id")
            {
                const auto first = op.find('"');
                const auto last  = op.rfind('"');
                opening.name     = first != std::string_view::npos && last > first
                                       ? std::string(op.substr(first + 1, last - first - 1))
                                       : std::string(op_words[1]);
            }
        }
        if (end == std::string_view::npos)
        {
            break;
        }
        rest = rest.substr(end + 1);
    }
    fen += ' ' + halfmove + ' ' + fullmove;
    const auto position = chess::Position::FromFen(fen, chess::FenMode::kTolerant);
    if (!position)
    {
        return Error{position.ErrorMessage()};
    }
    opening.fen = position->ToFen();
    return opening;
}

std::vector<Opening> ParseOpenings(std::string_view text, OpeningFormat format, int plies,
                                   std::vector<std::string> *errors)
{
    std::vector<Opening> openings;
    if (format == OpeningFormat::kEpd)
    {
        std::size_t start  = 0;
        int         number = 0;
        while (start < text.size())
        {
            const auto end  = text.find('\n', start);
            auto       line = text.substr(start, end - start);
            ++number;
            start = end == std::string_view::npos ? text.size() : end + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            {
                line.remove_suffix(1);
            }
            const auto first = line.find_first_not_of(" \t");
            if (first == std::string_view::npos || line[first] == '#')
            {
                continue;
            }
            auto opening = ParseEpdLine(line);
            if (opening)
            {
                openings.push_back(std::move(*opening));
            }
            else if (errors != nullptr)
            {
                errors->push_back("line " + std::to_string(number) + ": " + opening.ErrorMessage());
            }
        }
        return openings;
    }
    for (const auto &game : chess::ReadPgn(text, errors))
    {
        Opening opening;
        if (!(game.start == chess::Position::Start()))
        {
            opening.fen = game.start.ToFen();
        }
        for (const auto &move : game.moves)
        {
            if (plies > 0 && static_cast<int>(opening.moves.size()) >= plies)
            {
                break;
            }
            opening.moves.push_back(move.move.ToUci());
        }
        for (const char *tag : {"Opening", "ECO", "Event"})
        {
            const std::string value = game.Tag(tag);
            if (opening.name.empty() && !value.empty() && value != "?")
            {
                opening.name = value;
            }
        }
        openings.push_back(std::move(opening));
    }
    return openings;
}

Result<std::string> ReadTextFile(const std::string &path)
{
    std::ifstream file(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!file)
    {
        return Error{"cannot open '" + path + "'"};
    }
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

Result<std::vector<Opening>> LoadOpenings(const std::string &path, OpeningFormat format, int plies)
{
    auto text = ReadTextFile(path);
    if (!text)
    {
        return Error{text.ErrorMessage()};
    }
    std::vector<std::string> errors;
    auto                     openings = ParseOpenings(*text, format, plies, &errors);
    if (openings.empty())
    {
        return Error{"'" + path + "' holds no openings" + (errors.empty() ? std::string{} : ": " + errors.front())};
    }
    return openings;
}

void OrderOpenings(std::vector<Opening> &openings, OpeningOrder order, std::uint64_t seed, int start)
{
    if (openings.empty())
    {
        return;
    }
    if (order == OpeningOrder::kRandom)
    {
        std::mt19937_64 generator(seed);
        // A Fisher-Yates shuffle of the generator's own words, so every platform shuffles alike (std::shuffle's
        // use of the generator is the library's to choose).
        for (std::size_t index = openings.size() - 1; index > 0; --index)
        {
            std::swap(openings[index], openings[static_cast<std::size_t>(generator() % (index + 1))]);
        }
        return;
    }
    const auto offset = static_cast<std::size_t>(start) % openings.size();
    std::rotate(openings.begin(), openings.begin() + static_cast<std::ptrdiff_t>(offset), openings.end());
}

} // namespace uci_host::match
