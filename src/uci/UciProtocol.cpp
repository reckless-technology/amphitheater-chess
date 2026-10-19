#include "UciProtocol.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>

namespace uci_host::uci
{

namespace
{

/**
 * Read a whole word as an integer.
 * @tparam Integer The integer type.
 * @param word The word.
 * @return The number, or nothing.
 */
template <typename Integer> std::optional<Integer> ParseInteger(std::string_view word)
{
    Integer value{};
    if (!word.empty() && word.front() == '+')
    {
        word.remove_prefix(1);
    }
    const auto result = std::from_chars(word.data(), word.data() + word.size(), value);
    if (word.empty() || result.ec != std::errc{} || result.ptr != word.data() + word.size())
    {
        return std::nullopt;
    }
    return value;
}

/**
 * Words joined by single spaces.
 * @param words The words.
 * @param first The first to join.
 * @param last One past the last.
 * @return The text.
 */
std::string Join(const std::vector<std::string_view> &words, std::size_t first, std::size_t last)
{
    std::string text;
    for (std::size_t index = first; index < last; ++index)
    {
        if (!text.empty())
        {
            text += ' ';
        }
        text += words[index];
    }
    return text;
}

/**
 * Text in lower case, for comparisons UCI makes case-insensitive.
 * @param text The text.
 * @return Its lower-case copy.
 */
std::string Lower(std::string_view text)
{
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lower;
}

} // namespace

std::vector<std::string_view> SplitWords(std::string_view line)
{
    std::vector<std::string_view> words;
    std::size_t                   position = 0;
    while (position < line.size())
    {
        while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position])))
        {
            ++position;
        }
        const std::size_t start = position;
        while (position < line.size() && !std::isspace(static_cast<unsigned char>(line[position])))
        {
            ++position;
        }
        if (position > start)
        {
            words.push_back(line.substr(start, position - start));
        }
    }
    return words;
}

std::string OptionTypeName(OptionType type)
{
    switch (type)
    {
    case OptionType::kCheck:
        return "check";
    case OptionType::kSpin:
        return "spin";
    case OptionType::kCombo:
        return "combo";
    case OptionType::kButton:
        return "button";
    default:
        return "string";
    }
}

