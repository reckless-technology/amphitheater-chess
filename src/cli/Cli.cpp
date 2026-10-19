#include "Cli.h"

#include "chess/Notation.h"
#include "chess/Perft.h"
#include "chess/PolyglotBook.h"
#include "chess/Position.h"
#include "match/Openings.h"
#include "uci/Engine.h"

#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <ostream>

namespace uci_host::cli
{

namespace
{

using Clock     = std::chrono::steady_clock;
using KeyValues = std::vector<std::pair<std::string, std::string>>;

/**
 * Whether an argument is an option's name: a dash and something that is not a digit. A lone "-" (a FEN's empty
 * field) and a negative number are values.
 * @param argument The argument.
 * @return Whether it names an option.
 */
bool IsOptionName(const std::string &argument)
{
    return argument.size() > 1 && argument[0] == '-' && !std::isdigit(static_cast<unsigned char>(argument[1]));
}

/**
 * An option's name without its dashes.
 * @param argument "-games" or "--games".
 * @return "games".
 */
std::string OptionName(const std::string &argument)
{
    return argument.substr(argument.find_first_not_of('-'));
}

/**
 * Read a whole number.
 * @param text The text.
 * @param what The option it is for, for the error.
 * @param minimum The smallest allowed.
 * @return The number, or why not.
 */
Result<long long> ParseNumber(const std::string &text, const std::string &what, long long minimum)
{
    long long  value  = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size() || value < minimum)
    {
        return Error{what + " takes a whole number of at least " + std::to_string(minimum) + ", not '" + text + "'"};
    }
    return value;
}

/**
 * Read a time in seconds.
 * @param text The text ("0.5").
 * @param what The option it is for, for the error.
 * @return The time, or why not.
 */
Result<chess::Milliseconds> ParseSecondsArgument(const std::string &text, const std::string &what)
{
    const auto time = chess::ParseSeconds(text);
    if (!time)
    {
        return Error{what + " takes a time in seconds, not '" + text + "'"};
    }
    return *time;
}

/** The arguments, read one at a time. */
class ArgumentReader
{
  public:
    /** @param arguments The arguments. */
    explicit ArgumentReader(const std::vector<std::string> &arguments) : arguments_(arguments)
    {
    }

    /** @return Whether every argument has been read. */
    bool AtEnd() const
    {
        return index_ >= arguments_.size();
    }

    /** @return The next argument, consumed. */
    const std::string &Next()
    {
        return arguments_[index_++];
    }

    /**
     * The value after an option.
     * @param option The option's name, for the error.
     * @return The value, or why there is none.
     */
    Result<std::string> Value(const std::string &option)
    {
        if (AtEnd() || IsOptionName(arguments_[index_]))
        {
            return Error{"-" + option + " needs a value"};
        }
        return Next();
    }

    /** @return Every value up to the next option, consumed. */
    std::vector<std::string> Values()
    {
        std::vector<std::string> values;
        while (!AtEnd() && !IsOptionName(arguments_[index_]))
        {
            values.push_back(Next());
        }
        return values;
    }

    /**
     * Every key=value up to the next option.
     * @param option The option's name, for the error.
     * @return The pairs, or why one is not a pair.
     */
    Result<KeyValues> Pairs(const std::string &option)
    {
        KeyValues pairs;
        for (const auto &value : Values())
        {
            const auto equals = value.find('=');
            if (equals == std::string::npos && value == "ponder")
            {
                pairs.emplace_back(value, "true"); // The one key that may stand alone.
                continue;
            }
            if (equals == std::string::npos || equals == 0)
            {
                return Error{"-" + option + " takes key=value pairs, not '" + value + "'"};
            }
            pairs.emplace_back(value.substr(0, equals), value.substr(equals + 1));
        }
        return pairs;
    }

