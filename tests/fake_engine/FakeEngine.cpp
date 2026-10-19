// uci_host_fake_engine: a UCI engine for the tests, whose behaviour its arguments script.
//
//   --name NAME            the name it gives (id name)
//   --think MS             how long each search takes, unless stopped (default 0)
//   --score CP             the score it reports (default: the material balance, from the side to move)
//   --mate N               report "score mate N" instead
//   --crash-at N           exit with code 3 when the Nth "go" arrives (counting from 1, across games)
//   --hang-at N            stop answering anything when the Nth "go" arrives
//   --illegal-at N         answer the Nth "go" with an illegal move
//   --garbage              interleave lines that are not UCI, and malformed info and option lines
//   --stderr               write a line to standard error for every command
//   --no-uciok             never answer "uci"
//   --exit-on-uci          exit (code 0) when "uci" arrives
//   --slow-start MS        wait before answering "uci"
//   --hang-on-quit         ignore "quit" (and the end of input), and never exit
//   --empty-bestmove       answer every search with a bare "bestmove"
//   --print-arguments      print every argument, one per line, then exit (code 7)
//   --print-directory      print the working directory, then exit (code 0)
//   --seed N               vary the moves it chooses (default 1)
//   --ponder-move          answer "bestmove X ponder Y", Y the reply it would itself choose after X
//   --ignore-stop          ignore "stop" during a ponder search (only "ponderhit" or the end of input ends it)
//
// It plays the capture of the most valuable piece, else a move chosen by the position's hash and the seed, so a game
// is deterministic. Every setoption is answered with "info string option NAME = VALUE".

