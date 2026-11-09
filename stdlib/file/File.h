#pragma once

// File：跨平台、基于显式偏移的文件访问（只读 / 读写 / 追加）。
//
// 设计约定：
//   * 所有读写都带绝对偏移：POSIX 用 pread/pwrite，Windows 用 ReadFile/WriteFile 配
//     OVERLAPPED 偏移。不依赖也不修改句柄的文件指针，因此同一 File 上的多个 read 可以并发。
//   * 不使用用户态缓冲：write/append 返回时数据已交给操作系统；flush() 是空操作，
//     sync() 才执行 fsync/FlushFileBuffers。
//   * 打开模式：read 要求文件存在；write 创建或清空；append 创建但不截断。
//     read 只读（Windows: GENERIC_READ，POSIX: O_RDONLY）；write/append 读写
//     （Windows: GENERIC_READ|GENERIC_WRITE，POSIX: O_RDWR）。
//   * 读越界按 clamp 处理：末端超过文件大小时只读到 EOF；start 已到达或越过文件末尾时返回
//     Error::eof。空区间与空缓冲区是合法的空操作，返回 Error::none。
//   * 写越界会扩展文件（空洞读出为零）；数据长于区间时只写区间长度。
//   * 短读/短写、EINTR、ERROR_IO_PENDING、ERROR_HANDLE_EOF 都在内部循环处理。
//   * 错误一律通过 Error 返回，不抛异常；内部容器/字符串分配失败除外，
//     此时遵循标准库容器语义（std::bad_alloc）。
//   * 句柄遵循 RAII；File 可移动、不可拷贝；close() 幂等。
//   * 路径在 Windows 上按 UTF-8 解释，在 POSIX 上按系统编码原样传递。
//   * 64 位偏移：超过 2 GiB 的位置在 Windows 上由 OVERLAPPED.OffsetHigh 承载，
//     在 POSIX 上由 off_t 承载；超出平台上限返回 Error::too_large。
//   * 线程安全：read/write 使用定位 I/O，互不依赖文件指针；read_line 的行号缓存与
//     append 的“取大小 + 写入”序列由内部互斥量串行化。对同一对象的 close()/移动
//     不能与其它线程的调用并发。

