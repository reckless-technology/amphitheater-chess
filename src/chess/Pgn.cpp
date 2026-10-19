#include "Pgn.h"

#include "Notation.h"

#include <cctype>

namespace uci_host::chess
{

namespace
{

/** The Seven Tag Roster, in its order, with the value written when a game lacks one. */
constexpr std::pair<std::string_view, std::string_view> kSevenTagRoster[] = {
    {"Event", "?"}, {"Site", "?"},  {"Date", "????.??.??"}, {"Round", "?"},
    {"White", "?"}, {"Black", "?"}, {"Result", "*"},
};

/** The longest line the writer makes. */
constexpr std::size_t kLineLength = 80;

/**
 * The NAG a suffix annotation stands for.
 * @param text "!", "?", "!!", "??", "!?" or "?!".
 * @return 1 to 6, or 0 for anything else.
 */
int SuffixNag(std::string_view text)
{
    constexpr std::string_view kSuffixes[] = {"!", "?", "!!", "??", "!?", "?!"};
    for (int index = 0; index < 6; ++index)
    {
        if (text == kSuffixes[index])
        {
            return index + 1;
        }
    }
    return 0;
}

/**
 * Whether a character ends a movetext symbol.
 * @param character The character.
 * @return Whether it is whitespace or one of PGN's self-delimiting characters.
 */
bool EndsSymbol(char character)
{
    return std::isspace(static_cast<unsigned char>(character)) || character == '{' || character == '}' ||
           character == '(' || character == ')' || character == ';' || character == '[' || character == ']' ||
           character == '$';
}

/**
 * Join a comment onto one already there.
 * @param existing The comment so far.
 * @param text The comment to add.
 */
void AppendComment(std::string &existing, std::string_view text)
{
    std::size_t first = 0;
    std::size_t last  = text.size();
    while (first < last && std::isspace(static_cast<unsigned char>(text[first])))
    {
        ++first;
    }
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1])))
    {
        --last;
    }
    if (first == last)
    {
        return;
    }
    if (!existing.empty())
    {
        existing += ' ';
    }
    existing += text.substr(first, last - first);
}

/** Movetext tokens being laid out into lines. */
class MovetextWriter
{
  public:
    /**
     * Add a token, separated from the one before by a space (or a line break).
     * @param token The token.
     */
    void Add(std::string token)
    {
        if (is_open_pending_)
        {
            token            = "(" + token;
            is_open_pending_ = false;
        }
        tokens_.push_back(std::move(token));
    }

    /** Open a variation: the bracket joins the next token. */
    void Open()
    {
        is_open_pending_ = true;
    }

    /** Close a variation: the bracket joins the last token. */
    void Close()
    {
        tokens_.back() += ')';
    }

    /**
     * Add a comment as words, so a long comment wraps.
     * @param comment The comment (braces in it become brackets: PGN comments do not nest).
     */
    void AddComment(const std::string &comment)
    {
        std::string text = comment;
        for (char &character : text)
        {
            if (character == '}' || character == '{')
            {
                character = character == '{' ? '[' : ']';
            }
            else if (character == '\n' || character == '\r' || character == '\t')
            {
                character = ' ';
            }
        }
        std::vector<std::string> words;
        std::size_t              start = 0;
        while (start < text.size())
        {
            const auto space = text.find(' ', start);
            if (space != start)
            {
                words.push_back(text.substr(start, space - start));
            }
            if (space == std::string::npos)
            {
                break;
            }
            start = space + 1;
        }
        if (words.empty())
        {
            words.emplace_back();
        }
        words.front() = "{" + words.front();
        words.back() += "}";
        for (auto &word : words)
        {
            Add(std::move(word));
        }
    }

    /** @return The tokens laid out in lines of at most kLineLength characters, ending with a line break. */
    std::string Lines() const
    {
        std::string text;
        std::size_t line_length = 0;
        for (const auto &token : tokens_)
        {
            if (line_length > 0 && line_length + 1 + token.size() > kLineLength)
            {
                text += '\n';
                line_length = 0;
            }
            if (line_length > 0)
            {
                text += ' ';
                ++line_length;
            }
            text += token;
            line_length += token.size();
        }
        return text + '\n';
    }

  private:
    std::vector<std::string> tokens_;                  ///< The tokens.
    bool                     is_open_pending_ = false; ///< Whether a variation's bracket waits for its first token.
};

/**
 * Write a line of moves, and its variations, as tokens.
 * @param line The moves.
 * @param position The position before the first.
 * @param writer Where the tokens go.
 */