#include "chess/Notation.h"
#include "chess/Position.h"
#include "uci/Utf8Arguments.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace
{

using namespace uci_host::chess;

struct Script
{
    std::string        name     = "Fake Engine";
    int                think_ms = 0;
    std::optional<int> score;
    std::optional<int> mate;
    int                crash_at                 = 0;
    int                hang_at                  = 0;
    int                illegal_at               = 0;
    bool               is_garbage               = false;
    bool               does_use_stderr          = false;
    bool               is_without_uciok         = false;
    bool               does_exit_on_uci         = false;
    int                slow_start_ms            = 0;
    bool               does_hang_on_quit        = false;
    bool               does_send_empty_bestmove = false;
    bool               does_send_ponder_move    = false;
    bool               does_ignore_stop         = false;
    unsigned           seed                     = 1;
};

// Input lines, read on a thread of their own so a search can watch for "stop".
class Input
{
  public:
    Input()
    {
        reader_ = std::thread([this] {
            std::string line;
            while (std::getline(std::cin, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                std::lock_guard lock(mutex_);
                lines_.push_back(line);
                ready_.notify_one();
            }
            std::lock_guard lock(mutex_);
            is_ended_ = true;
            ready_.notify_one();
        });
        reader_.detach();
    }

    // The next line, waiting up to a timeout; nothing on timeout or at the end of input.
    std::optional<std::string> Next(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(mutex_);
        ready_.wait_for(lock, timeout, [this] { return !lines_.empty() || is_ended_; });
        if (lines_.empty())
        {
            return std::nullopt;
        }
        std::string line = lines_.front();
        lines_.pop_front();
        return line;
    }

    bool IsEnded()
    {
        std::lock_guard lock(mutex_);
        return is_ended_ && lines_.empty();
    }

  private:
    std::thread             reader_;
    std::mutex              mutex_;
    std::condition_variable ready_;
    std::deque<std::string> lines_;
    bool                    is_ended_ = false;
};

void Say(const std::string &line)
{
    std::cout << line << '\n' << std::flush;
}

// Leave at once: the input thread may be blocked inside getline holding stdin's lock, and the C runtime's exit-time
// cleanup of the standard streams can wait on that lock for ever. What was written is flushed first.
[[noreturn]] void Exit(int code)
{
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(code);
}

[[noreturn]] void HangForever()
{
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int PieceValue(Piece piece)
{
    constexpr int kValues[] = {0, 100, 300, 300, 500, 900, 0};
    return kValues[piece];
}

int Material(const Position &position)
{
    int balance = 0;
    for (int square = 0; square < 64; ++square)
    {
        const Piece piece = position.PieceOn(Square{square});
        if (piece != kNone)
        {
            balance +=
                position.ColorOn(Square{square}) == position.SideToMove() ? PieceValue(piece) : -PieceValue(piece);
        }
    }
    return balance;
}

Move ChooseMove(const Position &position, const std::vector<std::string> &search_moves, unsigned seed)
{
    std::vector<Move> moves;
    for (const Move &move : position.GenerateLegalMoves())
    {
        bool is_allowed = search_moves.empty();
        for (const auto &allowed : search_moves)
        {
            is_allowed |= move.ToUci() == allowed;
        }
        if (is_allowed)
        {
            moves.push_back(move);
        }
    }
    if (moves.empty())
    {
        return Move::None();
    }
    Move best       = Move::None();
    int  best_value = 0;
    for (const Move &move : moves)
    {
        if (PieceValue(move.CapturedPiece()) > best_value)
        {
            best_value = PieceValue(move.CapturedPiece());
            best       = move;
        }
    }
    if (!best.IsNone())
    {
        return best;
    }
    const auto index = (position.hash_ ^ (seed * 0x9E3779B97F4A7C15ULL)) % moves.size();
    return moves[static_cast<std::size_t>(index)];
}

std::string IllegalMove(const Position &position)
{
    const MoveList legal = position.GenerateLegalMoves();
    for (int from = 0; from < 64; ++from)
    {
        for (int to = 0; to < 64; ++to)
        {
            if (from == to)
            {
                continue;
            }
            const std::string text     = Square{from}.ToString() + Square{to}.ToString();
            bool              is_legal = false;
            for (const Move &move : legal)
            {
                is_legal |= move.ToUci() == text;
            }
            if (!is_legal)
            {
                return text;
            }
        }
    }
    return "a1a1";
}

} // namespace

int main(int argc, char **argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY); // LF line endings, as a POSIX engine writes them.
#endif
    Script                   script;
    std::vector<std::string> arguments = uci_host::Utf8Arguments(argc, argv);
    arguments.erase(arguments.begin());
    for (std::size_t index = 0; index < arguments.size(); ++index)
    {
        const std::string &argument = arguments[index];
        const auto         value = [&]() { return index + 1 < arguments.size() ? arguments[++index] : std::string{}; };
        if (argument == "--print-arguments")
        {
            for (const auto &each : arguments)
            {
                Say(each);
            }
            return 7;
        }
        if (argument == "--print-directory")
        {
            const std::u8string directory = std::filesystem::current_path().u8string();
            Say(std::string(directory.begin(), directory.end()));
            return 0;
        }
        if (argument == "--name")
        {
            script.name = value();
        }
        else if (argument == "--think")
        {
            script.think_ms = std::atoi(value().c_str());
        }
        else if (argument == "--score")
        {
            script.score = std::atoi(value().c_str());
        }
        else if (argument == "--mate")
        {
            script.mate = std::atoi(value().c_str());
        }
        else if (argument == "--crash-at")
        {
            script.crash_at = std::atoi(value().c_str());
        }
        else if (argument == "--hang-at")
        {
            script.hang_at = std::atoi(value().c_str());
        }
        else if (argument == "--illegal-at")
        {
            script.illegal_at = std::atoi(value().c_str());
        }
        else if (argument == "--garbage")
        {
            script.is_garbage = true;
        }
        else if (argument == "--stderr")
        {
            script.does_use_stderr = true;
        }
        else if (argument == "--no-uciok")
        {
            script.is_without_uciok = true;
        }
        else if (argument == "--exit-on-uci")
        {
            script.does_exit_on_uci = true;
        }
        else if (argument == "--slow-start")
        {
            script.slow_start_ms = std::atoi(value().c_str());
        }
        else if (argument == "--hang-on-quit")
        {
            script.does_hang_on_quit = true;
        }
        else if (argument == "--empty-bestmove")
        {
            script.does_send_empty_bestmove = true;
        }
        else if (argument == "--ponder-move")
        {
            script.does_send_ponder_move = true;
        }
        else if (argument == "--ignore-stop")
        {
            script.does_ignore_stop = true;
        }
        else if (argument == "--seed")
        {
            script.seed = static_cast<unsigned>(std::atoi(value().c_str()));
        }
    }

    Input    input;
    Position position = Position::Start();
    int      go_count = 0;
    while (true)
    {
        const auto line = input.Next(std::chrono::milliseconds(100));
        if (!line)
        {
            if (input.IsEnded())
            {
                if (script.does_hang_on_quit)
                {
                    HangForever();
                }
                Exit(0);
            }
            continue;
        }
        if (script.does_use_stderr)
        {
            std::cerr << "fake engine read: " << *line << '\n' << std::flush;
        }
        std::istringstream words(*line);
        std::string        command;
        words >> command;
        if (command == "uci")
        {
            if (script.does_exit_on_uci)
            {
                Exit(0);
            }
            if (script.is_without_uciok)
            {
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(script.slow_start_ms));
            Say("id name " + script.name);
            Say("id author The Tests");
            if (script.is_garbage)
            {
                Say("this is not a UCI line");
                Say("option name Broken type nonsense");
                Say("option type spin");
            }
            Say("option name Hash type spin default 16 min 1 max 1024");
            Say("option name Threads type spin default 1 min 1 max 64");
            Say("option name Ponder type check default false");
            Say("option name Style type combo default Normal var Solid var Normal var Risky");
            Say("option name Clear Hash type button");
            Say("option name Book File type string default <empty>");
            Say("option name Name With Spaces type string default a b c");
            Say("uciok");
        }
        else if (command == "isready")
        {
            Say("readyok");
        }
        else if (command == "ucinewgame")
        {
            position = Position::Start();
        }
        else if (command == "setoption")
        {
            const auto        name_at  = line->find(" name ");
            const auto        value_at = line->find(" value ");
            const std::string name =
                name_at == std::string::npos
                    ? ""
                    : line->substr(name_at + 6,
                                   value_at == std::string::npos ? std::string::npos : value_at - name_at - 6);
            const std::string value = value_at == std::string::npos ? "" : line->substr(value_at + 7);
            Say("info string option " + name + " = " + value);
        }
        else if (command == "position")
        {
            std::string kind;
            words >> kind;
            std::string fen;
            std::string word;
            if (kind == "fen")
            {
                while (words >> word && word != "moves")
                {
                    fen += (fen.empty() ? "" : " ") + word;
                }
                auto parsed = Position::FromFen(fen, FenMode::kTolerant);
                position    = parsed ? *parsed : Position::Start();
            }
            else
            {
                position = Position::Start();
                words >> word;
            }
            while (words >> word)
            {
                const auto move = ParseUciMove(position, word);
                if (move)
                {
                    position = position.MakeMove(*move);
                }
            }
        }
        else if (command == "go")
        {
            ++go_count;
            if (go_count == script.crash_at)
            {
                Exit(3);
            }
            if (go_count == script.hang_at)
            {
                HangForever();
            }
            bool                     is_infinite = false;
            bool                     is_ponder   = false;
            std::optional<int>       move_time;
            std::vector<std::string> search_moves;
            std::string              word;
            bool                     is_reading_moves = false;
            while (words >> word)
            {
                if (word == "infinite")
                {
                    is_infinite = true;
                }
                else if (word == "ponder")
                {
                    is_ponder = true;
                }
                else if (word == "movetime")
                {
                    words >> word;
                    move_time = std::atoi(word.c_str());
                }
                else if (word == "searchmoves")
                {
                    is_reading_moves = true;
                }
                else if (is_reading_moves)
                {
                    search_moves.push_back(word);
                }
            }
            const Move        move     = ChooseMove(position, search_moves, script.seed);
            const auto        start    = std::chrono::steady_clock::now();
            const int         material = Material(position);
            const std::string score    = script.mate ? "mate " + std::to_string(*script.mate)
                                                     : "cp " + std::to_string(script.score.value_or(material));
            if (script.is_garbage)
            {
                Say("info depth banana score cp");
                Say("info score mate");
                Say("garbage garbage garbage");
            }
            Say("info depth 1 seldepth 2 multipv 1 score " + score +
                " nodes 10 nps 1000 hashfull 0 tbhits 0 time 0 pv " + move.ToUci());
            Say("info currmove " + move.ToUci() + " currmovenumber 1");
            Say("info string thinking about " + move.ToUci());
            // Think until stopped, or for the scripted time (no longer than a movetime).
            int think = script.think_ms;
            if (move_time && *move_time < think)
            {
                think = *move_time;
            }
            bool is_waiting_for_stop = is_infinite || is_ponder;
            bool is_pondering        = is_ponder;
            auto search_start        = start;
            while (true)
            {
                const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - search_start);
                if (!is_waiting_for_stop && waited.count() >= think)
                {
                    break;
                }
                const auto next = input.Next(is_waiting_for_stop ? std::chrono::milliseconds(50)
                                                                 : std::chrono::milliseconds(think) - waited);
                if (next && *next == "stop")
                {
                    if (is_pondering && script.does_ignore_stop)
                    {
                        continue;
                    }
                    Say(is_pondering ? "info string stopped pondering" : "info string stopped");
                    break;
                }
                if (next && *next == "ponderhit")
                {
                    // The search becomes a real one, its thinking time counted from now.
                    Say("info string ponderhit");
                    is_waiting_for_stop = false;
                    is_pondering        = false;
                    search_start        = std::chrono::steady_clock::now();
                }
                if (next && *next == "quit" && !script.does_hang_on_quit)
                {
                    Exit(0);
                }
                if (input.IsEnded())
                {
                    break;
                }
            }
            Say("info depth 2 seldepth 3 score " + score + " nodes 20 time 1 pv " + move.ToUci());
            if (script.does_send_empty_bestmove)
            {
                Say("bestmove");
            }
            else if (go_count == script.illegal_at)
            {
                Say("bestmove " + IllegalMove(position));
            }
            else if (move.IsNone())
            {
                Say("bestmove (none)");
            }
            else
            {
                std::string answer = "bestmove " + move.ToUci();
                if (script.does_send_ponder_move)
                {
                    const Move reply = ChooseMove(position.MakeMove(move), {}, script.seed);
                    if (!reply.IsNone())
                    {
                        answer += " ponder " + reply.ToUci();
                    }
                }
                Say(answer);
            }
        }
        else if (command == "quit")
        {
            if (script.does_hang_on_quit)
            {
                HangForever();
            }
            Exit(0);
        }
        else if (script.is_garbage)
        {
            Say("Unknown command: " + *line);
        }
    }
}