#include "Range.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace coordination::file
{
    // 定位基准。本模块的读写都使用显式绝对偏移，内部不维护文件指针；
    // 该枚举描述调用方自行维护游标时的相对基准（例如把 end 换算成绝对偏移）。
    enum class SeekOrigin
    {
        begin,
        current,
        end
    };

    // 打开模式。
    //   read   ：只读，文件必须已存在。
    //   write  ：读写，文件被创建或清空。
    //   append ：读写，文件被创建但不截断；append() 每次写到当前末尾。
    enum class OpenMode
    {
        read,
        write,
        append
    };

    // 错误码。
    //   none            ：成功。
    //   not_found       ：路径不存在（或为空、含内嵌 '\0'）。
    //   permission_denied：操作系统拒绝访问，或对只读模式调用写操作。
    //   invalid_range   ：Range 非法（start > end）或必需的指针参数为空。
    //   io_error        ：其它 I/O 失败、句柄已关闭，或目标不是普通文件。
    //   eof             ：读取起点已到达或越过文件末尾。
    //   locked          ：文件被其它句柄锁定 / 共享冲突。
    //   too_large       ：偏移超出平台上限，或单行超过 kMaxLineLength。
    enum class Error
    {
        none,
        not_found,
        permission_denied,
        invalid_range,
        io_error,
        eof,
        locked,
        too_large
    };

    // 单行长度上限：超过后 read_line 返回 Error::too_large，避免无换行大文件耗尽内存。
    inline constexpr std::uint64_t kMaxLineLength = 64ull * 1024ull * 1024ull;

    // 按行读取的结果。
    //   number：1 起始的行号（offset 之前 '\n' 的个数 + 1）。
    //   range ：行内容的字节区间，不含行尾的 '\n' 或 "\r\n"。
    //   text  ：行内容，不含换行符；'\r' 仅在紧邻 '\n' 时被去掉，单独的 '\r' 保留。
    struct Line
    {
        std::uint64_t number = 0;
        Range range;
        std::string text;
    };

    namespace detail
    {
        // 单次系统调用最多传输 1 GiB：规避 DWORD/ssize_t 上限，并限制单次阻塞时长。
        inline constexpr std::size_t kMaxTransfer = 1ull << 30;

#if defined(_WIN32)
        using native_handle = HANDLE;

        // CreateFileW 失败返回 INVALID_HANDLE_VALUE；成功句柄不会是 nullptr。
        inline constexpr native_handle invalid_handle = nullptr;

        // Win32 错误码 → 模块错误码。
        [[nodiscard]] inline Error error_from_win32(DWORD code) noexcept
        {
            switch (code)
            {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
            case ERROR_INVALID_NAME:
            case ERROR_BAD_PATHNAME:
            case ERROR_INVALID_DRIVE:
                return Error::not_found;
            case ERROR_ACCESS_DENIED:
            case ERROR_PRIVILEGE_NOT_HELD:
            case ERROR_WRITE_PROTECT:
                return Error::permission_denied;
            case ERROR_SHARING_VIOLATION:
            case ERROR_LOCK_VIOLATION:
            case ERROR_DRIVE_LOCKED:
                return Error::locked;
            case ERROR_FILE_TOO_LARGE:
                return Error::too_large;
            case ERROR_INVALID_PARAMETER:
                return Error::invalid_range;
            default:
                return Error::io_error;
            }
        }

        // 文件偏移必须能放进 64 位有符号整数（OVERLAPPED 的 Offset/OffsetHigh 组合）。
        [[nodiscard]] inline bool fits_offset(std::uint64_t offset) noexcept
        {
            return offset <= static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
        }

        // UTF-8 → UTF-16；失败（非法字节序列或超长）返回 nullopt。
        [[nodiscard]] inline std::optional<std::wstring> to_wide_path(const std::string& path)
        {
            if (path.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            {
                return std::nullopt;
            }

            const int length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(),
                static_cast<int>(path.size()), nullptr, 0);
            if (length <= 0)
            {
                return std::nullopt;
            }

            std::wstring wide(static_cast<std::size_t>(length), L'\0');
            if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(),
                    static_cast<int>(path.size()), wide.data(), length) <= 0)
            {
                return std::nullopt;
            }

            return wide;
        }

        // 从 offset 起读取 length 字节；*transferred 返回实际读到的字节数。
        // 到达文件末尾不是错误：返回 Error::none，*transferred 可能小于 length。
        [[nodiscard]] inline Error read_at(native_handle handle, std::uint64_t offset, char* data,
            std::size_t length, std::size_t* transferred) noexcept
        {
            *transferred = 0;
            while (length > 0)
            {
                if (offset > (std::numeric_limits<std::uint64_t>::max)() - static_cast<std::uint64_t>(length))
                {
                    return Error::too_large;
                }

                OVERLAPPED overlapped{};
                overlapped.Offset = static_cast<DWORD>(offset & 0xFFFF'FFFFull);
                overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);

                const std::size_t request = length < kMaxTransfer ? length : kMaxTransfer;
                DWORD done = 0;
                if (!::ReadFile(handle, data, static_cast<DWORD>(request), &done, &overlapped))
                {
                    const DWORD code = ::GetLastError();
                    if (code == ERROR_HANDLE_EOF)
                    {
                        // 恰好从 EOF 开始读时 ReadFile 报这个“错误”，语义上只是没有数据。
                        return Error::none;
                    }

                    if (code != ERROR_IO_PENDING)
                    {
                        return error_from_win32(code);
                    }

                    // 句柄以 FILE_FLAG_OVERLAPPED 打开时可能返回 pending；同步等待结果。
                    if (!::GetOverlappedResult(handle, &overlapped, &done, TRUE))
                    {
                        const DWORD wait_code = ::GetLastError();
                        if (wait_code == ERROR_HANDLE_EOF)
                        {
                            return Error::none;
                        }

                        return error_from_win32(wait_code);
                    }
                }

                if (done == 0)
                {
                    // 防御：没有进展就当作 EOF，避免死循环。
                    return Error::none;
                }

                *transferred += static_cast<std::size_t>(done);
                offset += static_cast<std::uint64_t>(done);
                data += static_cast<std::size_t>(done);
                length -= static_cast<std::size_t>(done);
            }

            return Error::none;
        }

        // 从 offset 起写入 length 字节；循环处理短写。
        [[nodiscard]] inline Error write_at(native_handle handle, std::uint64_t offset, const char* data,
            std::size_t length, std::size_t* transferred) noexcept
        {
            *transferred = 0;
            while (length > 0)
            {
                if (offset > (std::numeric_limits<std::uint64_t>::max)() - static_cast<std::uint64_t>(length))
                {
                    return Error::too_large;
                }

                OVERLAPPED overlapped{};
                overlapped.Offset = static_cast<DWORD>(offset & 0xFFFF'FFFFull);
                overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);

                const std::size_t request = length < kMaxTransfer ? length : kMaxTransfer;
                DWORD done = 0;
                if (!::WriteFile(handle, data, static_cast<DWORD>(request), &done, &overlapped))
                {
                    const DWORD code = ::GetLastError();
                    if (code != ERROR_IO_PENDING)
                    {
                        return error_from_win32(code);
                    }

                    if (!::GetOverlappedResult(handle, &overlapped, &done, TRUE))
                    {
                        return error_from_win32(::GetLastError());
                    }
                }

                if (done == 0)
                {
                    return Error::io_error;
                }

                *transferred += static_cast<std::size_t>(done);
                offset += static_cast<std::uint64_t>(done);
                data += static_cast<std::size_t>(done);
                length -= static_cast<std::size_t>(done);
            }

            return Error::none;
        }
