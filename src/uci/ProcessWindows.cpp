// The child process on Windows: CreateProcessW, anonymous pipes, a handle list and a job object (Process.h).
#ifdef _WIN32

#include "Process.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <mutex>
#include <thread>

namespace uci_host::uci
{

namespace
{

/**
 * UTF-8 as UTF-16, for the wide Windows calls.
 * @param text UTF-8.
 * @return The UTF-16 text, or nothing when the text is not UTF-8.
 */
std::optional<std::wstring> Widen(std::string_view text)
{
    if (text.empty())
    {
        return std::wstring{};
    }
    const int length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0)
    {
        return std::nullopt;
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

/**
 * A Windows error code in words.
 * @param code The code (GetLastError's).
 * @return The system's message, without its trailing line break.
 */
std::string ErrorText(DWORD code)
{
    wchar_t    *buffer = nullptr;
    const DWORD length =
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, code, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    std::string text;
    if (length > 0 && buffer != nullptr)
    {
        const int size =
            WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
        text.resize(static_cast<std::size_t>(size));
        WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), text.data(), size, nullptr, nullptr);
    }
    LocalFree(buffer);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '.'))
    {
        text.pop_back();
    }
    return text.empty() ? "error " + std::to_string(code) : text;
}

/**
 * Quote one argument as the C runtime's command-line parser reads it back: in quotes when it is empty or holds a
 * space, a tab or a quote; a quote escaped with a backslash; backslashes doubled where a quote follows them.
 * @param argument The argument.
 * @return The quoted argument.
 */
std::wstring QuoteArgument(const std::wstring &argument)
{
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
    {
        return argument;
    }
    std::wstring quoted      = L"\"";
    std::size_t  backslashes = 0;
    for (const wchar_t character : argument)
    {
        if (character == L'\\')
        {
            ++backslashes;
            continue;
        }
        if (character == L'"')
        {
            quoted.append(backslashes * 2 + 1, L'\\');
        }
        else
        {
            quoted.append(backslashes, L'\\');
        }
        backslashes = 0;
        quoted += character;
    }
    quoted.append(backslashes * 2, L'\\');
    return quoted + L"\"";
}

/** Closes a handle when it goes out of scope, unless released. */
struct HandleCloser
{
    HANDLE handle = nullptr; ///< The handle, or null.

    ~HandleCloser()
    {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle);
        }
    }

    /** @return The handle, no longer closed here. */
    HANDLE Release()
    {
        HANDLE released = handle;
        handle          = nullptr;
        return released;
    }
};

} // namespace

/** The Windows handles of a child, and its reader threads. */
struct ChildProcess::Platform
{
    HANDLE      process = nullptr; ///< The child process.
    HANDLE      job     = nullptr; ///< The job object that kills it when closed.
    HANDLE      input   = nullptr; ///< The write end of its standard input.
    DWORD       id      = 0;       ///< Its process id.
    std::mutex  input_mutex;       ///< Serialises writes and the close of input.
    std::thread output_reader;     ///< Reads standard output.
    std::thread error_reader;      ///< Reads standard error.
};

ChildProcess::ChildProcess() : platform_(std::make_unique<Platform>())
{
}