void WriteLine(const PgnLine &line, Position position, MovetextWriter &writer)
{
    bool does_need_number = true;
    for (const PgnMove &move : line)
    {
        if (!move.comment_before.empty())
        {
            writer.AddComment(move.comment_before);
            does_need_number = true;
        }
        const int number = position.FullmoveNumber();
        if (position.SideToMove() == kWhite)
        {
            writer.Add(std::to_string(number) + ".");
        }
        else if (does_need_number)
        {
            writer.Add(std::to_string(number) + "...");
        }
        writer.Add(MoveToSan(position, move.move));
        does_need_number = false;
        for (const int nag : move.nags)
        {
            writer.Add("$" + std::to_string(nag));
        }
        if (!move.comment.empty())
        {
            writer.AddComment(move.comment);
            does_need_number = true;
        }
        for (const PgnLine &variation : move.variations)
        {
            if (variation.empty())
            {
                continue;
            }
            writer.Open();
            WriteLine(variation, position, writer);
            writer.Close();
            does_need_number = true;
        }
        position = position.MakeMove(move.move);
    }
}

} // namespace

PgnGame::PgnGame() : start(Position::Start())
{
}

std::string PgnGame::Tag(std::string_view name) const
{
    for (const auto &[tag, value] : tags)
    {
        if (tag == name)
        {
            return value;
        }
    }
    return {};
}

void PgnGame::SetTag(std::string_view name, std::string_view value)
{
    for (auto &[tag, existing] : tags)
    {
        if (tag == name)
        {
            existing = value;
            return;
        }
    }
    tags.emplace_back(name, value);
}

Game PgnGame::ToGame() const
{
    Game game(start);
    for (const PgnMove &move : moves)
    {
        game.Play(move.move);
    }
    if (!game.IsOver() && result != GameResult::kNone)
    {
        const std::string termination = Tag("Termination");
        Termination       reason      = Termination::kRecorded;
        if (termination == "time forfeit")
        {
            reason = Termination::kTimeForfeit;
        }
        else if (termination == "rules infraction")
        {
            reason = Termination::kIllegalMove;
        }
        else if (termination == "adjudication")
        {
            reason = Termination::kAdjudication;
        }
        else if (termination == "abandoned")
        {
            reason = Termination::kAbandoned;
        }
        game.SetOutcome({result, reason, ResultText(result)});
    }
    return game;
}

PgnGame PgnGame::FromGame(const Game &game)
{
    PgnGame pgn;
    pgn.start = game.StartPosition();
    if (!game.StartsFromStandardPosition())
    {
        pgn.SetTag("SetUp", "1");
        pgn.SetTag("FEN", game.StartPosition().ToFen());
    }
    for (int ply = 0; ply < game.PlyCount(); ++ply)
    {
        PgnMove move;
        move.move = game.Moves()[ply];
        move.san  = game.San(ply);
        pgn.moves.push_back(std::move(move));
    }
    pgn.result = game.GetOutcome().result;
    return pgn;
}

PgnReader::PgnReader(std::string_view text) : text_(text)
{
}

bool PgnReader::AtEnd() const
{
    return offset_ >= text_.size();
}

void PgnReader::SkipSpace()
{
    while (!AtEnd())
    {
        const char character     = text_[offset_];
        const bool is_line_start = offset_ == 0 || text_[offset_ - 1] == '\n';
        if (character == '%' && is_line_start)
        {
            while (!AtEnd() && text_[offset_] != '\n')
            {
                ++offset_;
            }
        }
        else if (std::isspace(static_cast<unsigned char>(character)))
        {
            line_ += character == '\n' ? 1 : 0;
            ++offset_;
        }
        else if (offset_ == 0 && text_.substr(0, 3) == "\xEF\xBB\xBF")
        {
            offset_ += 3; // A UTF-8 byte-order mark.
        }
        else
        {
            return;
        }
    }
}

void PgnReader::SkipToNextGame()
{
    // Back to the start of the line the error is on, to see whether it was among the tags.
    std::size_t line_start = offset_;
    while (line_start > 0 && text_[line_start - 1] != '\n')
    {
        --line_start;
    }
    bool is_previous_tag = line_start < text_.size() && text_[line_start] == '[';
    // On to the next line, then line by line to a tag line after a line that was not a tag.
    while (!AtEnd() && text_[offset_] != '\n')
    {
        ++offset_;
    }
    while (!AtEnd())
    {
        ++offset_; // The line break.
        ++line_;
        if (AtEnd())
        {
            return;
        }
        std::size_t first = offset_;
        while (first < text_.size() && (text_[first] == ' ' || text_[first] == '\t' || text_[first] == '\r'))
        {
            ++first;
        }
        const bool is_blank = first >= text_.size() || text_[first] == '\n';
        const bool is_tag   = !is_blank && text_[first] == '[';
        if (is_tag && !is_previous_tag)
        {
            offset_ = first;
            return;
        }
        if (!is_blank)
        {
            is_previous_tag = is_tag;
        }
        while (!AtEnd() && text_[offset_] != '\n')
        {
            ++offset_;
        }
    }
}