#else
        using native_handle = int;
        inline constexpr native_handle invalid_handle = -1;

        [[nodiscard]] inline Error error_from_errno(int code) noexcept
        {
            switch (code)
            {
            case ENOENT:
            case ENOTDIR:
                return Error::not_found;
            case EACCES:
            case EPERM:
            case EROFS:
                return Error::permission_denied;
            case EINVAL:
                return Error::invalid_range;
            case EFBIG:
            case EOVERFLOW:
                return Error::too_large;
            default:
                return Error::io_error;
            }
        }

        // 文件偏移必须能放进 off_t（32 位平台可能只有 32 位）。
        [[nodiscard]] inline bool fits_offset(std::uint64_t offset) noexcept
        {
            return offset <= static_cast<std::uint64_t>((std::numeric_limits<off_t>::max)());
        }

        // 从 offset 起读取 length 字节；EINTR 重试，读到 EOF 正常返回。
        [[nodiscard]] inline Error read_at(native_handle handle, std::uint64_t offset, char* data,
            std::size_t length, std::size_t* transferred) noexcept
        {
            *transferred = 0;
            while (length > 0)
            {
                if (!fits_offset(offset))
                {
                    return Error::too_large;
                }

                const std::size_t request = length < kMaxTransfer ? length : kMaxTransfer;
                const ssize_t done = ::pread(handle, data, request, static_cast<off_t>(offset));
                if (done < 0)
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }

                    return error_from_errno(errno);
                }

                if (done == 0)
                {
                    // EOF：没有更多数据，已读到的字节保留在 *transferred 中。
                    return Error::none;
                }

                *transferred += static_cast<std::size_t>(done);
                offset += static_cast<std::uint64_t>(done);
                data += static_cast<std::size_t>(done);
                length -= static_cast<std::size_t>(done);
            }

            return Error::none;
        }

        // 从 offset 起写入 length 字节；EINTR 重试，循环处理短写。
        [[nodiscard]] inline Error write_at(native_handle handle, std::uint64_t offset, const char* data,
            std::size_t length, std::size_t* transferred) noexcept
        {
            *transferred = 0;
            while (length > 0)
            {
                if (!fits_offset(offset))
                {
                    return Error::too_large;
                }

                const std::size_t request = length < kMaxTransfer ? length : kMaxTransfer;
                const ssize_t done = ::pwrite(handle, data, request, static_cast<off_t>(offset));
                if (done < 0)
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }

                    return error_from_errno(errno);
                }

                if (done == 0)
                {
                    return Error::io_error;
                }

                *transferred += static_cast<std::size_t>(done);
                offset += static_cast<std::uint64_t>(done);
                data += static_cast<std::size_t>(done);
                length -= static_cast<std::size_t>(done);
            }

            return Error::none;
        }
