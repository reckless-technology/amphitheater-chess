/// @file TextBook.cpp Reading, writing and replaying the Reckless engines' text opening books.

#include "TextBook.h"

#include "PolyglotBook.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <sstream>

namespace amphitheater::chess
{
namespace
{

/// @brief Whether a token is a move number (`1.`, `12...`) rather than a move.
/// @param token The token.
/// @return true when it begins with a digit, which is how the engines' own parsers tell the two apart.
bool IsMoveNumber(std::string_view token)
{
    return !token.empty() && std::isdigit(static_cast<unsigned char>(token.front())) != 0;
}

} // namespace

std::size_t TextBook::OpeningCount() const
{
    return static_cast<std::size_t>(
        std::ranges::count_if(lines, [](const TextBookLine &line) { return line.HasMoves(); }));
}

std::size_t TextBook::QuestionableTokenCount() const
{
    std::size_t count = 0;
    for (const TextBookLine &line : lines)
    {
        count += static_cast<std::size_t>(
            std::ranges::count_if(line.moves, [](const TextBookMove &move) { return move.is_questionable; }));
    }
    return count;
}

TextBook ReadTextBook(std::string_view text)
{
    TextBook    book;
    std::size_t start = 0;

    while (start <= text.size())
    {
        const std::size_t newline = text.find('\n', start);
        std::string_view  raw =
            text.substr(start, newline == std::string_view::npos ? std::string_view::npos : newline - start);
        // A file written on Windows, or edited there, carries the carriage return as part of the line. The engines'
        // parsers tolerate it because `>>` treats it as whitespace; this reader drops it so that a round trip writes
        // the LF the format is pinned to rather than preserving a stray CR.
        if (!raw.empty() && raw.back() == '\r')
        {
            raw.remove_suffix(1);
        }

        TextBookLine     line;
        std::string_view body = raw;
        if (const std::size_t hash = raw.find('#'); hash != std::string_view::npos)
        {
            body         = raw.substr(0, hash);
            line.comment = std::string(raw.substr(hash));
        }

        std::istringstream tokens{std::string(body)};
        for (std::string token; tokens >> token;)
        {
            line.tokens.push_back(token);
            if (IsMoveNumber(token))
            {
                continue;
            }
            TextBookMove move;
            move.is_questionable = token.back() == '?';
            move.coordinate      = move.is_questionable ? token.substr(0, token.size() - 1) : token;
            line.moves.push_back(std::move(move));
        }
        book.lines.push_back(std::move(line));

        if (newline == std::string_view::npos)
        {
            break;
        }
        start = newline + 1;
    }

    // A file ending in a newline - every one of these books does - leaves a final empty line that is an artefact of
    // the terminator rather than a line of the book. Dropping it is what makes the round trip exact, since the writer
    // terminates every line it writes.
    if (!book.lines.empty() && book.lines.back().tokens.empty() && book.lines.back().comment.empty())
    {
        book.lines.pop_back();
    }
    return book;
}

std::string WriteTextBook(const TextBook &book)
{
    std::string text;
    for (const TextBookLine &line : book.lines)
    {
        for (std::size_t index = 0; index < line.tokens.size(); ++index)
        {
            if (index != 0)
            {
                text += ' ';
            }
            text += line.tokens[index];
        }
        if (!line.comment.empty())
        {
            if (!line.tokens.empty())
            {
                text += ' ';
            }
            text += line.comment;
        }
        text += '\n';
    }
    return text;
}

TextBookIndex ReplayTextBook(const TextBook &book)
{
    TextBookIndex index;

    for (std::size_t number = 0; number < book.lines.size(); ++number)
    {
        const TextBookLine &line = book.lines[number];
        if (!line.HasMoves())
        {
            continue;
        }

        Position position = Position::Start();
        bool     is_whole = true;

        for (const TextBookMove &move : line.moves)
        {
            const MoveList legal = position.GenerateLegalMoves();
            const auto     found = std::ranges::find_if(
                legal, [&move](const Move &candidate) { return candidate.ToUci() == move.coordinate; });
            if (found == legal.end())
            {
                // Report and stop this line rather than the whole book: one corrupt line is a line to fix, and a
                // reader that gave up on the file could not say which line was wrong.
                index.errors.push_back(
                    std::format("line {}: '{}' is not legal here ({})", number + 1, move.coordinate, position.ToFen()));
                is_whole = false;
                break;
            }
            TextBookIndex::Entry &entry = index.positions[PolyglotKey(position)];
            std::vector<Move>    &into  = move.is_questionable ? entry.questionable : entry.selectable;
            // A position is reached by many lines and many move orders, so the same move arrives repeatedly. The
            // engines store the repeats, because a move's weight IS the number of lines through it - but this index
            // answers "what may be played here", so it holds each move once.
            if (std::ranges::find(into, *found) == into.end())
            {
                into.push_back(*found);
            }
            position = position.MakeMove(*found);
        }
        if (is_whole)
        {
            ++index.replayed;
        }
    }
    return index;
}

} // namespace amphitheater::chess
