// The child process on Linux and macOS: posix_spawn, pipes and a process group (Process.h).
#ifndef _WIN32

#include "Process.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char **environ;

namespace amphitheater::uci
{

namespace
{

/** Serialises making pipes and spawning, so no child started here inherits another's pipe before it is marked. */
std::mutex spawn_mutex;

/**
 * Make a pipe whose two ends close on exec.
 * @param ends The read end then the write end.
 * @return Whether the pipe was made.
 */
bool MakePipe(int ends[2])
{
    if (pipe(ends) != 0)
    {
        return false;
    }
    for (int end = 0; end < 2; ++end)
    {
        // A host started with its standard streams closed gets them back as pipe ends; one numbered 0, 1 or 2
        // would make the child's dup2 onto it a no-op that leaves close-on-exec set. Every end goes to 3 or above.
        if (ends[end] <= STDERR_FILENO)
        {
            const int moved = fcntl(ends[end], F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
            close(ends[end]);
            ends[end] = moved;
        }
        else
        {
            fcntl(ends[end], F_SETFD, FD_CLOEXEC);
        }
    }
    return ends[0] >= 0 && ends[1] >= 0;
}

/**
 * Close a descriptor if it is open, and mark it closed.
 * @param descriptor The descriptor, set to -1.
 */
void CloseDescriptor(int &descriptor)
{
    if (descriptor >= 0)
    {
        close(descriptor);
        descriptor = -1;
    }
}

} // namespace

/** The POSIX descriptors of a child, and its reader threads. */
struct ChildProcess::Platform
{
    pid_t              pid     = -1;       ///< The child's process id (and its process group's).
    int                input   = -1;       ///< The write end of its standard input.
    int                wake[2] = {-1, -1}; ///< A pipe whose write wakes the readers from their poll, to give up.
    std::mutex         mutex;              ///< Serialises writes, the close of input, and reaping.
    std::optional<int> exit_code;          ///< The exit code once reaped.
    std::thread        output_reader;      ///< Reads standard output.
    std::thread        error_reader;       ///< Reads standard error.
};

ChildProcess::ChildProcess() : platform_(std::make_unique<Platform>())
{
}

Result<std::unique_ptr<ChildProcess>> ChildProcess::Start(const ProcessOptions &options)
{
    if (options.executable.empty())
    {
        return Error{"the engine's path is empty"};
    }
    // A write to an engine that has died fails with EPIPE instead of killing the host.
    static std::once_flag ignore_sigpipe;
    std::call_once(ignore_sigpipe, [] { std::signal(SIGPIPE, SIG_IGN); });

    // The ChildProcess is made only once the spawn has succeeded, so no ChildProcess ever holds a process that did
    // not start: its destructor kills what it holds, and a pid of -1 there would be kill(-1), every process the user
    // owns.
    pid_t      pid      = -1;
    int        input[2] = {-1, -1}, output[2] = {-1, -1}, error[2] = {-1, -1}, wake[2] = {-1, -1};
    const auto close_all = [&]() {
        for (int *ends : {input, output, error, wake})
        {
            CloseDescriptor(ends[0]);
            CloseDescriptor(ends[1]);
        }
    };

    std::vector<std::string> argument_storage{options.executable};
    argument_storage.insert(argument_storage.end(), options.arguments.begin(), options.arguments.end());
    std::vector<char *> arguments;
    for (auto &argument : argument_storage)
    {
        arguments.push_back(argument.data());
    }
    arguments.push_back(nullptr);

    {
        std::lock_guard lock(spawn_mutex);
        if (!MakePipe(input) || !MakePipe(output) || (options.does_capture_error_output && !MakePipe(error)) ||
            !MakePipe(wake))
        {
            const int code = errno;
            close_all();
            return Error{std::string("cannot make the engine's pipes: ") + std::strerror(code)};
        }
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, input[0], STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO);
        if (options.does_capture_error_output)
        {
            posix_spawn_file_actions_adddup2(&actions, error[1], STDERR_FILENO);
        }
        else
        {
            posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        }
        if (!options.working_directory.empty())
        {
            posix_spawn_file_actions_addchdir_np(&actions, options.working_directory.c_str());
        }
        posix_spawnattr_t attributes;
        posix_spawnattr_init(&attributes);
        sigset_t no_signals, default_signals;
        sigemptyset(&no_signals);
        sigemptyset(&default_signals);
        sigaddset(&default_signals, SIGPIPE);
        posix_spawnattr_setsigmask(&attributes, &no_signals);
        posix_spawnattr_setsigdefault(&attributes, &default_signals);
        posix_spawnattr_setpgroup(&attributes, 0);
        posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
        const bool is_bare_name = options.executable.find('/') == std::string::npos;
        const int  result =
            is_bare_name
                ? posix_spawnp(&pid, options.executable.c_str(), &actions, &attributes, arguments.data(), environ)
                : posix_spawn(&pid, options.executable.c_str(), &actions, &attributes, arguments.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attributes);
        if (result != 0 || pid <= 0)
        {
            close_all();
            return Error{"cannot start '" + options.executable + "': " + std::strerror(result)};
        }
    }
    std::unique_ptr<ChildProcess> child(new ChildProcess());
    Platform                     &platform = *child->platform_;
    platform.pid                           = pid;
    platform.wake[0]                       = wake[0];
    platform.wake[1]                       = wake[1];