  private:
    const std::vector<std::string> &arguments_; ///< The arguments.
    std::size_t                     index_ = 0; ///< The next to read.
};

/** What one engine's key=value pairs said. */
struct EngineSpec
{
    uci::EngineConfig                 config;         ///< The engine.
    std::optional<chess::TimeControl> time_control;   ///< Its own time control, if it set any limit.
    std::string                       book_path;      ///< Its own Polyglot book's file, if any.
    int                               book_depth = 0; ///< Its book's depth in plies; 0 for no limit.
};

/**
 * Apply an engine's key=value pairs.
 * @param pairs The pairs (-each's first, then the engine's own).
 * @param global The tournament's time control, which an engine's own limits start from.
 * @return The engine, or what is wrong with a pair.
 */
Result<EngineSpec> BuildEngine(const KeyValues &pairs, const chess::TimeControl &global)
{
    EngineSpec spec;
    for (const auto &[key, value] : pairs)
    {
        const auto limits = [&]() -> chess::TimeControl & {
            if (!spec.time_control)
            {
                spec.time_control = global;
            }
            return *spec.time_control;
        };
        if (key == "cmd")
        {
            spec.config.executable = value;
        }
        else if (key == "name")
        {
            spec.config.name = value;
        }
        else if (key == "dir")
        {
            spec.config.working_directory = value;
        }
        else if (key == "arg")
        {
            spec.config.arguments.push_back(value);
        }
        else if (key.rfind("option.", 0) == 0 && key.size() > 7)
        {
            spec.config.options.emplace_back(key.substr(7), value);
        }
        else if (key == "tc")
        {
            const auto parsed = chess::TimeControl::Parse(value);
            if (!parsed)
            {
                return Error{parsed.ErrorMessage()};
            }
            limits().sessions = parsed->sessions;
        }
        else if (key == "st")
        {
            const auto time = ParseSecondsArgument(value, "st");
            if (!time)
            {
                return Error{time.ErrorMessage()};
            }
            limits().move_time = *time;
        }
        else if (key == "depth")
        {
            const auto depth = ParseNumber(value, "depth", 1);
            if (!depth)
            {
                return Error{depth.ErrorMessage()};
            }
            limits().depth = static_cast<int>(*depth);
        }
        else if (key == "nodes")
        {
            const auto nodes = ParseNumber(value, "nodes", 1);
            if (!nodes)
            {
                return Error{nodes.ErrorMessage()};
            }
            limits().nodes = *nodes;
        }
        else if (key == "book")
        {
            spec.book_path = value;
        }
        else if (key == "bookdepth")
        {
            const auto depth = ParseNumber(value, "bookdepth", 1);
            if (!depth)
            {
                return Error{depth.ErrorMessage()};
            }
            spec.book_depth = static_cast<int>(*depth);
        }
        else if (key == "ponder")
        {
            if (value != "true" && value != "false")
            {
                return Error{"ponder is a flag ('ponder', or ponder=true or ponder=false), not ponder=" + value};
            }
            spec.config.does_ponder = value == "true";
        }
        else if (key == "proto")
        {
            if (value != "uci")
            {
                return Error{"only UCI engines are supported, not proto=" + value};
            }
        }
        else
        {
            return Error{"unknown engine key '" + key + "'"};
        }
    }
    if (spec.config.executable.empty())
    {
        return Error{"an engine needs cmd=<program>"};
    }
    return spec;
}

/**
 * The first engine's score in a match so far.
 * @param results The finished games.
 * @return Its wins, draws and losses.
 */
match::ScoreLine FirstEngineScore(const std::vector<match::GameResultEntry> &results)
{
    match::ScoreLine score;
    for (const auto &game : results)
    {
        if (game.result == chess::GameResult::kNone)
        {
            continue;
        }
        if (game.result == chess::GameResult::kDraw)
        {
            ++score.draws;
            continue;
        }
        const bool is_white_win = game.result == chess::GameResult::kWhiteWins;
        const bool is_first_win = (game.white == 0) == is_white_win;
        ++(is_first_win ? score.wins : score.losses);
    }
    return score;
}

/**
 * A principal variation in SAN, as far as its moves are legal.
 * @param position The position it starts from.
 * @param pv Its moves, in UCI notation.
 * @return The moves in SAN, separated by spaces.
 */
std::string PvInSan(chess::Position position, const std::vector<std::string> &pv)
{
    std::string text;
    for (const auto &uci : pv)
    {
        const auto move = chess::ParseUciMove(position, uci);
        if (!move)
        {
            break;
        }
        text += (text.empty() ? "" : " ") + chess::MoveToSan(position, *move);
        position = position.MakeMove(*move);
    }
    return text;
}

/**
 * Run perft.
 * @param arguments The position ("startpos" or a FEN), the depth, and -divide.
 * @param out Where the counts go.
 * @param err Where errors go.
 * @return The exit code.
 */
int RunPerft(const std::vector<std::string> &arguments, std::ostream &out, std::ostream &err)
{
    std::vector<std::string> words;
    bool                     is_divide = false;
    for (const auto &argument : arguments)
    {
        if (argument == "-divide" || argument == "--divide")
        {
            is_divide = true;
        }
        else
        {
            words.push_back(argument);
        }
    }
    if (words.size() < 2)
    {
        err << "uci-host-cli: perft takes a position (startpos or a FEN) and a depth\n";
        return 2;
    }
    const auto depth = ParseNumber(words.back(), "perft's depth", 0);
    if (!depth)
    {
        err << "uci-host-cli: " << depth.ErrorMessage() << '\n';
        return 2;
    }
    words.pop_back();
    std::string fen;
    for (const auto &word : words)
    {
        fen += (fen.empty() ? "" : " ") + word;
    }
    const auto position = fen == "startpos" ? Result<chess::Position>(chess::Position::Start())
                                            : chess::Position::FromFen(fen, chess::FenMode::kTolerant);
    if (!position)
    {
        err << "uci-host-cli: " << position.ErrorMessage() << '\n';
        return 2;
    }
    const auto    start = Clock::now();
    std::uint64_t nodes = 0;
    if (is_divide && *depth > 0)
    {
        for (const auto &[move, count] : chess::PerftDivide(*position, static_cast<int>(*depth)))
        {
            out << move.ToUci() << ": " << count << '\n';
            nodes += count;
        }
    }
    else
    {
        nodes = chess::Perft(*position, static_cast<int>(*depth));
    }
    const auto milliseconds =
        std::max<long long>(1, std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
    out << "Nodes: " << nodes << '\n';
    out << "Time: " << milliseconds << " ms\n";
    out << "NPS: " << static_cast<long long>(nodes * 1000.0 / milliseconds) << '\n';
    return 0;
}

/**
 * Run a match or a tournament.
 * @param arguments The arguments after the subcommand.
 * @param is_match Whether it is a match.
 * @param out Where progress and results go.
 * @param err Where errors and warnings go.
 * @param stop Set to stop early, may be null.
 * @return The exit code.
 */
int RunTournament(const std::vector<std::string> &arguments, bool is_match, std::ostream &out, std::ostream &err,
                  const std::atomic<bool> *stop)
{
    auto options = ParseTournamentArguments(arguments, is_match);
    if (!options)
    {
        err << "uci-host-cli: " << options.ErrorMessage() << '\n';
        return 2;
    }
    for (const auto &warning : options->warnings)
    {
        err << "Warning: " << warning << '\n';
    }
    std::mutex log_mutex;
    if (options->is_debug)
    {
        options->settings.engine_logger = [&err, &log_mutex](int game, const std::string &engine,
                                                             uci::LineDirection direction, std::string_view line) {
            std::lock_guard lock(log_mutex);
            err << game << ' ' << engine << (direction == uci::LineDirection::kToEngine ? " < " : " > ") << line
                << '\n';
        };
    }
    match::Tournament tournament(options->settings);
    const auto        started = tournament.Start();
    if (!started)
    {
        err << "uci-host-cli: " << started.ErrorMessage() << '\n';
        return 1;
    }
    bool has_requested_stop = false;
    while (true)
    {
        if (stop != nullptr && stop->load() && !has_requested_stop)
        {
            err << "Stopping: abandoning the games in progress\n";
            tournament.RequestStop();
            has_requested_stop = true;
        }
        const auto event = tournament.Events().PopFor(std::chrono::milliseconds(100));
        if (!event)
        {
            if (tournament.Events().IsDrained())
            {
                break;
            }
            continue;
        }
        switch (event->kind)
        {
        case match::TournamentEvent::Kind::kGameStarted:
            out << "Started game " << event->game_number << " of " << event->total_games << " (" << event->white_name
                << " vs " << event->black_name << ")\n";
            break;
        case match::TournamentEvent::Kind::kGameFinished: {
            out << "Finished game " << event->game_number << " (" << event->white_name << " vs " << event->black_name
                << "): " << chess::ResultText(event->outcome.result) << " {" << event->outcome.description << "}\n";
            if (is_match)
            {
                const auto names = tournament.EngineNames();
                out << match::FormatMatchScore(names[0], names[1], FirstEngineScore(tournament.Results()));
            }
            break;
        }
        case match::TournamentEvent::Kind::kWarning:
            err << "Warning: " << event->message << '\n';
            break;
        case match::TournamentEvent::Kind::kFinished:
            break;
        }
        out.flush();
    }
    tournament.Wait();
    const auto names   = tournament.EngineNames();
    const auto results = tournament.Results();
    if (is_match && results.empty())
    {
        out << match::FormatMatchScore(names[0], names[1], FirstEngineScore(results));
    }
    else if (!is_match)
    {
        const auto standings = match::ComputeStandings(names, results);
        out << '\n' << match::FormatStandings(standings) << '\n';
        out << match::FormatCrosstable(names, standings,
                                       match::Crosstable::Build(static_cast<int>(names.size()), results));
    }
    out << "Finished " << (is_match ? "match" : "tournament") << '\n';
    return 0;
}

/**
 * Run an engine on a position and print what it says.
 * @param arguments The arguments after the subcommand.
 * @param out Where the analysis goes.
 * @param err Where errors go.
 * @return The exit code.
 */
int RunAnalyse(const std::vector<std::string> &arguments, std::ostream &out, std::ostream &err)
{
    const auto options = ParseAnalyseArguments(arguments);
    if (!options)
    {
        err << "uci-host-cli: " << options.ErrorMessage() << '\n';
        return 2;
    }
    chess::Position position = chess::Position::Start();
    if (!options->fen.empty())
    {
        const auto parsed = chess::Position::FromFen(options->fen, chess::FenMode::kTolerant);
        if (!parsed)
        {
            err << "uci-host-cli: " << parsed.ErrorMessage() << '\n';
            return 2;
        }
        position = *parsed;
    }
    for (const auto &text : options->moves)
    {
        const auto move = chess::ParseUciMove(position, text);
        if (!move)
        {
            err << "uci-host-cli: " << move.ErrorMessage() << '\n';
            return 2;
        }
        position = position.MakeMove(*move);
    }
    uci::Engine engine(options->engine);
    const auto  started = engine.Start();
    if (!started)
    {
        err << "uci-host-cli: " << started.ErrorMessage() << '\n';
        return 1;
    }
    for (const auto &warning : engine.Warnings())
    {
        err << "Warning: " << warning << '\n';
    }
    out << "Engine: " << engine.Name() << (engine.Author().empty() ? "" : " by " + engine.Author()) << '\n';
    out << "Position: " << position.ToFen() << '\n';
    if (options->multipv > 1 && engine.FindOption("MultiPV") != nullptr)
    {
        engine.SetOption("MultiPV", std::to_string(options->multipv));
    }
    engine.NewGame();
    engine.SetPosition(options->fen, options->moves);
    uci::GoParameters parameters;
    if (options->depth)
    {
        parameters.depth = options->depth;
    }
    else
    {
        parameters.is_infinite = true;
    }
    engine.Go(parameters);
    const auto deadline     = Clock::now() + options->time;
    bool       has_stopped  = false;
    const auto stop_waiting = Clock::now() + options->time + std::chrono::seconds(3);
    while (Clock::now() < stop_waiting)
    {
        if (!has_stopped && Clock::now() >= deadline)
        {
            engine.Stop();
            has_stopped = true;
        }
        const auto event = engine.NextEvent(std::chrono::milliseconds(50));
        if (!event)
        {
            continue;
        }
        if (event->kind == uci::EngineEventKind::kInfo)
        {
            const auto &info = event->info;
            if (info.text)
            {
                out << "info string " << *info.text << '\n';
            }
            else if (info.depth && info.score)
            {
                out << "depth " << *info.depth;
                if (info.selective_depth)
                {
                    out << '/' << *info.selective_depth;
                }
                if (info.multipv)
                {
                    out << " line " << *info.multipv;
                }
                out << " score " << info.score->ToString();
                if (info.nodes)
                {
                    out << " nodes " << *info.nodes;
                }
                if (info.nodes_per_second)
                {
                    out << " nps " << *info.nodes_per_second;
                }
                if (info.time)
                {
                    out << " time " << *info.time;
                }
                if (!info.pv.empty())
                {
                    out << " pv " << PvInSan(position, info.pv);
                }
                out << '\n';
            }
        }
        else if (event->kind == uci::EngineEventKind::kBestMove)
        {
            const auto move = chess::ParseUciMove(position, event->best_move.move);
            out << "bestmove " << event->best_move.move;
            if (move)
            {
                out << " (" << chess::MoveToSan(position, *move) << ")";
            }
            out << '\n';
            return 0;
        }
        else if (event->kind == uci::EngineEventKind::kExited)
        {
            err << "uci-host-cli: the engine exited during the analysis\n";
            return 1;
        }
    }
    err << "uci-host-cli: the engine did not answer 'stop'\n";
    return 1;
}

} // namespace

std::string UsageText()
{
    return R"(Reckless UCI Host - matches, tournaments and analysis with UCI chess engines.

Usage:
  uci-host-cli match      -engine <keys> -engine <keys> [options]
  uci-host-cli tournament -engine <keys> -engine <keys> [-engine <keys>...] [options]
  uci-host-cli perft      <startpos|FEN> <depth> [-divide]
  uci-host-cli analyse    -engine <keys> [-fen <startpos|FEN>] [-moves <uci>...] [-time <s>] [-depth <n>]
                          [-multipv <n>]
  uci-host-cli --version | --help

Engine keys (-engine for one engine, -each for every engine, the engine's own winning):
  cmd=<program>         the engine to run (required)
  name=<name>           the name to record (default: the engine's own)
  dir=<directory>       its working directory
  arg=<argument>        an argument to start it with (repeatable)
  option.<name>=<value> a UCI option to set
  tc=<control>          its own time control (as -tc); st=<s>, depth=<n>, nodes=<n> as below
  book=<file.bin>       its own Polyglot book: the host plays the book's move for it while the book has one
  bookdepth=<n>         book moves only in the game's first n plies
  ponder                let it think on the opponent's time (its Ponder option set; ponderhit or stop on the reply)
  proto=uci             accepted; UCI is the only protocol

Match and tournament options:
  -tc <control>         time control: [moves/]time[+increment], times in seconds or m:ss,
                        sessions separated by commas: 40/60+0.6, 10+0.1, 40/120,20/60,15+30
  -st <s>               a fixed time per move     -depth <n>   a depth limit     -nodes <n>   a node limit
  -games <n>            games per encounter, colours alternating (default 1)
  -rounds <n>           rounds: cycles of every engine meeting every other (default 1)
  -double               play every cycle twice, colours reversed (a double round robin)
  -repeat               play each opening twice in a row, colours swapped
  -concurrency <n>      games played at once (default 1)
  -openings file=<f> format=epd|pgn|polyglot [order=sequential|random] [plies=<n>] [start=<n>]
                        a Polyglot book (.bin) is walked from the start, weighted at random, to plies (8),
                        one line for each pair of colour-swapped games
  -srand <n>            the seed for random opening order and book moves (default 1)
  -pgnout <file>        append every finished game to a PGN file
  -resign movecount=<n> score=<cp> [twosided=true]
  -draw movenumber=<n> movecount=<n> score=<cp>
  -maxmoves <n>         adjudicate a draw after n moves by each side
  -margin <ms>          how far past its time an engine may go before its flag falls (default 0)
  -stall <s>            how long to wait for a move with no clock (default 60)
  -event <name>         PGN's Event     -site <name>   PGN's Site
  -debug                print every line exchanged with the engines on standard error
)";
}

Result<TournamentOptions> ParseTournamentArguments(const std::vector<std::string> &arguments, bool is_match)
{
    TournamentOptions      options;
    auto                  &settings = options.settings;
    std::vector<KeyValues> engine_pairs;
    KeyValues              each_pairs;
    KeyValues              opening_pairs;
    std::uint64_t          seed = 1;
    ArgumentReader         reader(arguments);
    while (!reader.AtEnd())
    {
        const std::string &argument = reader.Next();
        if (!IsOptionName(argument))
        {
            return Error{"unexpected '" + argument + "'"};
        }
        const std::string name   = OptionName(argument);
        const auto        number = [&](long long minimum) -> Result<long long> {
            const auto value = reader.Value(name);
            return value ? ParseNumber(*value, "-" + name, minimum) : Result<long long>(Error{value.ErrorMessage()});
        };
        const auto seconds = [&]() -> Result<chess::Milliseconds> {
            const auto value = reader.Value(name);
            return value ? ParseSecondsArgument(*value, "-" + name)
                         : Result<chess::Milliseconds>(Error{value.ErrorMessage()});
        };
        if (name == "engine" || name == "each" || name == "openings" || name == "resign" || name == "draw")
        {
            auto pairs = reader.Pairs(name);
            if (!pairs)
            {
                return Error{pairs.ErrorMessage()};
            }
            if (name == "engine")
            {
                engine_pairs.push_back(*pairs);
            }
            else if (name == "each")
            {
                each_pairs.insert(each_pairs.end(), pairs->begin(), pairs->end());
            }
            else if (name == "openings")
            {
                opening_pairs = *pairs;
            }
            else
            {
                const bool is_resign = name == "resign";
                if (is_resign)
                {
                    settings.adjudication.resign.is_enabled = true;
                }
                else
                {
                    settings.adjudication.draw.is_enabled = true;
                }
                for (const auto &[key, value] : *pairs)
                {
                    if (key == "twosided" && is_resign)
                    {
                        settings.adjudication.resign.is_two_sided = value == "true" || value == "1";
                        continue;
                    }
                    const auto parsed = ParseNumber(value, "-" + name + " " + key, 0);
                    if (!parsed)
                    {
                        return Error{parsed.ErrorMessage()};
                    }
                    const int count = static_cast<int>(*parsed);
                    if (key == "movecount")
                    {
                        (is_resign ? settings.adjudication.resign.move_count : settings.adjudication.draw.move_count) =
                            count;
                    }
                    else if (key == "score")
                    {
                        (is_resign ? settings.adjudication.resign.score : settings.adjudication.draw.score) = count;
                    }
                    else if (key == "movenumber" && !is_resign)
                    {
                        settings.adjudication.draw.move_number = count;
                    }
                    else
                    {
                        return Error{"-" + name + " has no key '" + key + "'"};
                    }
                }
            }
            continue;
        }
        if (name == "repeat" || name == "double" || name == "debug" || name == "recover")
        {
            if (name == "repeat")
            {
                settings.schedule.is_repeat = true;
                reader.Values(); // A repeat count, which other runners take, is always 2 here.
            }
            else if (name == "double")
            {
                settings.schedule.is_double = true;
            }
            else if (name == "debug")
            {
                options.is_debug = true;
            }
            continue;
        }
        if (name == "tc")
        {
            const auto value = reader.Value(name);
            if (!value)
            {
                return Error{value.ErrorMessage()};
            }
            const auto parsed = chess::TimeControl::Parse(*value);
            if (!parsed)
            {
                return Error{parsed.ErrorMessage()};
            }
            settings.time_control.sessions = parsed->sessions;
        }
        else if (name == "st" || name == "margin" || name == "stall")
        {
            if (name == "margin")
            {
                const auto value = number(0);
                if (!value)
                {
                    return Error{value.ErrorMessage()};
                }
                settings.time_margin = chess::Milliseconds{*value};
                continue;
            }
            const auto time = seconds();
            if (!time)
            {
                return Error{time.ErrorMessage()};
            }
            if (name == "st")
            {
                settings.time_control.move_time = *time;
            }
            else
            {
                settings.stall_timeout = *time;
            }
        }
        else if (name == "depth" || name == "nodes" || name == "games" || name == "rounds" || name == "concurrency" ||
                 name == "maxmoves" || name == "srand")
        {
            const auto value = number(name == "maxmoves" || name == "srand" ? 0 : 1);
            if (!value)
            {
                return Error{value.ErrorMessage()};
            }
            if (name == "depth")
            {
                settings.time_control.depth = static_cast<int>(*value);
            }
            else if (name == "nodes")
            {
                settings.time_control.nodes = *value;
            }
            else if (name == "games")
            {
                settings.schedule.games_per_encounter = static_cast<int>(*value);
            }
            else if (name == "rounds")
            {
                settings.schedule.rounds = static_cast<int>(*value);
            }
            else if (name == "concurrency")
            {
                settings.concurrency = static_cast<int>(*value);
            }
            else if (name == "maxmoves")
            {
                settings.adjudication.max_moves = static_cast<int>(*value);
            }
            else
            {
                seed = static_cast<std::uint64_t>(*value);
            }
        }
        else if (name == "pgnout" || name == "event" || name == "site")
        {
            const auto value = reader.Value(name);
            if (!value)
            {
                return Error{value.ErrorMessage()};
            }
            (name == "pgnout" ? settings.pgn_path : name == "event" ? settings.event : settings.site) = *value;
            if (name == "pgnout")
            {
                reader.Values(); // Other runners' trailing flags (min, fi) are not needed.
            }
        }
        else
        {
            return Error{"unknown option '" + argument + "'"};
        }
    }

    if (is_match && engine_pairs.size() != 2)
    {
        return Error{"a match takes exactly two engines, not " + std::to_string(engine_pairs.size())};
    }
    if (engine_pairs.size() < 2)
    {
        return Error{"a tournament takes at least two engines, not " + std::to_string(engine_pairs.size())};
    }
    std::map<std::string, std::shared_ptr<const chess::PolyglotBook>> books;
    for (const auto &pairs : engine_pairs)
    {
        KeyValues all = each_pairs;
        all.insert(all.end(), pairs.begin(), pairs.end());
        auto spec = BuildEngine(all, settings.time_control);
        if (!spec)
        {
            return Error{spec.ErrorMessage()};
        }
        const chess::TimeControl &limits = spec->time_control ? *spec->time_control : settings.time_control;
        if (limits.IsInfinite())
        {
            return Error{"engine '" + spec->config.executable +
                         "' has no time control or limit: give -tc, -st, -depth or -nodes"};
        }
        match::TournamentEngine engine{spec->config, spec->time_control, nullptr, spec->book_depth};
        if (!spec->book_path.empty())
        {
            // One copy of a book however many engines name it.
            auto &book = books[spec->book_path];
            if (!book)
            {
                auto loaded = chess::PolyglotBook::Load(spec->book_path);
                if (!loaded)
                {
                    return Error{loaded.ErrorMessage()};
                }
                book = std::make_shared<const chess::PolyglotBook>(std::move(*loaded));
            }
            engine.book = book;
        }
        settings.engines.push_back(std::move(engine));
    }

    settings.seed = seed;
    if (!opening_pairs.empty())
    {
        std::string          file;
        match::OpeningFormat format     = match::OpeningFormat::kEpd;
        match::OpeningOrder  order      = match::OpeningOrder::kSequential;
        int                  plies      = 0;
        int                  start      = 0;
        bool                 has_format = false;
        for (const auto &[key, value] : opening_pairs)
        {
            if (key == "file")
            {
                file = value;
            }
            else if (key == "format" && (value == "epd" || value == "pgn" || value == "polyglot"))
            {
                format     = value == "epd"   ? match::OpeningFormat::kEpd
                             : value == "pgn" ? match::OpeningFormat::kPgn
                                              : match::OpeningFormat::kPolyglot;
                has_format = true;
            }
            else if (key == "order" && (value == "sequential" || value == "random"))
            {
                order = value == "random" ? match::OpeningOrder::kRandom : match::OpeningOrder::kSequential;
            }
            else if (key == "plies" || key == "start")
            {
                const auto parsed = ParseNumber(value, "-openings " + key, key == "start" ? 1 : 0);
                if (!parsed)
                {
                    return Error{parsed.ErrorMessage()};
                }
                (key == "plies" ? plies : start) = static_cast<int>(*parsed);
            }
            else
            {
                return Error{"-openings has no '" + key + "=" + value + "'"};
            }
        }
        if (file.empty())
        {
            return Error{"-openings needs file=<path>"};
        }
        if (!has_format)
        {
            const auto dot       = file.rfind('.');
            const auto extension = dot == std::string::npos ? std::string{} : file.substr(dot);
            format               = extension == ".pgn" || extension == ".PGN"   ? match::OpeningFormat::kPgn
                                   : extension == ".bin" || extension == ".BIN" ? match::OpeningFormat::kPolyglot
                                                                                : match::OpeningFormat::kEpd;
        }
        if (format == match::OpeningFormat::kPolyglot)
        {
            // A book is walked afresh for each opening number, to plies (8 unless given), not read as a list.
            auto book = chess::PolyglotBook::Load(file);
            if (!book)
            {
                return Error{book.ErrorMessage()};
            }
            settings.opening_book       = std::make_shared<const chess::PolyglotBook>(std::move(*book));
            settings.opening_book_plies = plies > 0 ? plies : 8;
            settings.seed               = seed;
            return options;
        }
        auto openings = match::LoadOpenings(file, format, plies);
        if (!openings)
        {
            return Error{openings.ErrorMessage()};
        }
        match::OrderOpenings(*openings, order, seed, start > 0 ? start - 1 : 0);
        settings.openings = std::move(*openings);
    }
    return options;
}

Result<AnalyseOptions> ParseAnalyseArguments(const std::vector<std::string> &arguments)
{
    AnalyseOptions options;
    bool           has_engine = false;
    ArgumentReader reader(arguments);
    while (!reader.AtEnd())
    {
        const std::string &argument = reader.Next();
        if (!IsOptionName(argument))
        {
            return Error{"unexpected '" + argument + "'"};
        }
        const std::string name = OptionName(argument);
        if (name == "engine")
        {
            const auto pairs = reader.Pairs(name);
            if (!pairs)
            {
                return Error{pairs.ErrorMessage()};
            }
            const auto spec = BuildEngine(*pairs, {});
            if (!spec)
            {
                return Error{spec.ErrorMessage()};
            }
            options.engine = spec->config;
            has_engine     = true;
        }
        else if (name == "fen")
        {
            std::string fen;
            for (const auto &word : reader.Values())
            {
                fen += (fen.empty() ? "" : " ") + word;
            }
            options.fen = fen == "startpos" ? std::string{} : fen;
        }
        else if (name == "moves")
        {
            options.moves = reader.Values();
        }
        else if (name == "time")
        {
            const auto value = reader.Value(name);
            if (!value)
            {
                return Error{value.ErrorMessage()};
            }
            const auto time = ParseSecondsArgument(*value, "-time");
            if (!time)
            {
                return Error{time.ErrorMessage()};
            }
            options.time = *time;
        }
        else if (name == "depth" || name == "multipv")
        {
            const auto value = reader.Value(name);
            if (!value)
            {
                return Error{value.ErrorMessage()};
            }
            const auto parsed = ParseNumber(*value, "-" + name, 1);
            if (!parsed)
            {
                return Error{parsed.ErrorMessage()};
            }
            if (name == "depth")
            {
                options.depth = static_cast<int>(*parsed);
            }
            else
            {
                options.multipv = static_cast<int>(*parsed);
            }
        }
        else
        {
            return Error{"unknown option '" + argument + "'"};
        }
    }
    if (!has_engine)
    {
        return Error{"analyse needs -engine cmd=<program>"};
    }
    return options;
}

int RunCli(const std::vector<std::string> &arguments, std::ostream &out, std::ostream &err,
           const std::atomic<bool> *stop)
{
    if (arguments.size() < 2)
    {
        err << UsageText();
        return 2;
    }
    const std::string             &command = arguments[1];
    const std::vector<std::string> rest(arguments.begin() + 2, arguments.end());
    if (command == "--help" || command == "-h" || command == "help")
    {
        out << UsageText();
        return 0;
    }
    if (command == "--version" || command == "-v" || command == "version")
    {
        out << "Reckless UCI Host " << VersionText() << '\n';
        return 0;
    }
    if (command == "perft")
    {
        return RunPerft(rest, out, err);
    }
    if (command == "match" || command == "tournament")
    {
        return RunTournament(rest, command == "match", out, err, stop);
    }
    if (command == "analyse" || command == "analyze")
    {
        return RunAnalyse(rest, out, err);
    }
    err << "uci-host-cli: unknown command '" << command << "'\n\n" << UsageText();
    return 2;
}

} // namespace uci_host::cli
