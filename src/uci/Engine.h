#pragma once
/// @file Engine.h A UCI engine session: the process, the handshake, the options, and searches.
///
/// Threading: an Engine is driven from one thread, its owner - every method is the owner's to call. The engine's
/// output is read on the child process's own reader threads into a thread-safe queue, and parsed into events on the
/// owner's thread as the owner drains it, with NextEvent (waiting up to a timeout), PollEvent (never waiting) or a
/// blocking Search. The GUI will poll from its frame callback; a game runner on its own thread blocks in Search.
///
/// Robustness: every wait has a deadline; a line the engine writes that is not UCI is an event of its own and harms
/// nothing; an engine that exits is an event (kExited) once its output is drained; a destroyed Engine quits its
/// process, and kills it if it does not exit in the configured time.

#include "EngineConfig.h"
#include "Process.h"
#include "UciProtocol.h"
#include "chess/Result.h"

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace amphitheater::uci
{

/** Which way a logged line went. */
enum class LineDirection
{
    kToEngine,        ///< A command the host wrote.
    kFromEngine,      ///< A line the engine wrote on its standard output.
    kFromEngineError, ///< A line the engine wrote on its standard error.
};

/** Receives every line exchanged with an engine, on the owner's thread. */
using EngineLogger = std::function<void(LineDirection direction, std::string_view line)>;

/** The kinds of event an engine's output becomes. */
enum class EngineEventKind
{
    kInfo,        ///< An "info" line.
    kBestMove,    ///< A "bestmove" line: a search has ended.
    kReadyOk,     ///< "readyok".
    kOther,       ///< Any other line on standard output: "id", "option", "uciok" out of turn, or garbage.
    kErrorOutput, ///< A line on standard error.
    kExited,      ///< The engine's output has ended: the process has exited or closed it.
};

/** One event from an engine. */
struct EngineEvent
{
    EngineEventKind                       kind = EngineEventKind::kOther; ///< What it is.
    std::string                           line;      ///< The line it came from (empty for kExited).
    SearchInfo                            info;      ///< For kInfo: the line's fields.
    BestMove                              best_move; ///< For kBestMove: the move.
    std::chrono::steady_clock::time_point received;  ///< When the line was read from the engine.
};

/** How a blocking search ended. */
struct SearchResult
{
    /** Why the search ended. */
    enum class Ending
    {
        kBestMove, ///< The engine answered with "bestmove".
        kTimeout,  ///< The deadline passed with no answer.
        kExited,   ///< The engine exited (crashed or quit) without answering.
    };

    Ending              ending = Ending::kBestMove; ///< Why it ended.
    BestMove            best_move;                  ///< The answer, for kBestMove.
    SearchInfo          info;    ///< The search's last word: the latest of each field of the first line's infos.
    chess::Milliseconds elapsed; ///< From "go" being written to the answer being read (or the deadline).
};

/** A session with one UCI engine. */
class Engine
{
  public:
    /**
     * An engine not yet started.
     * @param config How to start it and set it up.
     */
    explicit Engine(EngineConfig config);

    /** Quits the engine, killing it if it does not exit in time. */
    ~Engine();

    Engine(const Engine &)            = delete;
    Engine &operator=(const Engine &) = delete;

    /**
     * Start the engine: the process, "uci" and its answer up to "uciok" (its name, author and options), the
     * configured options, and "isready" up to "readyok". An option the engine does not declare, or a value that does
     * not suit it, is skipped with a warning (Warnings) rather than failing the start.
     * @return Ok, or why the engine could not be started.
     */
    Status Start();

    /** @return The name to show: the configured one, else the engine's own, else the program's file name. */
    const std::string &Name() const
    {
        return name_;
    }

    /** @return The author the engine gave ("id author"), or empty. */
    const std::string &Author() const
    {
        return author_;
    }

    /** @return The options the engine declared. */
    const std::vector<EngineOption> &Options() const
    {
        return options_;
    }

    /** @return What went wrong without stopping the start: options skipped, option lines that did not parse. */
    const std::vector<std::string> &Warnings() const
    {
        return warnings_;
    }

    /** @return The configuration. */
    const EngineConfig &Config() const
    {
        return config_;
    }

    /**
     * An option the engine declared, by name; UCI's option names compare without regard to case.
     * @param name The name.
     * @return The option, or null.
     */
    const EngineOption *FindOption(std::string_view name) const;

    /**
     * Set an option the engine declared ("setoption").
     * @param name Its name.
     * @param value Its value; empty for a button.
     * @return Ok, or why not (no such option, a value that does not suit it, an engine that is gone).
     */
    Status SetOption(std::string_view name, std::string_view value);

    /**
     * Send "isready" and wait for "readyok". Events that arrive meanwhile are kept for NextEvent.
     * @param timeout How long to wait; the configured ready timeout when not given.
     * @return Ok, or why not (the time ran out, or the engine exited).
     */
    Status WaitReady(std::optional<chess::Milliseconds> timeout = std::nullopt);

    /**
     * Send "ucinewgame", then wait for the engine to be ready.
     * @return Ok, or why not.
     */
    Status NewGame();

    /**
     * Send "position".
     * @param fen The starting position's FEN, or empty for the starting position.
     * @param moves The moves since, in UCI notation.
     * @return Whether the engine took the line.
     */
    bool SetPosition(std::string_view fen, const std::vector<std::string> &moves);

    /**
     * Send "go"; the search's events follow through NextEvent.
     * @param parameters What to search for.
     * @return Whether the engine took the line.
     */
    bool Go(const GoParameters &parameters);

    /** @return Whether the engine took "stop". */
    bool Stop();

    /** @return Whether the engine took "ponderhit". */
    bool PonderHit();

    /**
     * Run a search to its end: "go", then the events up to "bestmove", the deadline, or the engine's exit.
     * @param parameters What to search for.
     * @param deadline When to stop waiting (the search is then left running: Stop it, or kill the engine).
     * @param on_info Called with each "info" line's fields, may be empty.
     * @return How it ended.
     */
    SearchResult Search(const GoParameters &parameters, std::chrono::steady_clock::time_point deadline,
                        const std::function<void(const SearchInfo &)> &on_info = {});

    /**
     * Wait for a search already under way to answer: after "ponderhit", or after "stop" to collect the answer.
     * @param start When the engine's time began, from which the elapsed time is measured.
     * @param deadline When to stop waiting.
     * @param on_info Called with each "info" line's fields, may be empty.
     * @return How it ended.
     */
    SearchResult AwaitBestMove(std::chrono::steady_clock::time_point          start,
                               std::chrono::steady_clock::time_point          deadline,
                               const std::function<void(const SearchInfo &)> &on_info = {});

    /**
     * The next event, waiting for one up to a timeout.
     * @param timeout How long to wait.
     * @return The event, or nothing when the time ran out (or the engine's exit was reported already).
     */
    std::optional<EngineEvent> NextEvent(chess::Milliseconds timeout);

    /** @return The next event if one is waiting, without waiting. */
    std::optional<EngineEvent> PollEvent();

    /** @return Whether a search started by Go or Search has not yet answered. */
    bool IsSearching() const
    {
        return is_searching_;
    }

    /** @return Whether the process is running. */
    bool IsRunning() const;

    /** @return The process's exit code, once it has exited. */
    std::optional<int> ExitCode() const;

    /** Send "stop" if searching and "quit", wait the configured time, then kill the process if it is still there. */
    void Quit();

    /** Kill the process at once. */
    void Kill();

    /**
     * Receive every line exchanged, in both directions, on the owner's thread.
     * @param logger The receiver; empty for none.
     */
    void SetLogger(EngineLogger logger)
    {
        logger_ = std::move(logger);
    }

  private:
    /**
     * Write a command, logging it.
     * @param line The command.
     * @return Whether the engine took it.
     */
    bool Send(std::string_view line);

    /**
     * The next event, waiting up to a deadline: kept events first, then the engine's output.
     * @param deadline When to stop waiting.
     * @return The event, or nothing.
     */
    std::optional<EngineEvent> NextEventUntil(std::chrono::steady_clock::time_point deadline);

    /**
     * An output line as an event, logged.
     * @param line The line.
     * @return The event.
     */
    EngineEvent ToEvent(ProcessLine line);

    EngineConfig                  config_;                ///< How to start it.
    std::unique_ptr<ChildProcess> process_;               ///< The process, once started.
    std::string                   name_;                  ///< The name to show.
    std::string                   author_;                ///< The engine's author.
    std::vector<EngineOption>     options_;               ///< The options it declared.
    std::vector<std::string>      warnings_;              ///< What went wrong without stopping the start.
    std::deque<EngineEvent>       kept_events_;           ///< Events read while waiting for something else.
    EngineLogger                  logger_;                ///< Receives every line.
    bool                          is_searching_  = false; ///< Whether a search has not yet answered.
    bool                          has_told_exit_ = false; ///< Whether kExited has been returned.
};

} // namespace amphitheater::uci