    // The child's ends close here, so the pipes end when the child does.
    CloseDescriptor(input[0]);
    CloseDescriptor(output[1]);
    CloseDescriptor(error[1]);
    platform.input = input[1];

    const int  wake_end = platform.wake[0];
    const auto reader   = [wake_end](int descriptor) {
        return [descriptor, wake_end](char *buffer, std::size_t size) -> long {
            while (true)
            {
                pollfd watched[2] = {{descriptor, POLLIN, 0}, {wake_end, POLLIN, 0}};
                if (poll(watched, 2, -1) < 0)
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    return -1;
                }
                if ((watched[1].revents & POLLIN) != 0)
                {
                    return -1;
                }
                const ssize_t count = read(descriptor, buffer, size);
                if (count < 0 && errno == EINTR)
                {
                    continue;
                }
                return static_cast<long>(count);
            }
        };
    };
    child->open_streams_     = options.does_capture_error_output ? 2 : 1;
    ChildProcess *self       = child.get();
    const int     output_end = output[0];
    platform.output_reader   = std::thread([self, output_end, reader] {
        self->ReadLines(ProcessStream::kOutput, reader(output_end));
        close(output_end);
    });
    if (options.does_capture_error_output)
    {
        const int error_end   = error[0];
        platform.error_reader = std::thread([self, error_end, reader] {
            self->ReadLines(ProcessStream::kError, reader(error_end));
            close(error_end);
        });
    }
    return child;
}

ChildProcess::~ChildProcess()
{
    if (IsRunning())
    {
        Kill();
    }
    CloseInput();
    JoinReaders();
    CloseDescriptor(platform_->wake[0]);
    CloseDescriptor(platform_->wake[1]);
}

void ChildProcess::JoinReaders()
{
    // The readers end when the pipes do. A grandchild outside the process group could hold a pipe open, so after a
    // grace period the readers are woken to give up.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!output_.IsClosed() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!output_.IsClosed())
    {
        // One byte wakes them; a wake that cannot be written leaves them to end with their pipes.
        const char wake = 1;
        ssize_t    woken;
        do
        {
            woken = write(platform_->wake[1], &wake, 1);
        } while (woken < 0 && errno == EINTR);
    }
    for (std::thread *reader : {&platform_->output_reader, &platform_->error_reader})
    {
        if (reader->joinable())
        {
            reader->join();
        }
    }
}

bool ChildProcess::WriteLine(std::string_view line)
{
    std::lock_guard lock(platform_->mutex);
    if (platform_->input < 0)
    {
        return false;
    }
    std::string data(line);
    data += '\n';
    const char *next      = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0)
    {
        const ssize_t written = write(platform_->input, next, remaining);
        if (written < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        next += written;
        remaining -= static_cast<std::size_t>(written);
    }
    return true;
}

void ChildProcess::CloseInput()
{
    std::lock_guard lock(platform_->mutex);
    CloseDescriptor(platform_->input);
}

std::optional<int> ChildProcess::ExitCode()
{
    std::lock_guard lock(platform_->mutex);
    if (!platform_->exit_code && platform_->pid > 0)
    {
        int status = 0;
        if (waitpid(platform_->pid, &status, WNOHANG) == platform_->pid)
        {
            platform_->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        }
    }
    return platform_->exit_code;
}

bool ChildProcess::IsRunning()
{
    // A child that never started (its spawn failed, pid -1) is not running: it must never reach Kill's signals, where
    // a pid of -1 or 0 would signal every process the user owns, or the host's own process group.
    return platform_->pid > 0 && !ExitCode().has_value();
}

bool ChildProcess::WaitForExit(std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (IsRunning())
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

void ChildProcess::Kill()
{
    if (platform_->pid <= 0)
    {
        return; // Never started: there is nothing to signal, and kill(-1) or kill(0) would signal far too much.
    }
    if (IsRunning())
    {
        kill(-platform_->pid, SIGKILL); // Its process group, its own children among it.
        kill(platform_->pid, SIGKILL);
    }
    std::lock_guard lock(platform_->mutex);
    if (!platform_->exit_code)
    {
        int status = 0;
        if (waitpid(platform_->pid, &status, 0) == platform_->pid)
        {
            platform_->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        }
    }
}

long ChildProcess::Id() const
{
    return static_cast<long>(platform_->pid);
}

} // namespace amphitheater::uci

#endif