std::optional<std::string> EngineOption::Validate(std::string_view value) const
{
    switch (type)
    {
    case OptionType::kCheck:
        if (value != "true" && value != "false")
        {
            return "option " + name + " is a check: 'true' or 'false', not '" + std::string(value) + "'";
        }
        return std::nullopt;
    case OptionType::kSpin: {
        const auto number = ParseInteger<std::int64_t>(value);
        if (!number || (minimum && *number < *minimum) || (maximum && *number > *maximum))
        {
            return "option " + name + " is a spin from " + (minimum ? std::to_string(*minimum) : "?") + " to " +
                   (maximum ? std::to_string(*maximum) : "?") + ", not '" + std::string(value) + "'";
        }
        return std::nullopt;
    }
    case OptionType::kCombo:
        for (const auto &choice : choices)
        {
            if (Lower(choice) == Lower(value))
            {
                return std::nullopt;
            }
        }
        return "option " + name + " has no choice '" + std::string(value) + "'";
    case OptionType::kButton:
        if (!value.empty())
        {
            return "option " + name + " is a button and takes no value";
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

Result<EngineOption> ParseOptionLine(std::string_view line)
{
    const auto words = SplitWords(line);
    if (words.size() < 2 || words[0] != "option" || words[1] != "name")
    {
        return Error{"an option line starts 'option name'"};
    }
    // The name runs to the word "type"; every value runs to the next keyword.
    std::size_t type_index = 2;
    while (type_index < words.size() && words[type_index] != "type")
    {
        ++type_index;
    }
    if (type_index == 2 || type_index + 1 >= words.size())
    {
        return Error{"an option line names the option and its type"};
    }
    EngineOption option;
    option.name                 = Join(words, 2, type_index);
    const std::string_view type = words[type_index + 1];
    if (type == "check")
    {
        option.type = OptionType::kCheck;
    }
    else if (type == "spin")
    {
        option.type = OptionType::kSpin;
    }
    else if (type == "combo")
    {
        option.type = OptionType::kCombo;
    }
    else if (type == "button")
    {
        option.type = OptionType::kButton;
    }
    else if (type == "string")
    {
        option.type = OptionType::kString;
    }
    else
    {
        return Error{"option " + option.name + " has a type UCI does not define: '" + std::string(type) + "'"};
    }
    const auto is_keyword = [](std::string_view word) {
        return word == "default" || word == "min" || word == "max" || word == "var";
    };
    std::size_t index = type_index + 2;
    while (index < words.size())
    {
        const std::string_view keyword = words[index];
        std::size_t            end     = index + 1;
        if (keyword == "default" && option.type == OptionType::kString)
        {
            end = words.size(); // A string's default may hold any words.
            while (end > index + 1 && (words[end - 2] == "min" || words[end - 2] == "max"))
            {
                end -= 2;
            }
        }
        else
        {
            while (end < words.size() && !is_keyword(words[end]))
            {
                ++end;
            }
        }
        const std::string value = Join(words, index + 1, end);
        if (keyword == "default")
        {
            option.default_value = value == "<empty>" ? std::string{} : value;
        }
        else if (keyword == "min")
        {
            option.minimum = ParseInteger<std::int64_t>(value);
        }
        else if (keyword == "max")
        {
            option.maximum = ParseInteger<std::int64_t>(value);
        }
        else if (keyword == "var")
        {
            option.choices.push_back(value);
        }
        index = end;
    }
    return option;
}

std::string Score::ToString() const
{
    if (kind == Kind::kMate)
    {
        return "#" + std::to_string(value);
    }
    char text[32];
    std::snprintf(text, sizeof text, "%+.2f", value / 100.0);
    return text;
}

SearchInfo ParseInfoLine(std::string_view line)
{
    SearchInfo info;
    const auto words = SplitWords(line);
    const auto next  = [&](std::size_t &index) -> std::string_view {
        return index + 1 < words.size() ? words[++index] : std::string_view{};
    };
    for (std::size_t index = 1; index < words.size(); ++index)
    {
        const std::string_view keyword = words[index];
        if (keyword == "depth")
        {
            info.depth = ParseInteger<int>(next(index));
        }
        else if (keyword == "seldepth")
        {
            info.selective_depth = ParseInteger<int>(next(index));
        }
        else if (keyword == "multipv")
        {
            info.multipv = ParseInteger<int>(next(index));
        }
        else if (keyword == "nodes")
        {
            info.nodes = ParseInteger<std::int64_t>(next(index));
        }
        else if (keyword == "nps")
        {
            info.nodes_per_second = ParseInteger<std::int64_t>(next(index));
        }
        else if (keyword == "hashfull")
        {
            info.hash_full = ParseInteger<int>(next(index));
        }
        else if (keyword == "tbhits")
        {
            info.tablebase_hits = ParseInteger<std::int64_t>(next(index));
        }
        else if (keyword == "time")
        {
            info.time = ParseInteger<std::int64_t>(next(index));
        }
        else if (keyword == "cpuload")
        {
            info.cpu_load = ParseInteger<int>(next(index));
        }
        else if (keyword == "currmove")
        {
            const auto move = next(index);
            if (!move.empty())
            {
                info.current_move = std::string(move);
            }
        }
        else if (keyword == "currmovenumber")
        {
            info.current_move_number = ParseInteger<int>(next(index));
        }
        else if (keyword == "score")
        {
            const auto kind  = next(index);
            const auto value = ParseInteger<int>(next(index));
            if ((kind == "cp" || kind == "mate") && value)
            {
                Score score;
                score.kind  = kind == "cp" ? Score::Kind::kCentipawns : Score::Kind::kMate;
                score.value = *value;
                while (index + 1 < words.size() &&
                       (words[index + 1] == "lowerbound" || words[index + 1] == "upperbound"))
                {
                    (words[++index] == "lowerbound" ? score.is_lower_bound : score.is_upper_bound) = true;
                }
                info.score = score;
            }
        }
        else if (keyword == "pv")
        {
            // The moves run to the next keyword; a move is never a keyword.
            static constexpr std::string_view kKeywords[] = {
                "depth",    "seldepth",   "multipv",  "score",   "nodes",    "nps",
                "hashfull", "tbhits",     "time",     "cpuload", "currmove", "currmovenumber",
                "string",   "refutation", "currline", "wdl",     "sbhits"};
            while (index + 1 < words.size() &&
                   std::find(std::begin(kKeywords), std::end(kKeywords), words[index + 1]) == std::end(kKeywords))
            {
                info.pv.emplace_back(words[++index]);
            }
        }
        else if (keyword == "string")
        {
            // The rest of the line as written, its own spacing kept.
            const std::size_t start = static_cast<std::size_t>(words[index].data() - line.data()) + 6;
            std::string_view  rest  = start < line.size() ? line.substr(start) : std::string_view{};
            while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front())))
            {
                rest.remove_prefix(1);
            }
            info.text = std::string(rest);
            break;
        }
    }
    return info;
}