#endif
    }

    // 文件句柄的 RAII 封装；可移动、不可拷贝。
    class File
    {
    public:
        File() noexcept = default;
        ~File();

        File(File&& other) noexcept;
        File& operator=(File&& other) noexcept;

        File(const File&) = delete;
        File& operator=(const File&) = delete;

        // 打开文件。error 非空时总是写入结果（成功为 Error::none）；
        // 失败返回 nullopt。Windows 上 path 按 UTF-8 解释。
        [[nodiscard]] static std::optional<File> open(const std::string& path, OpenMode mode,
            Error* error = nullptr);

        [[nodiscard]] bool is_open() const noexcept;

        // 关闭句柄并释放锁；重复调用无副作用。关闭后 size() 返回 0，其它操作返回 io_error。
        void close() noexcept;

        // 当前文件大小；句柄无效或查询失败时返回 0，不区分错误。
        [[nodiscard]] std::uint64_t size() const noexcept;

        // 把 [range.start, range.end) 读入 buffer，*bytes_read 为实际字节数。
        //
        // 行为：
        //   * start > end               → invalid_range，*bytes_read = 0。
        //   * 空区间或空 buffer         → none，*bytes_read = 0（合法的空操作）。
        //   * start >= size()           → eof，*bytes_read = 0。
        //   * end > size()              → 按 clamp 只读到 EOF，不是错误。
        //   * 实际读取长度取 range.size() 与 buffer.size() 的较小值；只读入这么多，
        //     剩余部分不报错（调用方可据此判断“缓冲区不足”）。
        //   * 读取途中被并发截断（实际字节数少于请求）→ eof，*bytes_read 为已读字节数。
        [[nodiscard]] Error read(Range range, std::span<char> buffer, std::size_t* bytes_read = nullptr) const noexcept;

        // 读取整个区间；越界按 clamp。error 非空时写入结果。
        // 失败返回空 vector；若返回部分数据（并发截断）则同时给出 eof。
        [[nodiscard]] std::vector<char> read_range(Range range, Error* error = nullptr) const;

        // 把 data 写入 [range.start, range.end)，*bytes_written 为实际字节数。
        //
        // 行为：
        //   * 只读模式                  → permission_denied，*bytes_written = 0。
        //   * start > end               → invalid_range。
        //   * 空区间或空 data           → none，*bytes_written = 0。
        //   * data 长于区间             → 只写区间长度，不报错。
        //   * end > size()              → 扩展文件，空洞读出为零。
        //   * 偏移超出平台上限          → too_large。
        //   * 短写由内部循环处理；失败时 *bytes_written 为已写入的字节数。
        [[nodiscard]] Error write(Range range, std::string_view data, std::size_t* bytes_written = nullptr);

        // 追加到当前文件末尾（write/append 模式）。跨进程并发追加不保证原子，
        // 同一 File 内部由互斥量串行化；失败时文件可能已追加了部分字节（短写内部已重试）。
        [[nodiscard]] Error append(std::string_view data);

        // 从 offset 读取一行；next_offset 返回下一行的起点（EOF 时为文件大小）。
        //
        // 行为：
        //   * line == nullptr           → invalid_range。
        //   * 支持 "\n" 与 "\r\n"；text 不含换行符，range 覆盖 text 的内容。
        //   * offset > size()           → invalid_range；offset == size() → eof。
        //   * 末行没有换行符时 text 为剩余内容，next_offset == size()。
        //   * number 为 1 起始行号：offset 之前 '\n' 的个数 + 1；顺序读取会命中内部缓存。
        //   * 单行超过 kMaxLineLength   → too_large；失败时不修改 *line。
        [[nodiscard]] Error read_line(std::uint64_t offset, Line* line, std::uint64_t* next_offset = nullptr) const;

        // 本模块不使用用户态缓冲，flush 是空操作（数据在 write/append 返回时已交给操作系统）。
        [[nodiscard]] Error flush() noexcept;

        // 把操作系统缓存刷入磁盘（fsync / FlushFileBuffers）。
        // 只读模式没有待刷数据，直接返回 none（Windows 的 FlushFileBuffers 也要求写权限）。
        [[nodiscard]] Error sync() noexcept;

        // 对整个文件加锁（非阻塞）：无法立即获得时返回 locked。
        // 独占锁与共享锁分别对应 LockFileEx 的独占/共享标志与 flock 的 LOCK_EX/LOCK_SH；
        // 同一 File 重复加锁的平台相关，unlock() 释放整文件锁。
        [[nodiscard]] Error lock(bool exclusive = true) noexcept;

        // 释放整文件锁；未持锁时也返回 none（幂等）。
        [[nodiscard]] Error unlock() noexcept;

    private:
        File(detail::native_handle handle, OpenMode mode) noexcept;

        // 求 offset 所在行的 1 起始行号；命中缓存时无需扫描前缀。
        [[nodiscard]] Error line_number_at(std::uint64_t offset, std::uint64_t* number) const;

        // 内容变化后行号可能改变，使顺序读取缓存失效。
        void invalidate_line_cache() noexcept;

        detail::native_handle handle_ = detail::invalid_handle;
        OpenMode mode_ = OpenMode::read;
        mutable std::uint64_t line_cache_offset_ = 0;
        mutable std::uint64_t line_cache_number_ = 1;
        mutable std::mutex mutex_;
    };

    inline File::File(detail::native_handle handle, OpenMode mode) noexcept
        : handle_(handle)
        , mode_(mode)
    {
    }

    inline File::~File()
    {
        close();
    }

    inline File::File(File&& other) noexcept
        : handle_(other.handle_)
        , mode_(other.mode_)
        , line_cache_offset_(other.line_cache_offset_)
        , line_cache_number_(other.line_cache_number_)
    {
        other.handle_ = detail::invalid_handle;
        other.mode_ = OpenMode::read;
        other.line_cache_offset_ = 0;
        other.line_cache_number_ = 1;
    }

    inline File& File::operator=(File&& other) noexcept
    {
        if (this != &other)
        {
            close();
            handle_ = other.handle_;
            mode_ = other.mode_;
            line_cache_offset_ = other.line_cache_offset_;
            line_cache_number_ = other.line_cache_number_;
            other.handle_ = detail::invalid_handle;
            other.mode_ = OpenMode::read;
            other.line_cache_offset_ = 0;
            other.line_cache_number_ = 1;
        }

        return *this;
    }

    inline std::optional<File> File::open(const std::string& path, OpenMode mode, Error* error)
    {
        const auto fail = [error](Error code) noexcept -> std::optional<File>
        {
            if (error != nullptr)
            {
                *error = code;
            }

            return std::nullopt;
        };

        if (path.empty() || path.find('\0') != std::string::npos)
        {
            return fail(Error::not_found);
        }

#if defined(_WIN32)
        DWORD access = 0;
        DWORD creation = 0;
        switch (mode)
        {
        case OpenMode::read:
            access = GENERIC_READ;
            creation = OPEN_EXISTING;
            break;
        case OpenMode::write:
            access = GENERIC_READ | GENERIC_WRITE;
            creation = CREATE_ALWAYS;
            break;
        case OpenMode::append:
            access = GENERIC_READ | GENERIC_WRITE;
            creation = OPEN_ALWAYS;
            break;
        default:
            return fail(Error::invalid_range);
        }

        const std::optional<std::wstring> wide = detail::to_wide_path(path);
        if (!wide.has_value())
        {
            return fail(Error::io_error);
        }

        // 不加 FILE_FLAG_OVERLAPPED：同步句柄配合 OVERLAPPED 偏移即可完成定位读写，
        // 同时让 LockFileEx 保持同步语义。
        const HANDLE handle = ::CreateFileW(wide->c_str(), access,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, creation,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return fail(detail::error_from_win32(::GetLastError()));
        }
#else
        int flags = 0;
        switch (mode)
        {
        case OpenMode::read:
            flags = O_RDONLY;
            break;
        case OpenMode::write:
            flags = O_RDWR | O_CREAT | O_TRUNC;
            break;
        case OpenMode::append:
            flags = O_RDWR | O_CREAT;
            break;
        default:
            return fail(Error::invalid_range);
        }

#if defined(O_CLOEXEC)
        flags |= O_CLOEXEC;
#endif
        const int handle = ::open(path.c_str(), flags, 0666);
        if (handle < 0)
        {
            return fail(detail::error_from_errno(errno));
        }

        // POSIX 允许 open 目录，Windows 的 CreateFileW 会拒绝；这里显式对齐为 permission_denied。
        struct stat information{};
        if (::fstat(handle, &information) != 0)
        {
            static_cast<void>(::close(handle));
            return fail(Error::io_error);
        }

        if (S_ISDIR(information.st_mode))
        {
            static_cast<void>(::close(handle));
            return fail(Error::permission_denied);
        }
#endif

        if (error != nullptr)
        {
            *error = Error::none;
        }

        return File(handle, mode);
    }

    inline bool File::is_open() const noexcept
    {
        return handle_ != detail::invalid_handle;
    }

    inline void File::close() noexcept
    {
        if (!is_open())
        {
            return;
        }

        const detail::native_handle handle = handle_;
        handle_ = detail::invalid_handle;
        mode_ = OpenMode::read;
        line_cache_offset_ = 0;
        line_cache_number_ = 1;

#if defined(_WIN32)
        ::CloseHandle(handle);
#else
        static_cast<void>(::close(handle));
#endif
    }

    inline std::uint64_t File::size() const noexcept
    {
        if (!is_open())
        {
            return 0;
        }

#if defined(_WIN32)
        LARGE_INTEGER value{};
        if (!::GetFileSizeEx(handle_, &value) || value.QuadPart < 0)
        {
            return 0;
        }

        return static_cast<std::uint64_t>(value.QuadPart);
#else
        struct stat information{};
        if (::fstat(handle_, &information) != 0 || information.st_size < 0)
        {
            return 0;
        }

        return static_cast<std::uint64_t>(information.st_size);
#endif
    }

    inline Error File::read(Range range, std::span<char> buffer, std::size_t* bytes_read) const noexcept
    {
        if (bytes_read != nullptr)
        {
            *bytes_read = 0;
        }

        if (!is_open())
        {
            return Error::io_error;
        }

        if (range.start > range.end)
        {
            return Error::invalid_range;
        }

        if (range.empty() || buffer.empty())
        {
            return Error::none;
        }

        const std::uint64_t total = size();
        if (range.start >= total)
        {
            return Error::eof;
        }

        const std::uint64_t available = total - range.start;
        const std::uint64_t buffer_size = static_cast<std::uint64_t>(buffer.size());
        const std::uint64_t requested = range.size() < buffer_size ? range.size() : buffer_size;
        const std::uint64_t length = requested < available ? requested : available;

        std::size_t transferred = 0;
        const Error error = detail::read_at(handle_, range.start, buffer.data(),
            static_cast<std::size_t>(length), &transferred);
        if (bytes_read != nullptr)
        {
            *bytes_read = transferred;
        }

        if (error != Error::none)
        {
            return error;
        }

        if (transferred < static_cast<std::size_t>(length))
        {
            // 文件在读取期间被截断：数据比请求的少，按 EOF 汇报并保留已读字节数。
            return Error::eof;
        }

        return Error::none;
    }

    inline std::vector<char> File::read_range(Range range, Error* error) const
    {
        if (error != nullptr)
        {
            *error = Error::none;
        }

        if (!is_open())
        {
            if (error != nullptr)
            {
                *error = Error::io_error;
            }

            return {};
        }

        if (range.start > range.end)
        {
            if (error != nullptr)
            {
                *error = Error::invalid_range;
            }

            return {};
        }

        if (range.empty())
        {
            return {};
        }

        const std::uint64_t total = size();
        if (range.start >= total)
        {
            if (error != nullptr)
            {
                *error = Error::eof;
            }

            return {};
        }

        const std::uint64_t available = total - range.start;
        const std::uint64_t length = range.size() < available ? range.size() : available;
        if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t))
        {
            if (length > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()))
            {
                if (error != nullptr)
                {
                    *error = Error::too_large;
                }

                return {};
            }
        }

        std::vector<char> data(static_cast<std::size_t>(length));
        std::size_t transferred = 0;
        const Error read_error = read(Range::from_size(range.start, length), data, &transferred);
        if (read_error != Error::none && read_error != Error::eof)
        {
            if (error != nullptr)
            {
                *error = read_error;
            }

            return {};
        }

        data.resize(transferred);
        if (error != nullptr)
        {
            *error = read_error;
        }

        return data;
    }

    inline Error File::write(Range range, std::string_view data, std::size_t* bytes_written)
    {
        if (bytes_written != nullptr)
        {
            *bytes_written = 0;
        }

        if (!is_open())
        {
            return Error::io_error;
        }

        if (mode_ == OpenMode::read)
        {
            return Error::permission_denied;
        }

        if (range.start > range.end)
        {
            return Error::invalid_range;
        }

        if (range.empty() || data.empty())
        {
            return Error::none;
        }

        const std::uint64_t count = range.size() < static_cast<std::uint64_t>(data.size())
            ? range.size()
            : static_cast<std::uint64_t>(data.size());

        // count <= range.size()，因此 start + count 不会溢出 uint64；
        // 只需检查最后一个字节是否落在平台偏移范围内。
        if (!detail::fits_offset(range.start) || !detail::fits_offset(range.start + count - 1))
        {
            return Error::too_large;
        }

        std::lock_guard<std::mutex> guard(mutex_);
        std::size_t transferred = 0;
        const Error error = detail::write_at(handle_, range.start, data.data(),
            static_cast<std::size_t>(count), &transferred);
        if (bytes_written != nullptr)
        {
            *bytes_written = transferred;
        }

        // 即使写入失败也可能已经部分落盘，行号缓存一律失效。
        invalidate_line_cache();
        return error;
    }

    inline Error File::append(std::string_view data)
    {
        if (!is_open())
        {
            return Error::io_error;
        }

        if (mode_ == OpenMode::read)
        {
            return Error::permission_denied;
        }

        if (data.empty())
        {
            return Error::none;
        }

        std::lock_guard<std::mutex> guard(mutex_);
        const std::uint64_t total = size();
        if (static_cast<std::uint64_t>(data.size())
            > (std::numeric_limits<std::uint64_t>::max)() - total)
        {
            return Error::too_large;
        }

        const std::uint64_t end = total + static_cast<std::uint64_t>(data.size());
        if (!detail::fits_offset(end - 1))
        {
            return Error::too_large;
        }

        std::size_t transferred = 0;
        const Error error = detail::write_at(handle_, total, data.data(), data.size(), &transferred);
        invalidate_line_cache();
        return error;
    }

    inline Error File::read_line(std::uint64_t offset, Line* line, std::uint64_t* next_offset) const
    {
        if (line == nullptr)
        {
            return Error::invalid_range;
        }

        if (!is_open())
        {
            return Error::io_error;
        }

        const std::uint64_t total = size();
        if (offset > total)
        {
            return Error::invalid_range;
        }

        if (offset == total)
        {
            return Error::eof;
        }

        std::lock_guard<std::mutex> guard(mutex_);

        std::uint64_t number = 1;
        const Error number_error = line_number_at(offset, &number);
        if (number_error != Error::none)
        {
            return number_error;
        }

        std::string text;
        std::uint64_t position = offset;
        bool terminated = false;
        std::array<char, 64 * 1024> buffer{};
        while (position < total)
        {
            const std::uint64_t remaining = total - position;
            const std::size_t request = remaining < static_cast<std::uint64_t>(buffer.size())
                ? static_cast<std::size_t>(remaining)
                : buffer.size();
            std::size_t transferred = 0;
            const Error read_error = detail::read_at(handle_, position, buffer.data(), request, &transferred);
            if (read_error != Error::none)
            {
                return read_error;
            }

            if (transferred == 0)
            {
                break;
            }

            const char* newline = static_cast<const char*>(
                std::memchr(buffer.data(), '\n', transferred));
            if (newline != nullptr)
            {
                const std::size_t index = static_cast<std::size_t>(newline - buffer.data());
                if (text.size() + index > kMaxLineLength)
                {
                    return Error::too_large;
                }

                text.append(buffer.data(), index);
                position += static_cast<std::uint64_t>(index);
                terminated = true;
                break;
            }

            if (text.size() + transferred > kMaxLineLength)
            {
                return Error::too_large;
            }

            text.append(buffer.data(), transferred);
            position += static_cast<std::uint64_t>(transferred);
        }

        std::uint64_t content_end = position;
        if (terminated)
        {
            // position 指向 '\n'：行内容到它为止，并去掉 CRLF 的 '\r'。
            if (!text.empty() && text.back() == '\r')
            {
                text.pop_back();
                --content_end;
            }

            ++position;
        }

        line->number = number;
        line->range = Range{ offset, content_end };
        line->text = std::move(text);

        if (next_offset != nullptr)
        {
            *next_offset = position;
        }

        if (terminated)
        {
            // 顺序读取下一行时可直接复用行号，无需重新扫描前缀。
            line_cache_offset_ = position;
            line_cache_number_ = number + 1;
        }

        return Error::none;
    }

    inline Error File::flush() noexcept
    {
        if (!is_open())
        {
            return Error::io_error;
        }

        return Error::none;
    }

    inline Error File::sync() noexcept
    {
        if (!is_open())
        {
            return Error::io_error;
        }

        if (mode_ == OpenMode::read)
        {
            return Error::none;
        }

#if defined(_WIN32)
        if (!::FlushFileBuffers(handle_))
        {
            return detail::error_from_win32(::GetLastError());
        }
#else
        if (::fsync(handle_) != 0)
        {
            return detail::error_from_errno(errno);
        }
#endif

        return Error::none;
    }

    inline Error File::lock(bool exclusive) noexcept
    {
        if (!is_open())
        {
            return Error::io_error;
        }

#if defined(_WIN32)
        // 长度 MAXDWORD/MAXDWORD 表示“从偏移 0 到文件末尾之后”，即整文件锁。
        OVERLAPPED overlapped{};
        DWORD flags = LOCKFILE_FAIL_IMMEDIATELY;
        if (exclusive)
        {
            flags |= LOCKFILE_EXCLUSIVE_LOCK;
        }

        if (!::LockFileEx(handle_, flags, 0, MAXDWORD, MAXDWORD, &overlapped))
        {
            const DWORD code = ::GetLastError();
            if (code == ERROR_LOCK_VIOLATION || code == ERROR_SHARING_VIOLATION
                || code == ERROR_IO_PENDING)
            {
                return Error::locked;
            }

            return detail::error_from_win32(code);
        }
#else
        const int operation = (exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB;
        if (::flock(handle_, operation) != 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                return Error::locked;
            }

            return detail::error_from_errno(errno);
        }
#endif

        return Error::none;
    }

    inline Error File::unlock() noexcept
    {
        if (!is_open())
        {
            return Error::io_error;
        }

#if defined(_WIN32)
        OVERLAPPED overlapped{};
        if (!::UnlockFileEx(handle_, 0, MAXDWORD, MAXDWORD, &overlapped))
        {
            const DWORD code = ::GetLastError();
            if (code == ERROR_NOT_LOCKED)
            {
                // 未持锁时解锁视为成功，与 POSIX 的 flock(LOCK_UN) 一致。
                return Error::none;
            }

            return detail::error_from_win32(code);
        }
#else
        if (::flock(handle_, LOCK_UN) != 0)
        {
            return detail::error_from_errno(errno);
        }
#endif

        return Error::none;
    }

    inline Error File::line_number_at(std::uint64_t offset, std::uint64_t* number) const
    {
        if (offset == line_cache_offset_)
        {
            *number = line_cache_number_;
            return Error::none;
        }

        std::uint64_t newlines = 0;
        std::uint64_t position = 0;
        std::array<char, 64 * 1024> buffer{};
        while (position < offset)
        {
            const std::uint64_t remaining = offset - position;
            const std::size_t request = remaining < static_cast<std::uint64_t>(buffer.size())
                ? static_cast<std::size_t>(remaining)
                : buffer.size();
            std::size_t transferred = 0;
            const Error error = detail::read_at(handle_, position, buffer.data(), request, &transferred);
            if (error != Error::none)
            {
                return error;
            }

            if (transferred == 0)
            {
                break;
            }

            for (std::size_t index = 0; index < transferred; ++index)
            {
                if (buffer[index] == '\n')
                {
                    ++newlines;
                }
            }

            position += static_cast<std::uint64_t>(transferred);
        }

        line_cache_offset_ = offset;
        line_cache_number_ = newlines + 1;
        *number = line_cache_number_;
        return Error::none;
    }

    inline void File::invalidate_line_cache() noexcept
    {
        line_cache_offset_ = 0;
        line_cache_number_ = 1;
    }
}