bool PgnReader::Next(PgnGame &game, std::string &error)
{
    error.clear();
    SkipSpace();
    if (AtEnd())
    {
        return false;
    }
    ++game_count_;
    game            = PgnGame{};
    const auto fail = [&](const std::string &message) {
        error = "game " + std::to_string(game_count_) + ", line " + std::to_string(line_) + ": " + message;
        SkipToNextGame();
        return true;
    };

    // The tags.
    while (!AtEnd() && text_[offset_] == '[')
    {
        ++offset_;
        SkipSpace();
        const std::size_t name_start = offset_;
        while (!AtEnd() && (std::isalnum(static_cast<unsigned char>(text_[offset_])) || text_[offset_] == '_'))
        {
            ++offset_;
        }
        const std::string name(text_.substr(name_start, offset_ - name_start));
        SkipSpace();
        if (name.empty() || AtEnd() || text_[offset_] != '"')
        {
            return fail("a tag is not a name and a quoted value");
        }
        ++offset_;
        std::string value;
        while (!AtEnd() && text_[offset_] != '"' && text_[offset_] != '\n')
        {
            if (text_[offset_] == '\\' && offset_ + 1 < text_.size() &&
                (text_[offset_ + 1] == '"' || text_[offset_ + 1] == '\\'))
            {
                ++offset_;
            }
            value += text_[offset_++];
        }
        if (AtEnd() || text_[offset_] != '"')
        {
            return fail("tag " + name + "'s value has no closing quote");
        }
        ++offset_;
        SkipSpace();
        if (AtEnd() || text_[offset_] != ']')
        {
            return fail("tag " + name + " has no closing bracket");
        }
        ++offset_;
        game.SetTag(name, value);
        SkipSpace();
    }

    const std::string fen = game.Tag("FEN");
    if (!fen.empty())
    {
        auto start = Position::FromFen(fen, FenMode::kTolerant);
        if (!start)
        {
            return fail("the FEN tag is not a position: " + start.ErrorMessage());
        }
        game.start = *start;
    }

    // The movetext, as a stack of lines: the main line, and each variation open within it.
    struct Level
    {
        PgnLine *line;        ///< The line moves are added to.
        Position position;    ///< The position after the line's last move.
        Position before_last; ///< The position before it, where a variation of it starts.
    };

    std::vector<Level> levels{{&game.moves, game.start, game.start}};
    std::string        pending_comment; // A comment before the first move of a line.
    bool               has_result = false;
    while (true)
    {
        SkipSpace();
        if (AtEnd())
        {
            break;
        }
        Level     &level     = levels.back();
        PgnMove   *last      = level.line->empty() ? nullptr : &level.line->back();
        const char character = text_[offset_];
        if (character == '[')
        {
            if (levels.size() > 1)
            {
                return fail("a variation is not closed");
            }
            break; // The next game, after a game with no result.
        }
        if (character == '{' || character == ';')
        {
            const char        end   = character == '{' ? '}' : '\n';
            const std::size_t start = ++offset_;
            while (!AtEnd() && text_[offset_] != end)
            {
                line_ += text_[offset_] == '\n' ? 1 : 0;
                ++offset_;
            }
            if (AtEnd() && end == '}')
            {
                return fail("a comment is not closed");
            }
            AppendComment(last ? last->comment : pending_comment, text_.substr(start, offset_ - start));
            if (end == '}')
            {
                ++offset_; // The closing brace; a ';' comment's line break is whitespace, skipped next.
            }
            continue;
        }
        if (character == '(')
        {
            ++offset_;
            if (last == nullptr)
            {
                return fail("a variation with no move before it");
            }
            const Position from = level.before_last;
            levels.push_back({&last->variations.emplace_back(), from, from});
            pending_comment.clear();
            continue;
        }
        if (character == ')')
        {
            ++offset_;
            if (levels.size() == 1)
            {
                return fail("a ')' closes no variation");
            }
            levels.pop_back();
            pending_comment.clear();
            continue;
        }
        if (character == '$')
        {
            const std::size_t start = ++offset_;
            while (!AtEnd() && std::isdigit(static_cast<unsigned char>(text_[offset_])))
            {
                ++offset_;
            }
            if (offset_ == start || last == nullptr)
            {
                return fail("a NAG with no move before it, or no number");
            }
            last->nags.push_back(std::stoi(std::string(text_.substr(start, offset_ - start))));
            continue;
        }
        if (character == ']' || character == '}')
        {
            return fail(std::string("a stray '") + character + "'");
        }

        // A symbol: a result, a move number, an annotation or a move.
        const std::size_t start = offset_;
        while (!AtEnd() && !EndsSymbol(text_[offset_]))
        {
            ++offset_;
        }
        std::string_view symbol = text_.substr(start, offset_ - start);
        if (symbol == "1-0" || symbol == "0-1" || symbol == "1/2-1/2" || symbol == "*")
        {
            if (levels.size() > 1)
            {
                return fail("a result inside a variation");
            }
            game.result = ParseResultText(symbol);
            has_result  = true;
            break;
        }
        std::size_t digits = 0;
        while (digits < symbol.size() && std::isdigit(static_cast<unsigned char>(symbol[digits])))
        {
            ++digits;
        }
        std::size_t dots = digits;
        while (dots < symbol.size() && symbol[dots] == '.')
        {
            ++dots;
        }
        if (digits > 0 && (dots > digits || dots == symbol.size()))
        {
            symbol.remove_prefix(dots); // A move number, alone or joined to its move.
            if (symbol.empty())
            {
                continue;
            }
        }
        // Suffix annotations, alone or joined to the move.
        std::size_t annotation = symbol.size();
        while (annotation > 0 && (symbol[annotation - 1] == '!' || symbol[annotation - 1] == '?'))
        {
            --annotation;
        }
        const int nag = SuffixNag(symbol.substr(annotation));
        symbol        = symbol.substr(0, annotation);
        if (symbol.empty())
        {
            if (nag == 0 || last == nullptr)
            {
                return fail("an annotation with no move before it");
            }
            last->nags.push_back(nag);
            continue;
        }
        if (symbol == "--" || symbol == "Z0" || symbol == "0000")
        {
            return fail("null moves are not supported");
        }
        const auto move = ParseSan(level.position, symbol);
        if (!move)
        {
            return fail(move.ErrorMessage());
        }
        PgnMove entry;
        entry.move           = *move;
        entry.san            = MoveToSan(level.position, *move);
        entry.comment_before = std::move(pending_comment);
        pending_comment.clear();
        if (nag != 0)
        {
            entry.nags.push_back(nag);
        }
        level.line->push_back(std::move(entry));
        level.before_last = level.position;
        level.position    = level.position.MakeMove(*move);
    }
    if (levels.size() > 1)
    {
        return fail("a variation is not closed");
    }
    if (!has_result)
    {
        game.result = ParseResultText(game.Tag("Result"));
    }
    if (game.moves.empty())
    {
        game.comment = pending_comment;
    }
    return true;
}