std::optional<BestMove> ParseBestMoveLine(std::string_view line)
{
    const auto words = SplitWords(line);
    if (words.empty() || words[0] != "bestmove")
    {
        return std::nullopt;
    }
    BestMove best;
    if (words.size() >= 2)
    {
        best.move = std::string(words[1]);
    }
    if (words.size() >= 4 && words[2] == "ponder")
    {
        best.ponder = std::string(words[3]);
    }
    return best;
}

std::string FormatGo(const GoParameters &parameters)
{
    std::string line = "go";
    const auto  add  = [&](std::string_view keyword, long long value) {
        line += ' ';
        line += keyword;
        line += ' ' + std::to_string(value);
    };
    if (parameters.is_ponder)
    {
        line += " ponder";
    }
    if (parameters.white_time)
    {
        add("wtime", std::max<long long>(0, parameters.white_time->count()));
    }
    if (parameters.black_time)
    {
        add("btime", std::max<long long>(0, parameters.black_time->count()));
    }
    if (parameters.white_increment)
    {
        add("winc", parameters.white_increment->count());
    }
    if (parameters.black_increment)
    {
        add("binc", parameters.black_increment->count());
    }
    if (parameters.moves_to_go)
    {
        add("movestogo", *parameters.moves_to_go);
    }
    if (parameters.depth)
    {
        add("depth", *parameters.depth);
    }
    if (parameters.nodes)
    {
        add("nodes", *parameters.nodes);
    }
    if (parameters.mate)
    {
        add("mate", *parameters.mate);
    }
    if (parameters.move_time)
    {
        add("movetime", parameters.move_time->count());
    }
    if (parameters.is_infinite)
    {
        line += " infinite";
    }
    if (!parameters.search_moves.empty())
    {
        line += " searchmoves";
        for (const auto &move : parameters.search_moves)
        {
            line += ' ' + move;
        }
    }
    return line;
}

std::string FormatPosition(std::string_view fen, const std::vector<std::string> &moves)
{
    std::string line = fen.empty() ? "position startpos" : "position fen " + std::string(fen);
    if (!moves.empty())
    {
        line += " moves";
        for (const auto &move : moves)
        {
            line += ' ' + move;
        }
    }
    return line;
}

std::string FormatSetOption(std::string_view name, std::string_view value)
{
    std::string line = "setoption name " + std::string(name);
    if (!value.empty())
    {
        line += " value " + std::string(value);
    }
    return line;
}

} // namespace uci_host::uci
