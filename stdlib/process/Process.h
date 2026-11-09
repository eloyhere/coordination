#pragma once

// process 模块：不经过 shell 的子进程管理，提供同步执行（run）与异步子进程（Child）。
//
// 设计要点：
//   1. 参数以数组形式传递，绝不拼接 shell 命令行，避免命令注入；
//   2. stdout / stderr 各由一个 std::thread 读取，stdin 由后台线程写入，读写互不阻塞，
//      因此子进程即使“写满输出但不读 stdin”也不会死锁；
//   3. 超时后强制终止子进程，并解除管道上的阻塞读写，保证 run 一定返回；
//   4. 所有接口不抛异常，失败原因写入 ProcessResult::error 或 spawn 的 error 参数。
//
// 注意：子进程若派生并留下了仍持有管道的后台进程，未设置 timeout 的 run 会一直等待这些
// 管道关闭；需要兜底时请设置 Command::timeout。
//
// 平台：Windows 用 CreateProcessW + 匿名管道；POSIX 用 fork/execvp + pipe + poll。
// 编码：所有字符串按 UTF-8 解释，Windows 侧转换为 UTF-16 后交给系统 API。

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cwchar>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace coordination::process
{
    // 子进程描述。
    struct Command
    {
        // 可执行文件：可带路径，也可只写文件名（按 PATH 查找）；不经过 shell。
        std::string program;

        // 参数数组（不含 argv[0]），逐项原样传递。
        std::vector<std::string> arguments;

        // 子进程工作目录；空表示继承父进程当前目录。
        std::string working_directory;

        // 追加或覆盖的环境变量：inherit_environment 为 true 时在继承结果上应用，否则只用这里的条目。
        std::vector<std::pair<std::string, std::string>> environment;

        // 是否继承父进程的环境变量。
        bool inherit_environment = true;

        // 写入子进程 stdin 的数据；写完后关闭 stdin，让子进程读到 EOF。
        std::string stdin_data;

        // 超时；0 表示不限制。
        std::chrono::milliseconds timeout{0};
    };

    // 同步执行的结果。
    struct ProcessResult
    {
        int exit_code = -1;       // 退出码；POSIX 下被信号终止时为 128 + 信号值
        bool exited = false;      // 是否正常结束（以非 0 退出码结束也算正常结束）
        bool timed_out = false;   // 是否因超时被强制终止
        bool signaled = false;    // POSIX：是否被信号终止
        int signal = 0;           // POSIX：终止信号值
        std::string stdout_data;  // 标准输出
        std::string stderr_data;  // 标准错误
        std::string error;        // 启动或等待失败的原因；为空表示流程正常

        // 成功：无错误、正常退出、退出码为 0，且不是超时或信号终止。
        bool ok() const
        {
            return error.empty() && exited && !timed_out && !signaled && exit_code == 0;
        }
    };

#if defined(_WIN32)

    namespace detail
    {
        // UTF-8 转 UTF-16；失败时写入 error。
        inline bool utf8_to_wide(const std::string& text, std::wstring& result, std::string& error)
        {
            result.clear();
            if (text.empty())
            {
                return true;
            }

            const int length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                                     static_cast<int>(text.size()), nullptr, 0);
            if (length <= 0)
            {
                error = "文本不是合法的 UTF-8";
                return false;
            }

            result.resize(static_cast<std::size_t>(length));
            if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                      static_cast<int>(text.size()), result.data(), length) <= 0)
            {
                result.clear();
                error = "文本不是合法的 UTF-8";
                return false;
            }
            return true;
        }

        // UTF-16 转 UTF-8。
        inline std::string wide_to_utf8(const std::wstring& text)
        {
            if (text.empty())
            {
                return std::string();
            }

            const int length = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                                     nullptr, 0, nullptr, nullptr);
            if (length <= 0)
            {
                return std::string();
            }

            std::string result(static_cast<std::size_t>(length), '\0');
            ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                  result.data(), length, nullptr, nullptr);
            return result;
        }

        // 把 Windows 错误码转成可读文本。
        inline std::string windows_error_text(::DWORD code)
        {
            wchar_t* buffer = nullptr;
            const ::DWORD length = ::FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, code, 0, reinterpret_cast<::LPWSTR>(&buffer), 0, nullptr);

            std::string message;
            if (length > 0 && buffer != nullptr)
            {
                message = wide_to_utf8(std::wstring(buffer, length));
                while (!message.empty() &&
                       (message.back() == '\r' || message.back() == '\n' || message.back() == ' '))
                {
                    message.pop_back();
                }
            }
            if (buffer != nullptr)
            {
                ::LocalFree(buffer);
            }
            if (message.empty())
            {
                message = "错误码 " + std::to_string(code);
            }
            return message;
        }

        // 按 CommandLineToArgvW 的规则给单个参数加引号，保证参数原样到达子进程。
        inline void append_quoted_argument(std::wstring& command_line, const std::wstring& argument)
        {
            if (!command_line.empty())
            {
                command_line.push_back(L' ');
            }

            if (argument.empty())
            {
                command_line.append(L"\"\"");
                return;
            }

            if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
            {
                command_line.append(argument);
                return;
            }

            command_line.push_back(L'"');
            std::size_t index = 0;
            while (index < argument.size())
            {
                std::size_t backslashes = 0;
                while (index < argument.size() && argument[index] == L'\\')
                {
                    ++backslashes;
                    ++index;
                }

                if (index == argument.size())
                {
                    // 结尾的反斜杠必须成对，避免转义收尾的引号。
                    command_line.append(backslashes * 2, L'\\');
                    break;
                }

                if (argument[index] == L'"')
                {
                    command_line.append(backslashes * 2 + 1, L'\\');
                }
                else
                {
                    command_line.append(backslashes, L'\\');
                }
                command_line.push_back(argument[index]);
                ++index;
            }
            command_line.push_back(L'"');
        }

        // 查找可执行文件：SearchPathW 补全 .exe，并按父进程目录、当前目录、系统目录、PATH 查找。
        inline bool resolve_executable(const std::wstring& program, std::wstring& resolved, std::string& error)
        {
            ::DWORD capacity = MAX_PATH;
            for (;;)
            {
                std::wstring buffer(static_cast<std::size_t>(capacity), L'\0');
                const ::DWORD length = ::SearchPathW(nullptr, program.c_str(), L".exe", capacity,
                                                     buffer.data(), nullptr);
                if (length == 0)
                {
                    error = "找不到程序: " + wide_to_utf8(program);
                    return false;
                }
                if (length < capacity)
                {
                    buffer.resize(static_cast<std::size_t>(length));
                    resolved = std::move(buffer);
                    return true;
                }
                capacity = length + 1;
            }
        }

        // 构造 CREATE_UNICODE_ENVIRONMENT 需要的环境块：KEY=VALUE 以 '\0' 分隔，整块以 '\0' 收尾。
        inline bool build_environment_block(const Command& command, std::vector<wchar_t>& block, std::string& error)
        {
            std::vector<std::pair<std::wstring, std::wstring>> entries;

            if (command.inherit_environment)
            {
                wchar_t* const environment = ::GetEnvironmentStringsW();
                if (environment == nullptr)
                {
                    error = "读取环境变量失败: " + windows_error_text(::GetLastError());
                    return false;
                }

                for (const wchar_t* cursor = environment; *cursor != L'\0'; cursor += std::wcslen(cursor) + 1)
                {
                    // 以 '=' 开头的条目（如 "=C:=C:\dir"）是驱动器当前目录，不能出现在环境块中。
                    if (*cursor == L'=')
                    {
                        continue;
                    }

                    const wchar_t* separator = std::wcschr(cursor, L'=');
                    if (separator == nullptr)
                    {
                        continue;
                    }
                    entries.emplace_back(std::wstring(cursor, static_cast<std::size_t>(separator - cursor)),
                                         std::wstring(separator + 1));
                }
                ::FreeEnvironmentStringsW(environment);
            }

            for (const std::pair<std::string, std::string>& variable : command.environment)
            {
                std::wstring key;
                std::wstring value;
                if (!utf8_to_wide(variable.first, key, error) || !utf8_to_wide(variable.second, value, error))
                {
                    return false;
                }
                if (key.empty() || key.find(L'=') != std::wstring::npos ||
                    key.find(L'\0') != std::wstring::npos || value.find(L'\0') != std::wstring::npos)
                {
                    error = "环境变量名不能为空、不能含 '=' 或 NUL";
                    return false;
                }

                bool replaced = false;
                for (std::pair<std::wstring, std::wstring>& entry : entries)
                {
                    // Windows 的环境变量名不区分大小写。
                    if (::_wcsicmp(entry.first.c_str(), key.c_str()) == 0)
                    {
                        entry.second = std::move(value);
                        replaced = true;
                        break;
                    }
                }
                if (!replaced)
                {
                    entries.emplace_back(std::move(key), std::move(value));
                }
            }

            std::sort(entries.begin(), entries.end(),
                      [](const std::pair<std::wstring, std::wstring>& left,
                         const std::pair<std::wstring, std::wstring>& right)
                      {
                          return ::_wcsicmp(left.first.c_str(), right.first.c_str()) < 0;
                      });

            block.clear();
            for (const std::pair<std::wstring, std::wstring>& entry : entries)
            {
                block.insert(block.end(), entry.first.begin(), entry.first.end());
                block.push_back(L'=');
                block.insert(block.end(), entry.second.begin(), entry.second.end());
                block.push_back(L'\0');
            }
            block.push_back(L'\0');
            return true;
        }

        // 句柄包装：作用域结束时自动 CloseHandle。
        class UniqueHandle
        {
        public:
            UniqueHandle() = default;

            explicit UniqueHandle(::HANDLE handle)
                : handle_(handle)
            {
            }

            ~UniqueHandle()
            {
                reset();
            }

            UniqueHandle(const UniqueHandle&) = delete;
            UniqueHandle& operator=(const UniqueHandle&) = delete;

            UniqueHandle(UniqueHandle&& other) noexcept
                : handle_(other.release())
            {
            }

            UniqueHandle& operator=(UniqueHandle&& other) noexcept
            {
                if (this != &other)
                {
                    reset(other.release());
                }
                return *this;
            }

            ::HANDLE get() const
            {
                return handle_;
            }

            ::HANDLE release()
            {
                const ::HANDLE handle = handle_;
                handle_ = nullptr;
                return handle;
            }

            void reset(::HANDLE handle = nullptr)
            {
                if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
                {
                    ::CloseHandle(handle_);
                }
                handle_ = handle;
            }

            explicit operator bool() const
            {
                return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
            }

        private:
            ::HANDLE handle_ = nullptr;
        };

        // 创建匿名管道；两端默认可继承，由调用方把父进程这一端设为不可继承。
        inline bool create_pipe(UniqueHandle& read_end, UniqueHandle& write_end, std::string& error)
        {
            ::SECURITY_ATTRIBUTES attributes{};
            attributes.nLength = static_cast<::DWORD>(sizeof(attributes));
            attributes.bInheritHandle = TRUE;

            ::HANDLE read_handle = nullptr;
            ::HANDLE write_handle = nullptr;
            if (!::CreatePipe(&read_handle, &write_handle, &attributes, 0))
            {
                error = "创建管道失败: " + windows_error_text(::GetLastError());
                return false;
            }

            read_end.reset(read_handle);
            write_end.reset(write_handle);
            return true;
        }

        // 父进程持有的管道端不可被子进程继承，否则子进程退出后管道仍有引用，读取线程等不到 EOF。
        inline void make_non_inheritable(::HANDLE handle)
        {
            if (handle != nullptr)
            {
                ::SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0);
            }
        }

        // 在独立线程里把管道读空；句柄由调用方的 UniqueHandle 关闭。
        inline void read_pipe_all(::HANDLE handle, std::string& output)
        {
            char buffer[4096];
            for (;;)
            {
                ::DWORD read = 0;
                if (!::ReadFile(handle, buffer, static_cast<::DWORD>(sizeof(buffer)), &read, nullptr) || read == 0)
                {
                    break;
                }
                output.append(buffer, static_cast<std::size_t>(read));
            }
        }

        // 在独立线程里写 stdin；句柄由调用方的 UniqueHandle 关闭。
        inline void write_pipe_all(::HANDLE handle, const std::string& data)
        {
            std::size_t offset = 0;
            while (offset < data.size())
            {
                const ::DWORD chunk = static_cast<::DWORD>(std::min<std::size_t>(data.size() - offset, 65536));
                ::DWORD written = 0;
                if (!::WriteFile(handle, data.data() + offset, chunk, &written, nullptr) || written == 0)
                {
                    break;  // 子进程已关闭 stdin（ERROR_BROKEN_PIPE）时提前结束
                }
                offset += written;
            }
        }

        // 取消线程上阻塞的同步读写；用于超时路径，避免子进程的后代仍持有管道时 join 永久阻塞。
        inline void cancel_and_join(std::thread& worker)
        {
            if (!worker.joinable())
            {
                return;
            }

            const ::HANDLE thread_handle = static_cast<::HANDLE>(worker.native_handle());
            for (int attempt = 0; attempt < 50; ++attempt)
            {
                if (::WaitForSingleObject(thread_handle, 0) == WAIT_OBJECT_0)
                {
                    break;
                }
                ::CancelSynchronousIo(thread_handle);
                ::Sleep(1);
            }
            worker.join();
        }

        // 创建进程；standard_input / standard_output / standard_error 必须可继承。
        inline bool create_process(const Command& command, ::HANDLE standard_input, ::HANDLE standard_output,
                                   ::HANDLE standard_error, ::PROCESS_INFORMATION& info, std::string& error)
        {
            if (command.program.empty())
            {
                error = "program 不能为空";
                return false;
            }

            std::wstring program;
            if (!utf8_to_wide(command.program, program, error))
            {
                return false;
            }
            // Windows 同时接受 '/'，统一成 '\\' 便于 SearchPathW 识别路径。
            std::replace(program.begin(), program.end(), L'/', L'\\');

            std::wstring executable;
            if (!resolve_executable(program, executable, error))
            {
                return false;
            }

            std::wstring command_line;
            append_quoted_argument(command_line, program);
            for (const std::string& argument : command.arguments)
            {
                std::wstring wide_argument;
                if (!utf8_to_wide(argument, wide_argument, error))
                {
                    return false;
                }
                append_quoted_argument(command_line, wide_argument);
            }
            if (command_line.size() > 32767)
            {
                error = "命令行超过 Windows 上限（32767 个字符）";
                return false;
            }

            std::wstring working_directory;
            if (!command.working_directory.empty())
            {
                if (!utf8_to_wide(command.working_directory, working_directory, error))
                {
                    return false;
                }
                std::replace(working_directory.begin(), working_directory.end(), L'/', L'\\');
            }

            std::vector<wchar_t> environment_block;
            if (!build_environment_block(command, environment_block, error))
            {
                return false;
            }

            // bInheritHandles=TRUE 会继承父进程所有可继承句柄；并发创建进程时可能把别的管道带进
            // 子进程，导致父进程永远等不到 EOF。这里用属性列表把继承范围精确限定为三个标准流句柄。
            ::SIZE_T attribute_size = 0;
            ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
            std::vector<unsigned char> attribute_storage(attribute_size);
            auto* const attributes = reinterpret_cast<::LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
            if (!::InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_size))
            {
                error = "初始化进程属性列表失败: " + windows_error_text(::GetLastError());
                return false;
            }

            const ::HANDLE stdio_handles[3] = {standard_input, standard_output, standard_error};
            ::HANDLE inherited_handles[3] = {nullptr, nullptr, nullptr};
            ::DWORD inherited_count = 0;
            for (const ::HANDLE handle : stdio_handles)
            {
                bool duplicated = false;
                for (::DWORD index = 0; index < inherited_count; ++index)
                {
                    if (inherited_handles[index] == handle)
                    {
                        duplicated = true;
                        break;
                    }
                }
                if (!duplicated)
                {
                    inherited_handles[inherited_count] = handle;
                    ++inherited_count;
                }
            }

            if (!::UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited_handles,
                                             static_cast<::SIZE_T>(inherited_count) * sizeof(::HANDLE), nullptr,
                                             nullptr))
            {
                ::DeleteProcThreadAttributeList(attributes);
                error = "设置进程句柄继承列表失败: " + windows_error_text(::GetLastError());
                return false;
            }

            ::STARTUPINFOEXW startup{};
            startup.StartupInfo.cb = static_cast<::DWORD>(sizeof(startup));
            startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
            startup.StartupInfo.hStdInput = standard_input;
            startup.StartupInfo.hStdOutput = standard_output;
            startup.StartupInfo.hStdError = standard_error;
            startup.lpAttributeList = attributes;

            info = ::PROCESS_INFORMATION{};
            const ::BOOL created = ::CreateProcessW(
                executable.c_str(), command_line.data(), nullptr, nullptr, TRUE,
                EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, environment_block.data(),
                working_directory.empty() ? nullptr : working_directory.c_str(), &startup.StartupInfo, &info);
            const ::DWORD create_error = created ? ERROR_SUCCESS : ::GetLastError();
            ::DeleteProcThreadAttributeList(attributes);

            if (!created)
            {
                error = "创建进程失败: " + windows_error_text(create_error);
                return false;
            }

            ::CloseHandle(info.hThread);
            info.hThread = nullptr;
            return true;
        }
    }

    // 同步执行：捕获 stdout/stderr，按需写入 stdin，超时则强制终止。
    inline ProcessResult run(const Command& command)
    {
        ProcessResult result;

        detail::UniqueHandle input_read;
        detail::UniqueHandle input_write;
        detail::UniqueHandle output_read;
        detail::UniqueHandle output_write;
        detail::UniqueHandle error_read;
        detail::UniqueHandle error_write;

        if (!detail::create_pipe(input_read, input_write, result.error) ||
            !detail::create_pipe(output_read, output_write, result.error) ||
            !detail::create_pipe(error_read, error_write, result.error))
        {
            return result;
        }

        detail::make_non_inheritable(input_write.get());
        detail::make_non_inheritable(output_read.get());
        detail::make_non_inheritable(error_read.get());

        ::PROCESS_INFORMATION info{};
        if (!detail::create_process(command, input_read.get(), output_write.get(), error_write.get(), info, result.error))
        {
            return result;
        }

        detail::UniqueHandle process(info.hProcess);
        input_read.reset();
        output_write.reset();
        error_write.reset();

        std::thread output_reader;
        std::thread error_reader;
        std::thread input_writer;

        try
        {
            output_reader = std::thread([handle = std::move(output_read), &result]()
                                        {
                                            detail::read_pipe_all(handle.get(), result.stdout_data);
                                        });
            error_reader = std::thread([handle = std::move(error_read), &result]()
                                       {
                                           detail::read_pipe_all(handle.get(), result.stderr_data);
                                       });
            if (!command.stdin_data.empty())
            {
                input_writer = std::thread([handle = std::move(input_write), data = command.stdin_data]()
                                           {
                                               detail::write_pipe_all(handle.get(), data);
                                           });
            }
            else
            {
                input_write.reset();
            }
        }
        catch (const std::exception& exception)
        {
            result.error = std::string("创建管道线程失败: ") + exception.what();
            ::TerminateProcess(process.get(), 1);
            ::WaitForSingleObject(process.get(), INFINITE);
            detail::cancel_and_join(output_reader);
            detail::cancel_and_join(error_reader);
            detail::cancel_and_join(input_writer);
            return result;
        }

        auto timeout = command.timeout.count();
        if (timeout < 1)
        {
            timeout = 0;
        }
        if (timeout > 0xFFFFFFFELL)
        {
            timeout = 0xFFFFFFFELL;
        }

        ::DWORD wait_state = WAIT_FAILED;
        if (timeout > 0)
        {
            wait_state = ::WaitForSingleObject(process.get(), static_cast<::DWORD>(timeout));
        }
        else
        {
            wait_state = ::WaitForSingleObject(process.get(), INFINITE);
        }

        if (wait_state == WAIT_TIMEOUT)
        {
            // 超时：强制终止并等待句柄有信号，之后子进程不会再写管道。
            result.timed_out = true;
            ::TerminateProcess(process.get(), 1);
            ::WaitForSingleObject(process.get(), INFINITE);
        }
        else if (wait_state == WAIT_FAILED)
        {
            result.error = "等待进程失败: " + detail::windows_error_text(::GetLastError());
            ::TerminateProcess(process.get(), 1);
            ::WaitForSingleObject(process.get(), INFINITE);
        }

        if (result.timed_out || wait_state == WAIT_FAILED)
        {
            // 子进程可能留下了仍持有管道的后代，取消阻塞读，避免 join 卡死。
            detail::cancel_and_join(output_reader);
            detail::cancel_and_join(error_reader);
            detail::cancel_and_join(input_writer);
        }
        else
        {
            if (output_reader.joinable())
            {
                output_reader.join();
            }
            if (error_reader.joinable())
            {
                error_reader.join();
            }
            if (input_writer.joinable())
            {
                input_writer.join();
            }
        }

        ::DWORD exit_code = 0;
        if (::GetExitCodeProcess(process.get(), &exit_code))
        {
            result.exit_code = static_cast<int>(exit_code);
            result.exited = (wait_state == WAIT_OBJECT_0);
        }
        else if (result.error.empty())
        {
            result.error = "读取退出码失败: " + detail::windows_error_text(::GetLastError());
        }

        return result;
    }

