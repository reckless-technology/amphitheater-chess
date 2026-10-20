#pragma once
/// @file Process.h A child process with pipes to its standard input and output, its lines read on threads of its own.
///
/// Windows: CreateProcessW from UTF-8 converted to UTF-16, the command line quoted as the C runtime parses it, no
/// console window (CREATE_NO_WINDOW), only the child's three pipe ends inherited (a handle list, so a child started at
/// the same moment on another thread never holds this one's pipes open), and the child in a job object that kills it
/// when the job's handle closes - when this object goes, or when the host itself dies, however it dies.
///
/// POSIX: posix_spawn (posix_spawnp for a bare name, searched on PATH), the child in a process group of its own, the
/// pipes close-on-exec in the host, SIGPIPE ignored in the host (a write to an engine that has died fails with EPIPE
/// rather than killing the host) and back at its default in the child. A forced kill is SIGKILL to the whole group.
/// A host that dies leaves its engines reading the end of their input, which a UCI engine takes as quit.
///
/// Two reader threads (output and error) split what the child writes into lines - a CR before the LF dropped, a line
/// longer than a megabyte cut into megabyte pieces - and push them into one queue, which closes once both streams
/// have ended. The owner drains it; nothing else of the object is touched by those threads.

#include "ThreadSafeQueue.h"
#include "chess/Result.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace amphitheater::uci
{

/** What to start. */
struct ProcessOptions
{
    std::string              executable;        ///< The program, UTF-8; a bare name is searched for as the OS does.
    std::vector<std::string> arguments;         ///< Its arguments, UTF-8, each passed whole.
    std::string              working_directory; ///< Where it runs; empty for the host's own.
    bool                     does_capture_error_output = true; ///< Whether stderr's lines join the queue.
};

/** Which of the child's streams a line came from. */
enum class ProcessStream
{
    kOutput, ///< Standard output.
    kError,  ///< Standard error.
};

/** One line the child wrote. */
struct ProcessLine
{
    ProcessStream                         stream = ProcessStream::kOutput; ///< Where it came from.
    std::string                           text;                            ///< The line, without its line ending.
    std::chrono::steady_clock::time_point received;                        ///< When the reader thread read it.
};

/** A running child process. Not copyable; the destructor kills the child if it is still running. */
class ChildProcess
{
  public:
    /**
     * Start a child.
     * @param options What to start.
     * @return The running child, or why it could not start.
     */
    static Result<std::unique_ptr<ChildProcess>> Start(const ProcessOptions &options);

    ~ChildProcess();
    ChildProcess(const ChildProcess &)            = delete;
    ChildProcess &operator=(const ChildProcess &) = delete;

    /**
     * Write a line to the child's standard input, with a line feed after it.
     * @param line The line, without its line ending.
     * @return False when the child no longer reads its input (it has exited, or closed it).
     */
    bool WriteLine(std::string_view line);

    /** Close the child's standard input: the end of its input. */
    void CloseInput();

    /** @return The lines the child writes; closed once both its output streams have ended. */
    ThreadSafeQueue<ProcessLine> &Output()
    {
        return output_;
    }

    /** @return Whether the child is still running. */
    bool IsRunning();

    /** @return The child's exit code once it has exited (on POSIX, 128 plus the signal for a signalled child). */
    std::optional<int> ExitCode();

    /**
     * Wait for the child to exit.
     * @param timeout How long to wait.
     * @return Whether it has exited.
     */
    bool WaitForExit(std::chrono::milliseconds timeout);

    /** Kill the child (and on POSIX its process group) and reap it. */
    void Kill();

    /** @return The child's process id. */
    long Id() const;

  private:
    struct Platform;

    ChildProcess();

    /**
     * Read a stream to its end, pushing its lines.
     * @param stream Which stream it is.
     * @param read Reads up to a buffer's worth, returning the bytes read, 0 at the end, negative on an error.
     */
    void ReadLines(ProcessStream stream, const std::function<long(char *, std::size_t)> &read);

    /** Join the reader threads (after the child has gone and the pipes have closed). */
    void JoinReaders();

    std::unique_ptr<Platform>    platform_;         ///< The OS's handles, and the reader threads.
    ThreadSafeQueue<ProcessLine> output_;           ///< The lines read.
    std::atomic<int>             open_streams_ = 0; ///< Streams still being read; the last to end closes output_.
};

} // namespace amphitheater::uci