std::vector<PgnGame> ReadPgn(std::string_view text, std::vector<std::string> *errors)
{
    std::vector<PgnGame> games;
    PgnReader            reader(text);
    PgnGame              game;
    std::string          error;
    while (reader.Next(game, error))
    {
        if (error.empty())
        {
            games.push_back(std::move(game));
        }
        else if (errors != nullptr)
        {
            errors->push_back(error);
        }
    }
    return games;
}

std::string EscapeTagValue(std::string_view value)
{
    std::string escaped;
    for (const char character : value)
    {
        if (character == '"' || character == '\\')
        {
            escaped += '\\';
        }
        escaped += character == '\n' || character == '\r' ? ' ' : character;
    }
    return escaped;
}

std::string WritePgn(const PgnGame &game)
{
    std::string text;
    const auto  write_tag = [&](std::string_view name, std::string_view value) {
        text += "[" + std::string(name) + " \"" + EscapeTagValue(value) + "\"]\n";
    };
    for (const auto &[name, fallback] : kSevenTagRoster)
    {
        if (name == "Result")
        {
            write_tag(name, ResultText(game.result));
            continue;
        }
        const std::string value = game.Tag(name);
        write_tag(name, value.empty() ? fallback : std::string_view(value));
    }
    const bool is_standard_start = game.start == Position::Start();
    for (const auto &[name, value] : game.tags)
    {
        bool is_roster = false;
        for (const auto &[roster_name, fallback] : kSevenTagRoster)
        {
            is_roster |= name == roster_name;
        }
        if (!is_roster && !((name == "FEN" || name == "SetUp") && is_standard_start))
        {
            write_tag(name, value);
        }
    }
    if (!is_standard_start && game.Tag("FEN").empty())
    {
        write_tag("SetUp", "1");
        write_tag("FEN", game.start.ToFen());
    }
    text += '\n';

    MovetextWriter writer;
    if (!game.comment.empty())
    {
        writer.AddComment(game.comment);
    }
    WriteLine(game.moves, game.start, writer);
    writer.Add(ResultText(game.result));
    return text + writer.Lines() + '\n';
}

} // namespace uci_host::chess