#else

    namespace detail
    {
        // 文件描述符包装：作用域结束时自动 close。
        class UniqueFd
        {
        public:
            UniqueFd() = default;

            explicit UniqueFd(int descriptor)
                : descriptor_(descriptor)
            {
            }

            ~UniqueFd()
            {
                reset();
            }

            UniqueFd(const UniqueFd&) = delete;
            UniqueFd& operator=(const UniqueFd&) = delete;

            UniqueFd(UniqueFd&& other) noexcept
                : descriptor_(other.release())
            {
            }

            UniqueFd& operator=(UniqueFd&& other) noexcept
            {
                if (this != &other)
                {
                    reset(other.release());
                }
                return *this;
            }

            int get() const
            {
                return descriptor_;
            }

            int release()
            {
                const int descriptor = descriptor_;
                descriptor_ = -1;
                return descriptor;
            }

            void reset(int descriptor = -1)
            {
                if (descriptor_ >= 0)
                {
                    ::close(descriptor_);
                }
                descriptor_ = descriptor;
            }

            explicit operator bool() const
            {
                return descriptor_ >= 0;
            }

        private:
            int descriptor_ = -1;
        };

        // 创建管道并给两端设置 FD_CLOEXEC：exec 成功后自动关闭，子进程不会意外持有父进程端。
        inline bool create_pipe(UniqueFd& read_end, UniqueFd& write_end, std::string& error)
        {
            int descriptors[2] = {-1, -1};
            if (::pipe(descriptors) != 0)
            {
                error = std::string("创建管道失败: ") + std::strerror(errno);
                return false;
            }

            for (const int descriptor : descriptors)
            {
                const int flags = ::fcntl(descriptor, F_GETFD);
                if (flags >= 0)
                {
                    ::fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC);
                }
            }

            read_end.reset(descriptors[0]);
            write_end.reset(descriptors[1]);
            return true;
        }

        // 设为非阻塞：读线程以 poll 驱动，超时路径不会被 read/write 卡住。
        inline void set_nonblocking(int descriptor)
        {
            const int flags = ::fcntl(descriptor, F_GETFL);
            if (flags >= 0)
            {
                ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK);
            }
        }

        // fork 前准备好的参数与环境：所有字符串都已稳定存储，子进程里只做系统调用。
        struct LaunchPlan
        {
            std::vector<std::string> argument_storage;
            std::vector<char*> argument_pointers;
            std::vector<std::string> environment_storage;
            std::vector<char*> environment_pointers;
            std::string working_directory;
        };

        inline bool prepare_launch(const Command& command, LaunchPlan& plan, std::string& error)
        {
            if (command.program.empty())
            {
                error = "program 不能为空";
                return false;
            }
            if (command.program.find('\0') != std::string::npos)
            {
                error = "program 不能包含 NUL 字符";
                return false;
            }

            plan.argument_storage.push_back(command.program);
            for (const std::string& argument : command.arguments)
            {
                if (argument.find('\0') != std::string::npos)
                {
                    error = "参数不能包含 NUL 字符";
                    return false;
                }
                plan.argument_storage.push_back(argument);
            }
            plan.argument_pointers.reserve(plan.argument_storage.size() + 1);
            for (std::string& argument : plan.argument_storage)
            {
                plan.argument_pointers.push_back(argument.data());
            }
            plan.argument_pointers.push_back(nullptr);

            std::vector<std::pair<std::string, std::string>> entries;
            if (command.inherit_environment)
            {
                for (char** cursor = ::environ; cursor != nullptr && *cursor != nullptr; ++cursor)
                {
                    const std::string entry(*cursor);
                    const std::size_t separator = entry.find('=');
                    if (separator == std::string::npos || separator == 0)
                    {
                        continue;
                    }
                    entries.emplace_back(entry.substr(0, separator), entry.substr(separator + 1));
                }
            }

            for (const std::pair<std::string, std::string>& variable : command.environment)
            {
                if (variable.first.empty() || variable.first.find('=') != std::string::npos ||
                    variable.first.find('\0') != std::string::npos ||
                    variable.second.find('\0') != std::string::npos)
                {
                    error = "环境变量名不能为空、不能含 '=' 或 NUL";
                    return false;
                }

                bool replaced = false;
                for (std::pair<std::string, std::string>& entry : entries)
                {
                    // POSIX 的环境变量名区分大小写。
                    if (entry.first == variable.first)
                    {
                        entry.second = variable.second;
                        replaced = true;
                        break;
                    }
                }
                if (!replaced)
                {
                    entries.push_back(variable);
                }
            }

            plan.environment_storage.reserve(entries.size());
            for (const std::pair<std::string, std::string>& entry : entries)
            {
                plan.environment_storage.push_back(entry.first + '=' + entry.second);
            }
            plan.environment_pointers.reserve(plan.environment_storage.size() + 1);
            for (std::string& entry : plan.environment_storage)
            {
                plan.environment_pointers.push_back(entry.data());
            }
            plan.environment_pointers.push_back(nullptr);

            plan.working_directory = command.working_directory;
            return true;
        }

        // 子进程 exec 失败时回传给父进程的信息：stage 标明失败阶段，code 为 errno。
        struct ExecFailure
        {
            int stage = 0;  // 1 = chdir，2 = dup2，3 = execvp
            int code = 0;
        };

        // 子进程内上报启动失败；只做一次 write，不分配内存。
        inline void report_exec_failure(int descriptor, int stage, int code) noexcept
        {
            ExecFailure failure;
            failure.stage = stage;
            failure.code = code;
            (void)::write(descriptor, &failure, sizeof(failure));
        }

        // 子进程：fork 之后只能调用异步信号安全的接口，绝不分配内存、不抛异常。
        inline void child_exec(const LaunchPlan& plan, int standard_input, int standard_output, int standard_error,
                               int exec_error_descriptor, const int* inherited_descriptors,
                               std::size_t inherited_count) noexcept
        {
            if (!plan.working_directory.empty() && ::chdir(plan.working_directory.c_str()) != 0)
            {
                report_exec_failure(exec_error_descriptor, 1, errno);
                ::_exit(127);
            }

            if (::dup2(standard_input, STDIN_FILENO) < 0 || ::dup2(standard_output, STDOUT_FILENO) < 0 ||
                ::dup2(standard_error, STDERR_FILENO) < 0)
            {
                report_exec_failure(exec_error_descriptor, 2, errno);
                ::_exit(127);
            }

            // 其余管道 fd 都带 FD_CLOEXEC，这里显式关闭以便尽早释放引用。
            for (std::size_t index = 0; index < inherited_count; ++index)
            {
                if (inherited_descriptors[index] > STDERR_FILENO)
                {
                    ::close(inherited_descriptors[index]);
                }
            }

            ::environ = plan.environment_pointers.data();
            ::execvp(plan.argument_pointers[0], plan.argument_pointers.data());

            report_exec_failure(exec_error_descriptor, 3, errno);
            ::_exit(127);
        }

        // 在独立线程里读管道：用 poll 等数据，stop 置位后读空缓冲即返回，不会永久阻塞。
        inline void read_pipe_all(int descriptor, std::string& output, const std::atomic<bool>& stop)
        {
            char buffer[4096];
            for (;;)
            {
                if (stop.load(std::memory_order_relaxed))
                {
                    for (;;)
                    {
                        const ssize_t count = ::read(descriptor, buffer, sizeof(buffer));
                        if (count <= 0)
                        {
                            break;
                        }
                        output.append(buffer, static_cast<std::size_t>(count));
                    }
                    break;
                }

                ::pollfd descriptor_state{};
                descriptor_state.fd = descriptor;
                descriptor_state.events = POLLIN;

                const int ready = ::poll(&descriptor_state, 1, 20);
                if (ready < 0)
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    break;
                }
                if (ready == 0)
                {
                    continue;
                }

                const ssize_t count = ::read(descriptor, buffer, sizeof(buffer));
                if (count > 0)
                {
                    output.append(buffer, static_cast<std::size_t>(count));
                    continue;
                }
                if (count == 0)
                {
                    break;  // EOF：写端已全部关闭
                }
                if (errno == EINTR)
                {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    continue;
                }
                break;
            }
        }

        // 在独立线程里写 stdin：屏蔽 SIGPIPE，避免子进程提前退出把整个进程杀掉。
        inline void write_pipe_all(int descriptor, const std::string& data, const std::atomic<bool>& stop)
        {
            sigset_t blocked;
            sigset_t previous;
            ::sigemptyset(&blocked);
            ::sigaddset(&blocked, SIGPIPE);
            const bool masked = ::pthread_sigmask(SIG_BLOCK, &blocked, &previous) == 0;

            std::size_t offset = 0;
            while (offset < data.size() && !stop.load(std::memory_order_relaxed))
            {
                ::pollfd descriptor_state{};
                descriptor_state.fd = descriptor;
                descriptor_state.events = POLLOUT;

                const int ready = ::poll(&descriptor_state, 1, 20);
                if (ready < 0)
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    break;
                }
                if (ready == 0)
                {
                    continue;
                }

                const ssize_t count = ::write(descriptor, data.data() + offset, data.size() - offset);
                if (count > 0)
                {
                    offset += static_cast<std::size_t>(count);
                    continue;
                }
                if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
                {
                    continue;
                }
                break;  // EPIPE：子进程已关闭 stdin
            }

            // 消费可能挂起的 SIGPIPE，否则恢复掩码时默认动作会终止整个进程。
            if (masked)
            {
                sigset_t pending;
                ::sigemptyset(&pending);
                if (::sigpending(&pending) == 0 && ::sigismember(&pending, SIGPIPE) == 1)
                {
                    int received = 0;
                    (void)::sigwait(&blocked, &received);
                }
                (void)::pthread_sigmask(SIG_SETMASK, &previous, nullptr);
            }
        }

        // 等待 fork 出来的子进程完成 exec：成功时 exec 管道读到 EOF，失败时读到 ExecFailure。
        inline bool wait_for_exec(int exec_read_end, ExecFailure& failure)
        {
            std::size_t received = 0;
            char* const buffer = reinterpret_cast<char*>(&failure);
            while (received < sizeof(failure))
            {
                const ssize_t count = ::read(exec_read_end, buffer + received, sizeof(failure) - received);
                if (count > 0)
                {
                    received += static_cast<std::size_t>(count);
                    continue;
                }
                if (count < 0 && errno == EINTR)
                {
                    continue;
                }
                break;
            }
            return received == sizeof(failure);
        }

        // 把执行失败信息转成可读文本。
        inline std::string exec_failure_text(const ExecFailure& failure, const std::string& program)
        {
            if (failure.stage == 1)
            {
                return std::string("无法切换工作目录: ") + std::strerror(failure.code);
            }
            if (failure.stage == 2)
            {
                return std::string("无法设置子进程管道: ") + std::strerror(failure.code);
            }
            if (failure.code == ENOENT)
            {
                return "找不到程序: " + program;
            }
            return std::string("无法执行程序: ") + std::strerror(failure.code);
        }
    }

    // 同步执行：捕获 stdout/stderr，按需写入 stdin，超时则强制终止。
    inline ProcessResult run(const Command& command)
    {
        ProcessResult result;

        detail::LaunchPlan plan;
        if (!detail::prepare_launch(command, plan, result.error))
        {
            return result;
        }

        detail::UniqueFd input_read;
        detail::UniqueFd input_write;
        detail::UniqueFd output_read;
        detail::UniqueFd output_write;
        detail::UniqueFd error_read;
        detail::UniqueFd error_write;
        detail::UniqueFd exec_read;
        detail::UniqueFd exec_write;

        if (!detail::create_pipe(input_read, input_write, result.error) ||
            !detail::create_pipe(output_read, output_write, result.error) ||
            !detail::create_pipe(error_read, error_write, result.error) ||
            !detail::create_pipe(exec_read, exec_write, result.error))
        {
            return result;
        }

        const ::pid_t pid = ::fork();
        if (pid < 0)
        {
            result.error = std::string("fork 失败: ") + std::strerror(errno);
            return result;
        }

        if (pid == 0)
        {
            const int inherited[] = {input_read.get(),  input_write.get(),  output_read.get(), output_write.get(),
                                     error_read.get(), error_write.get(), exec_read.get()};
            detail::child_exec(plan, input_read.get(), output_write.get(), error_write.get(), exec_write.get(),
                               inherited, sizeof(inherited) / sizeof(inherited[0]));
            ::_exit(127);  // child_exec 不会返回
        }

        // 父进程只保留自己这一端。
        input_read.reset();
        output_write.reset();
        error_write.reset();
        exec_write.reset();

        detail::ExecFailure exec_failure;
        if (detail::wait_for_exec(exec_read.get(), exec_failure))
        {
            exec_read.reset();
            int status = 0;
            (void)::waitpid(pid, &status, 0);
            result.error = detail::exec_failure_text(exec_failure, command.program);
            return result;
        }
        exec_read.reset();

        detail::set_nonblocking(output_read.get());
        detail::set_nonblocking(error_read.get());
        detail::set_nonblocking(input_write.get());

        // stop 置位后读写线程会在一个 poll 周期内退出，用于超时与异常路径。
        std::atomic<bool> stop{false};
        std::thread output_reader;
        std::thread error_reader;
        std::thread input_writer;

        try
        {
            output_reader = std::thread([descriptor = std::move(output_read), &result, &stop]()
                                        {
                                            detail::read_pipe_all(descriptor.get(), result.stdout_data, stop);
                                        });
            error_reader = std::thread([descriptor = std::move(error_read), &result, &stop]()
                                       {
                                           detail::read_pipe_all(descriptor.get(), result.stderr_data, stop);
                                       });
            if (!command.stdin_data.empty())
            {
                input_writer = std::thread([descriptor = std::move(input_write), data = command.stdin_data, &stop]()
                                           {
                                               detail::write_pipe_all(descriptor.get(), data, stop);
                                           });
            }
            else
            {
                input_write.reset();
            }
        }
        catch (const std::exception& exception)
        {
            result.error = std::string("创建管道线程失败: ") + exception.what();
            stop.store(true);
            (void)::kill(pid, SIGKILL);
            int status = 0;
            (void)::waitpid(pid, &status, 0);
            if (output_reader.joinable())
            {
                output_reader.join();
            }
            if (error_reader.joinable())
            {
                error_reader.join();
            }
            if (input_writer.joinable())
            {
                input_writer.join();
            }
            return result;
        }

        int status = 0;
        bool reaped = false;

        if (command.timeout.count() > 0)
        {
            const auto deadline = std::chrono::steady_clock::now() + command.timeout;
            for (;;)
            {
                const ::pid_t waited = ::waitpid(pid, &status, WNOHANG);
                if (waited == pid)
                {
                    reaped = true;
                    break;
                }
                if (waited < 0)
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    result.error = std::string("waitpid 失败: ") + std::strerror(errno);
                    break;
                }
                if (std::chrono::steady_clock::now() >= deadline)
                {
                    // 超时：SIGKILL 后同步回收，避免留下僵尸进程。
                    result.timed_out = true;
                    (void)::kill(pid, SIGKILL);
                    for (;;)
                    {
                        const ::pid_t killed = ::waitpid(pid, &status, 0);
                        if (killed == pid)
                        {
                            reaped = true;
                            break;
                        }
                        if (killed < 0 && errno == EINTR)
                        {
                            continue;
                        }
                        break;
                    }
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        else
        {
            for (;;)
            {
                const ::pid_t waited = ::waitpid(pid, &status, 0);
                if (waited == pid)
                {
                    reaped = true;
                    break;
                }
                if (waited < 0 && errno == EINTR)
                {
                    continue;
                }
                if (waited < 0)
                {
                    result.error = std::string("waitpid 失败: ") + std::strerror(errno);
                }
                break;
            }
        }

        if (reaped)
        {
            if (WIFEXITED(status))
            {
                result.exit_code = WEXITSTATUS(status);
                result.exited = true;
            }
            else if (WIFSIGNALED(status))
            {
                result.signaled = true;
                result.signal = WTERMSIG(status);
                result.exit_code = 128 + result.signal;
            }
        }

        if (result.timed_out)
        {
            stop.store(true);
        }

        if (output_reader.joinable())
        {
            output_reader.join();
        }
        if (error_reader.joinable())
        {
            error_reader.join();
        }
        if (input_writer.joinable())
        {
            input_writer.join();
        }

        return result;
    }

#endif

    // 异步子进程：spawn 后父进程继续执行，wait 等待结束，kill 强制终止。
    //
    // Child 不捕获子进程输出，stdout/stderr 重定向到空设备；需要输出请用 run()。
    // Command::timeout 只作用于 run()，异步子进程由调用方通过 kill()/wait() 控制。
    // 析构时若子进程仍在运行则终止并回收，避免僵尸进程与句柄泄漏。
    class Child
    {
    public:
        Child() = default;
        ~Child();

        Child(Child&& other) noexcept;
        Child& operator=(Child&& other) noexcept;
        Child(const Child&) = delete;
        Child& operator=(const Child&) = delete;

        // 启动子进程；失败返回 std::nullopt 并把原因写入 error（error 可以为 nullptr）。
        static std::optional<Child> spawn(const Command& command, std::string* error);

        // 是否仍在运行。
        bool running() const;

        // 等待结束并返回退出码；POSIX 下被信号终止时返回 128 + 信号值，已回收或失败返回 -1。
        int wait();

        // 强制终止；子进程已结束或取消失败时返回 false。
        bool kill();

    private:
        // 终止并回收，供析构与移动赋值使用。
        void cleanup() noexcept;

#if defined(_WIN32)
        detail::UniqueHandle process_;
#else
        ::pid_t pid_ = -1;
        mutable int cached_status_ = 0;
        mutable bool cached_exit_ = false;
#endif
        std::thread stdin_writer_;
#if !defined(_WIN32)
        std::shared_ptr<std::atomic<bool>> stop_ = std::make_shared<std::atomic<bool>>(false);
#endif
    };

#if defined(_WIN32)

    inline Child::~Child()
    {
        cleanup();
    }

    inline Child::Child(Child&& other) noexcept
        : process_(std::move(other.process_)), stdin_writer_(std::move(other.stdin_writer_))
    {
    }

    inline Child& Child::operator=(Child&& other) noexcept
    {
        if (this != &other)
        {
            cleanup();
            process_ = std::move(other.process_);
            stdin_writer_ = std::move(other.stdin_writer_);
        }
        return *this;
    }

    inline void Child::cleanup() noexcept
    {
        if (running())
        {
            (void)kill();
        }
        (void)wait();
    }

    inline std::optional<Child> Child::spawn(const Command& command, std::string* error)
    {
        std::string local_error;
        std::string& message = (error != nullptr) ? *error : local_error;
        message.clear();

        // 子进程的输出与输入统一接到空设备：Child 不提供输出接口，也不会阻塞在管道上。
        ::SECURITY_ATTRIBUTES attributes{};
        attributes.nLength = static_cast<::DWORD>(sizeof(attributes));
        attributes.bInheritHandle = TRUE;

        detail::UniqueHandle null_device(::CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                                                       FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                                                       OPEN_EXISTING, 0, nullptr));
        if (!null_device)
        {
            message = "打开 NUL 设备失败: " + detail::windows_error_text(::GetLastError());
            return std::nullopt;
        }

        detail::UniqueHandle input_read;
        detail::UniqueHandle input_write;
        if (!command.stdin_data.empty())
        {
            if (!detail::create_pipe(input_read, input_write, message))
            {
                return std::nullopt;
            }
            detail::make_non_inheritable(input_write.get());
        }

        const ::HANDLE child_input = command.stdin_data.empty() ? null_device.get() : input_read.get();

        ::PROCESS_INFORMATION info{};
        if (!detail::create_process(command, child_input, null_device.get(), null_device.get(), info, message))
        {
            return std::nullopt;
        }

        input_read.reset();
        null_device.reset();

        Child child;
        child.process_.reset(info.hProcess);

        if (command.stdin_data.empty())
        {
            input_write.reset();
        }
        else
        {
            try
            {
                child.stdin_writer_ = std::thread([handle = std::move(input_write), data = command.stdin_data]()
                                                  {
                                                      detail::write_pipe_all(handle.get(), data);
                                                  });
            }
            catch (const std::exception& exception)
            {
                message = std::string("创建 stdin 线程失败: ") + exception.what();
                ::TerminateProcess(child.process_.get(), 1);
                ::WaitForSingleObject(child.process_.get(), INFINITE);
                return std::nullopt;
            }
        }

        return child;
    }

    inline bool Child::running() const
    {
        return process_ && ::WaitForSingleObject(process_.get(), 0) == WAIT_TIMEOUT;
    }

    inline int Child::wait()
    {
        if (!process_)
        {
            return -1;
        }

        ::WaitForSingleObject(process_.get(), INFINITE);
        ::DWORD exit_code = 0;
        const bool queried = ::GetExitCodeProcess(process_.get(), &exit_code) != FALSE;
        process_.reset();

        detail::cancel_and_join(stdin_writer_);
        return queried ? static_cast<int>(exit_code) : -1;
    }

    inline bool Child::kill()
    {
        if (!process_ || ::WaitForSingleObject(process_.get(), 0) == WAIT_OBJECT_0)
        {
            return false;
        }
        return ::TerminateProcess(process_.get(), 1) != FALSE;
    }

#else

    inline Child::~Child()
    {
        cleanup();
    }

    inline Child::Child(Child&& other) noexcept
        : pid_(other.pid_),
          cached_status_(other.cached_status_),
          cached_exit_(other.cached_exit_),
          stdin_writer_(std::move(other.stdin_writer_)),
          stop_(std::move(other.stop_))
    {
        other.pid_ = -1;
        other.cached_exit_ = false;
    }

    inline Child& Child::operator=(Child&& other) noexcept
    {
        if (this != &other)
        {
            cleanup();
            pid_ = other.pid_;
            other.pid_ = -1;
            cached_status_ = other.cached_status_;
            cached_exit_ = other.cached_exit_;
            other.cached_exit_ = false;
            stdin_writer_ = std::move(other.stdin_writer_);
            stop_ = std::move(other.stop_);
        }
        return *this;
    }

    inline void Child::cleanup() noexcept
    {
        if (running())
        {
            (void)kill();
        }
        (void)wait();
    }

    inline std::optional<Child> Child::spawn(const Command& command, std::string* error)
    {
        std::string local_error;
        std::string& message = (error != nullptr) ? *error : local_error;
        message.clear();

        detail::LaunchPlan plan;
        if (!detail::prepare_launch(command, plan, message))
        {
            return std::nullopt;
        }

        detail::UniqueFd input_read;
        detail::UniqueFd input_write;
        detail::UniqueFd exec_read;
        detail::UniqueFd exec_write;
        detail::UniqueFd null_input;
        detail::UniqueFd null_output;

        if (!command.stdin_data.empty())
        {
            if (!detail::create_pipe(input_read, input_write, message))
            {
                return std::nullopt;
            }
        }
        else
        {
            null_input.reset(::open("/dev/null", O_RDONLY | O_CLOEXEC));
            if (!null_input)
            {
                message = std::string("打开 /dev/null 失败: ") + std::strerror(errno);
                return std::nullopt;
            }
        }

        null_output.reset(::open("/dev/null", O_WRONLY | O_CLOEXEC));
        if (!null_output)
        {
            message = std::string("打开 /dev/null 失败: ") + std::strerror(errno);
            return std::nullopt;
        }

        if (!detail::create_pipe(exec_read, exec_write, message))
        {
            return std::nullopt;
        }

        const int child_input = command.stdin_data.empty() ? null_input.get() : input_read.get();
        const int child_output = null_output.get();

        const ::pid_t pid = ::fork();
        if (pid < 0)
        {
            message = std::string("fork 失败: ") + std::strerror(errno);
            return std::nullopt;
        }

        if (pid == 0)
        {
            const int inherited[] = {input_read.get(), input_write.get(), null_input.get(), null_output.get(),
                                     exec_read.get()};
            detail::child_exec(plan, child_input, child_output, child_output, exec_write.get(), inherited,
                               sizeof(inherited) / sizeof(inherited[0]));
            ::_exit(127);  // child_exec 不会返回
        }

        input_read.reset();
        exec_write.reset();
        null_input.reset();
        null_output.reset();

        detail::ExecFailure exec_failure;
        const bool exec_failed = detail::wait_for_exec(exec_read.get(), exec_failure);
        exec_read.reset();

        if (exec_failed)
        {
            int status = 0;
            (void)::waitpid(pid, &status, 0);
            message = detail::exec_failure_text(exec_failure, command.program);
            return std::nullopt;
        }

        Child child;
        child.pid_ = pid;

        if (command.stdin_data.empty())
        {
            input_write.reset();
        }
        else
        {
            detail::set_nonblocking(input_write.get());
            std::shared_ptr<std::atomic<bool>> stop = child.stop_;
            try
            {
                child.stdin_writer_ =
                    std::thread([descriptor = std::move(input_write), data = command.stdin_data, stop]()
                                {
                                    detail::write_pipe_all(descriptor.get(), data, *stop);
                                });
            }
            catch (const std::exception& exception)
            {
                message = std::string("创建 stdin 线程失败: ") + exception.what();
                (void)::kill(pid, SIGKILL);
                int status = 0;
                (void)::waitpid(pid, &status, 0);
                child.pid_ = -1;  // 已回收，析构时不再等待
                return std::nullopt;
            }
        }

        return child;
    }

    inline bool Child::running() const
    {
        if (pid_ <= 0 || cached_exit_)
        {
            return false;
        }

        int status = 0;
        const ::pid_t waited = ::waitpid(pid_, &status, WNOHANG);
        if (waited == pid_)
        {
            // 提前探测到退出：缓存状态交给 wait()，避免二次 waitpid 拿不到状态。
            cached_exit_ = true;
            cached_status_ = status;
            return false;
        }
        return waited == 0;
    }

    inline int Child::wait()
    {
        if (pid_ <= 0)
        {
            return -1;
        }

        const ::pid_t target = pid_;
        int status = cached_status_;
        bool reaped = cached_exit_;

        if (!reaped)
        {
            ::pid_t waited = -1;
            do
            {
                waited = ::waitpid(target, &status, 0);
            } while (waited < 0 && errno == EINTR);
            reaped = (waited == target);
        }

        pid_ = -1;
        cached_exit_ = false;
        cached_status_ = 0;

        if (stop_ != nullptr)
        {
            stop_->store(true);
        }
        if (stdin_writer_.joinable())
        {
            stdin_writer_.join();
        }

        if (!reaped)
        {
            return -1;
        }
        if (WIFEXITED(status))
        {
            return WEXITSTATUS(status);
        }
        if (WIFSIGNALED(status))
        {
            return 128 + WTERMSIG(status);
        }
        return -1;
    }

    inline bool Child::kill()
    {
        if (!running())
        {
            return false;
        }
        return ::kill(pid_, SIGKILL) == 0;
    }

#endif
}