Result<std::unique_ptr<ChildProcess>> ChildProcess::Start(const ProcessOptions &options)
{
    const auto executable = Widen(options.executable);
    if (!executable || executable->empty())
    {
        return Error{"the engine's path '" + options.executable + "' is empty or not UTF-8"};
    }
    std::wstring command_line = QuoteArgument(*executable);
    for (const auto &argument : options.arguments)
    {
        const auto wide = Widen(argument);
        if (!wide)
        {
            return Error{"the argument '" + argument + "' is not UTF-8"};
        }
        command_line += L" " + QuoteArgument(*wide);
    }
    const auto directory = Widen(options.working_directory);
    if (!directory)
    {
        return Error{"the working directory '" + options.working_directory + "' is not UTF-8"};
    }

    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HandleCloser        input_read, input_write, output_read, output_write, error_read, error_write;
    if (!CreatePipe(&input_read.handle, &input_write.handle, &inheritable, 0) ||
        !CreatePipe(&output_read.handle, &output_write.handle, &inheritable, 1 << 16))
    {
        return Error{"cannot make the engine's pipes: " + ErrorText(GetLastError())};
    }
    if (options.does_capture_error_output)
    {
        if (!CreatePipe(&error_read.handle, &error_write.handle, &inheritable, 1 << 16))
        {
            return Error{"cannot make the engine's pipes: " + ErrorText(GetLastError())};
        }
        SetHandleInformation(error_read.handle, HANDLE_FLAG_INHERIT, 0);
    }
    else
    {
        error_write.handle = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
                                         OPEN_EXISTING, 0, nullptr);
    }
    SetHandleInformation(input_write.handle, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(output_read.handle, HANDLE_FLAG_INHERIT, 0);

    // Only these three handles reach the child, whatever else is inheritable in the host at this moment.
    SIZE_T attribute_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
    std::vector<char> attribute_buffer(attribute_size);
    auto             *attributes   = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_buffer.data());
    HANDLE            inherited[3] = {input_read.handle, output_write.handle, error_write.handle};
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_size) ||
        !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof inherited,
                                   nullptr, nullptr))
    {
        return Error{"cannot prepare the engine's handles: " + ErrorText(GetLastError())};
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb         = sizeof startup;
    startup.StartupInfo.dwFlags    = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput  = input_read.handle;
    startup.StartupInfo.hStdOutput = output_write.handle;
    startup.StartupInfo.hStdError  = error_write.handle;
    startup.lpAttributeList        = attributes;
    PROCESS_INFORMATION information{};
    const BOOL          is_started =
        CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                       nullptr, directory->empty() ? nullptr : directory->c_str(), &startup.StartupInfo, &information);
    const DWORD start_error = GetLastError();
    DeleteProcThreadAttributeList(attributes);
    if (!is_started)
    {
        return Error{"cannot start '" + options.executable + "': " + ErrorText(start_error)};
    }

    std::unique_ptr<ChildProcess> child(new ChildProcess());
    Platform                     &platform = *child->platform_;
    platform.process                       = information.hProcess;
    platform.id                            = information.dwProcessId;
    platform.input                         = input_write.Release();

    // The job kills the child when its last handle closes: in the destructor, or as the host's own process ends.
    platform.job = CreateJobObjectW(nullptr, nullptr);
    if (platform.job != nullptr)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(platform.job, JobObjectExtendedLimitInformation, &limits, sizeof limits) ||
            !AssignProcessToJobObject(platform.job, information.hProcess))
        {
            CloseHandle(platform.job);
            platform.job = nullptr;
        }
    }
    ResumeThread(information.hThread);
    CloseHandle(information.hThread);

    // The child's ends close here, so the pipes end when the child does.
    CloseHandle(input_read.Release());
    CloseHandle(output_write.Release());
    CloseHandle(error_write.Release());

    const auto reader = [](HANDLE handle) {
        return [handle](char *buffer, std::size_t size) -> long {
            DWORD count = 0;
            do
            {
                if (!ReadFile(handle, buffer, static_cast<DWORD>(size), &count, nullptr))
                {
                    return -1;
                }
            } while (count == 0);
            return static_cast<long>(count);
        };
    };
    child->open_streams_        = options.does_capture_error_output ? 2 : 1;
    HANDLE        output_handle = output_read.Release();
    ChildProcess *self          = child.get();
    platform.output_reader      = std::thread([self, output_handle, reader] {
        self->ReadLines(ProcessStream::kOutput, reader(output_handle));
        CloseHandle(output_handle);
    });
    if (options.does_capture_error_output)
    {
        HANDLE error_handle   = error_read.Release();
        platform.error_reader = std::thread([self, error_handle, reader] {
            self->ReadLines(ProcessStream::kError, reader(error_handle));
            CloseHandle(error_handle);
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
    if (platform_->job != nullptr)
    {
        CloseHandle(platform_->job);
    }
    if (platform_->process != nullptr)
    {
        CloseHandle(platform_->process);
    }
}

void ChildProcess::JoinReaders()
{
    // The readers end when the pipes do: when the child and everything in its job have gone. A grandchild outside
    // the job could hold a pipe open, so after a grace period the blocked reads are cancelled.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!output_.IsClosed() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (std::thread *reader : {&platform_->output_reader, &platform_->error_reader})
    {
        if (reader->joinable())
        {
            if (!output_.IsClosed())
            {
                CancelSynchronousIo(reader->native_handle());
            }
            reader->join();
        }
    }
}

bool ChildProcess::WriteLine(std::string_view line)
{
    std::lock_guard lock(platform_->input_mutex);
    if (platform_->input == nullptr)
    {
        return false;
    }
    std::string data(line);
    data += '\n';
    const char *next      = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0)
    {
        DWORD written = 0;
        if (!WriteFile(platform_->input, next, static_cast<DWORD>(remaining), &written, nullptr))
        {
            return false;
        }
        next += written;
        remaining -= written;
    }
    return true;
}

void ChildProcess::CloseInput()
{
    std::lock_guard lock(platform_->input_mutex);
    if (platform_->input != nullptr)
    {
        CloseHandle(platform_->input);
        platform_->input = nullptr;
    }
}

bool ChildProcess::IsRunning()
{
    return WaitForSingleObject(platform_->process, 0) == WAIT_TIMEOUT;
}

std::optional<int> ChildProcess::ExitCode()
{
    if (IsRunning())
    {
        return std::nullopt;
    }
    DWORD code = 0;
    GetExitCodeProcess(platform_->process, &code);
    return static_cast<int>(code);
}

bool ChildProcess::WaitForExit(std::chrono::milliseconds timeout)
{
    return WaitForSingleObject(platform_->process, static_cast<DWORD>(timeout.count())) == WAIT_OBJECT_0;
}

void ChildProcess::Kill()
{
    if (platform_->job != nullptr)
    {
        TerminateJobObject(platform_->job, 1);
    }
    else
    {
        TerminateProcess(platform_->process, 1);
    }
    WaitForSingleObject(platform_->process, 5000);
}

long ChildProcess::Id() const
{
    return static_cast<long>(platform_->id);
}

} // namespace uci_host::uci

#endif
