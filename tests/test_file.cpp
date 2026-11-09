// coordination::file 自包含测试：不依赖第三方测试框架。
// 覆盖 Range 语义、打开/移动/关闭、范围读写与 clamp、非法区间、EOF、短写、
// 随机偏移读写、按行读取（LF/CRLF/末行无换行/空行/长行）、append、flush/sync、
// 独占/共享锁，以及 >2 GiB 的 64 位稀疏文件偏移。全部通过时打印 PASS 并返回 0。

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../stdlib/file/File.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

// 失败立即打印原因并退出；全部通过时只输出一行 PASS。
// 使用可变参数宏，允许断言表达式里出现 Range{0, 5} 这类含逗号的初始化。
#define CHECK(...) \
    do \
    { \
        if (!(__VA_ARGS__)) \
        { \
            std::cout << "FAIL: " << #__VA_ARGS__ << " (line " << __LINE__ << ")" << std::endl; \
            return 1; \
        } \
    } while (false)

using coordination::file::Error;
using coordination::file::File;
using coordination::file::Line;
using coordination::file::OpenMode;
using coordination::file::Range;

namespace
{
    // 测试目录：构造时在系统临时目录下建立唯一子目录，析构时递归删除。
    struct TempDirectory
    {
        TempDirectory()
        {
            std::error_code error;
            std::filesystem::path base = std::filesystem::temp_directory_path(error);
            if (error || base.empty())
            {
                base = std::filesystem::current_path(error);
            }

            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path = base / ("coordination_file_test_" + std::to_string(stamp));

            std::filesystem::create_directories(path, error);
        }

        ~TempDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }

        TempDirectory(const TempDirectory&) = delete;
        TempDirectory& operator=(const TempDirectory&) = delete;

        std::filesystem::path path;
    };

    // File::open 接受 UTF-8 路径；Windows 上不能直接使用 path::string() 的 ANSI 编码。
    std::string to_utf8(const std::filesystem::path& path)
    {
        const std::u8string text = path.u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    }

    std::optional<File> open_file(const std::filesystem::path& path, OpenMode mode, Error* error = nullptr)
    {
        return File::open(to_utf8(path), mode, error);
    }

    std::vector<char> bytes(std::string_view text)
    {
        return std::vector<char>(text.begin(), text.end());
    }

    std::string text_of(const std::vector<char>& data)
    {
        return std::string(data.begin(), data.end());
    }

    // 以写模式创建或清空文件并写入全部内容。
    bool write_text(const std::filesystem::path& path, std::string_view text)
    {
        Error error = Error::none;
        std::optional<File> file = open_file(path, OpenMode::write, &error);
        if (!file.has_value() || error != Error::none)
        {
            return false;
        }

        std::size_t written = 0;
        return file->write(Range::from_size(0, text.size()), text, &written) == Error::none
            && written == text.size();
    }

    std::optional<std::string> read_text(const std::filesystem::path& path)
    {
        Error error = Error::none;
        std::optional<File> file = open_file(path, OpenMode::read, &error);
        if (!file.has_value() || error != Error::none)
        {
            return std::nullopt;
        }

        const std::vector<char> data = file->read_range(Range::from_size(0, file->size()), &error);
        if (error != Error::none)
        {
            return std::nullopt;
        }

        return text_of(data);
    }

    // 创建指定长度的稀疏文件：NTFS 需要 FSCTL_SET_SPARSE，否则扩展会真实占用磁盘。
    bool create_sparse_file(const std::filesystem::path& path, std::uint64_t length)
    {
#if defined(_WIN32)
        if (length > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
        {
            return false;
        }

        const std::wstring wide = path.wstring();
        HANDLE handle = ::CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        DWORD returned = 0;
        const BOOL sparse = ::DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0,
            &returned, nullptr);

        LARGE_INTEGER distance{};
        distance.QuadPart = static_cast<LONGLONG>(length);
        const BOOL moved = ::SetFilePointerEx(handle, distance, nullptr, FILE_BEGIN);
        const BOOL resized = ::SetEndOfFile(handle);
        ::CloseHandle(handle);

        return sparse != FALSE && moved != FALSE && resized != FALSE;
#else
        if (length > static_cast<std::uint64_t>((std::numeric_limits<off_t>::max)()))
        {
            return false;
        }

        const int handle = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
        if (handle < 0)
        {
            return false;
        }

        const bool resized = ::ftruncate(handle, static_cast<off_t>(length)) == 0;
        static_cast<void>(::close(handle));
        return resized;
#endif
    }

    int test_range();
    int test_open_move_close(const std::filesystem::path& root);
    int test_range_io(const std::filesystem::path& root);
    int test_write_extend(const std::filesystem::path& root);
    int test_append_modes(const std::filesystem::path& root);
    int test_lines(const std::filesystem::path& root);
    int test_flush_sync(const std::filesystem::path& root);
    int test_random_access(const std::filesystem::path& root);
    int test_locking(const std::filesystem::path& root);
    int test_large_offsets(const std::filesystem::path& root);

    int test_range()
    {
        const Range empty;
        CHECK(empty.start == 0);
        CHECK(empty.end == 0);
        CHECK(empty.empty());
        CHECK(empty.size() == 0);
        CHECK(!empty.contains(0));

        const Range range{10, 15};
        CHECK(!range.empty());
        CHECK(range.size() == 5);
        CHECK(range.contains(10));
        CHECK(range.contains(14));
        CHECK(!range.contains(15));
        CHECK(!range.contains(9));

        // 倒置区间按空处理，不做无符号回绕。
        const Range inverted{7, 3};
        CHECK(inverted.empty());
        CHECK(inverted.size() == 0);
        CHECK(!inverted.contains(5));

        CHECK(Range::from_size(4, 6) == Range{4, 10});
        CHECK(Range::from_size(0, 0).empty());
        CHECK(Range{5, 5}.empty());

        const std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
        const Range saturated = Range::from_size(maximum - 2, 10);
        CHECK(saturated.start == maximum - 2);
        CHECK(saturated.end == maximum);
        CHECK(saturated.size() == 2);
        CHECK(Range::from_size(0, maximum).end == maximum);

        CHECK(Range{0, 100}.clamp(10) == Range{0, 10});
        CHECK(Range{5, 20}.clamp(10) == Range{5, 10});
        CHECK(Range{20, 30}.clamp(10) == Range{10, 10});
        CHECK(Range{0, 100}.clamp(0).empty());
        CHECK(Range{7, 3}.clamp(100).empty());

        CHECK(Range{0, 10}.intersect(Range{5, 15}) == Range{5, 10});
        CHECK(Range{0, 5}.intersect(Range{5, 10}).empty());
        CHECK(Range{0, 20}.intersect(Range{5, 10}) == Range{5, 10});
        CHECK(Range{2, 4}.intersect(Range{6, 8}).empty());
        CHECK(Range{0, 5}.intersect(Range{10, 20}).empty());

        return 0;
    }

    int test_open_move_close(const std::filesystem::path& root)
    {
        const std::filesystem::path missing = root / "missing.bin";

        Error error = Error::invalid_range;
        std::optional<File> file = open_file(missing, OpenMode::read, &error);
        CHECK(!file.has_value());
        CHECK(error == Error::not_found);

        error = Error::none;
        file = File::open("", OpenMode::read, &error);
        CHECK(!file.has_value());
        CHECK(error == Error::not_found);

        // 目录不能当作文件打开（Windows 与 POSIX 都返回 permission_denied）。
        error = Error::none;
        CHECK(!open_file(root, OpenMode::read, &error).has_value());
        CHECK(error == Error::permission_denied);

        const std::filesystem::path target = root / "create.bin";
        error = Error::invalid_range;
        file = open_file(target, OpenMode::write, &error);
        CHECK(file.has_value());
        CHECK(error == Error::none);
        CHECK(file->is_open());
        CHECK(file->size() == 0);

        // 移动构造：源对象失效，目标对象持有句柄。
        File moved(std::move(*file));
        CHECK(moved.is_open());
        CHECK(!file->is_open());
        CHECK(file->size() == 0);
        CHECK(file->read(Range{0, 1}, std::span<char>()) == Error::io_error);
        file.reset();

        std::size_t written = 0;
        CHECK(moved.write(Range{0, 7}, "persist", &written) == Error::none);
        CHECK(written == 7);
        CHECK(moved.size() == 7);

        Line line;
        moved.close();
        CHECK(!moved.is_open());
        moved.close();
        CHECK(moved.size() == 0);
        CHECK(moved.flush() == Error::io_error);
        CHECK(moved.sync() == Error::io_error);
        CHECK(moved.read_range(Range{0, 1}).empty());
        CHECK(moved.write(Range{0, 1}, "x") == Error::io_error);
        CHECK(moved.append("x") == Error::io_error);
        CHECK(moved.read_line(0, &line) == Error::io_error);

        // 关闭后内容仍然保留。
        CHECK(read_text(target) == std::optional<std::string>("persist"));

        error = Error::none;
        std::optional<File> reopened = open_file(target, OpenMode::read, &error);
        CHECK(reopened.has_value());
        CHECK(reopened->size() == 7);

        // 移动赋值：目标原有的句柄被关闭，源对象失效。
        error = Error::none;
        std::optional<File> other = open_file(target, OpenMode::read, &error);
        CHECK(other.has_value());

        File assigned;
        CHECK(!assigned.is_open());
        assigned = std::move(*reopened);
        CHECK(assigned.is_open());
        CHECK(!reopened->is_open());
        CHECK(assigned.size() == 7);

        assigned = std::move(*other);
        CHECK(assigned.is_open());
        CHECK(!other->is_open());
        CHECK(assigned.size() == 7);

        // 自我移动赋值保持有效。
        File& alias = assigned;
        assigned = std::move(alias);
        CHECK(assigned.is_open());
        CHECK(assigned.size() == 7);

        // 同一文件可以被多个 File 同时打开。
        Error left_error = Error::none;
        std::optional<File> left = open_file(target, OpenMode::read, &left_error);
        CHECK(left.has_value());
        CHECK(left_error == Error::none);

        Error right_error = Error::none;
        std::optional<File> right = open_file(target, OpenMode::read, &right_error);
        CHECK(right.has_value());
        CHECK(right_error == Error::none);
        CHECK(left->size() == right->size());

        return 0;
    }

    int test_range_io(const std::filesystem::path& root)
    {
        const std::filesystem::path path = root / "range_io.bin";
        CHECK(write_text(path, "hello world"));

        Error error = Error::invalid_range;
        std::optional<File> file = open_file(path, OpenMode::read, &error);
        CHECK(file.has_value());
        CHECK(error == Error::none);
        CHECK(file->size() == 11);

        std::vector<char> buffer(11, '\0');
        std::size_t got = 99;
        CHECK(file->read(Range{0, 5}, buffer, &got) == Error::none);
        CHECK(got == 5);
        CHECK(text_of(buffer).substr(0, got) == "hello");

        CHECK(file->read(Range{6, 11}, buffer, &got) == Error::none);
        CHECK(got == 5);
        CHECK(text_of(buffer).substr(0, got) == "world");

        // 缓冲区小于区间：只填满缓冲区，不报错。
        std::vector<char> small(3, '\0');
        CHECK(file->read(Range{0, 11}, small, &got) == Error::none);
        CHECK(got == 3);
        CHECK(text_of(small) == "hel");

        // 空缓冲区与空区间都是空操作。
        CHECK(file->read(Range{0, 11}, std::span<char>(), &got) == Error::none);
        CHECK(got == 0);
        CHECK(file->read(Range{3, 3}, buffer, &got) == Error::none);
        CHECK(got == 0);

        // 非法区间。
        CHECK(file->read(Range{5, 2}, buffer, &got) == Error::invalid_range);
        CHECK(got == 0);

        // 读越界：起点在 EOF 或之后返回 eof，且不写缓冲区。
        CHECK(file->read(Range{11, 20}, buffer, &got) == Error::eof);
        CHECK(got == 0);
        CHECK(file->read(Range{20, 30}, buffer, &got) == Error::eof);
        CHECK(got == 0);

        // 末端越过 EOF：按 clamp 只读到末尾。
        CHECK(file->read(Range{6, 100}, buffer, &got) == Error::none);
        CHECK(got == 5);
        CHECK(text_of(buffer).substr(0, got) == "world");

        CHECK(file->read_range(Range{0, 5}) == bytes("hello"));
        CHECK(file->read_range(Range{6, 100}) == bytes("world"));

        error = Error::none;
        CHECK(file->read_range(Range{11, 20}, &error).empty());
        CHECK(error == Error::eof);
        CHECK(file->read_range(Range{20, 30}, &error).empty());
        CHECK(error == Error::eof);

        error = Error::none;
        CHECK(file->read_range(Range{5, 2}, &error).empty());
        CHECK(error == Error::invalid_range);

        error = Error::invalid_range;
        CHECK(file->read_range(Range{3, 3}, &error).empty());
        CHECK(error == Error::none);

        // 只读模式拒绝写入。
        CHECK(file->write(Range{0, 1}, "H") == Error::permission_denied);
        CHECK(file->append("x") == Error::permission_denied);

        // 关闭后所有 I/O 都返回 io_error。
        file->close();
        CHECK(file->read(Range{0, 1}, buffer, &got) == Error::io_error);
        error = Error::none;
        CHECK(file->read_range(Range{0, 1}, &error).empty());
        CHECK(error == Error::io_error);

        return 0;
    }

    int test_write_extend(const std::filesystem::path& root)
    {
        const std::filesystem::path path = root / "write.bin";
        Error error = Error::none;
        std::optional<File> file = open_file(path, OpenMode::write, &error);
        CHECK(file.has_value());
        CHECK(file->size() == 0);

        std::size_t written = 0;
        CHECK(file->write(Range{0, 5}, "hello", &written) == Error::none);
        CHECK(written == 5);
        CHECK(file->size() == 5);

        // 数据长于区间：只写区间长度。
        CHECK(file->write(Range{0, 2}, "XYZ", &written) == Error::none);
        CHECK(written == 2);
        CHECK(file->read_range(Range{0, 5}) == bytes("XYllo"));

        // 空数据与空区间是空操作。
        CHECK(file->write(Range{0, 5}, "", &written) == Error::none);
        CHECK(written == 0);
        CHECK(file->write(Range{2, 2}, "abc", &written) == Error::none);
        CHECK(written == 0);
        CHECK(file->size() == 5);

        // 非法区间。
        CHECK(file->write(Range{4, 2}, "x", &written) == Error::invalid_range);
        CHECK(written == 0);
        CHECK(file->size() == 5);

        // 越界写扩展文件，空洞读出为零。
        CHECK(file->write(Range{8, 11}, "end", &written) == Error::none);
        CHECK(written == 3);
        CHECK(file->size() == 11);
        CHECK(file->read_range(Range{0, 11}) == bytes(std::string("XYllo\0\0\0end", 11)));

        // 写模式句柄同时可读。
        std::vector<char> prefix(2, '\0');
        std::size_t got = 0;
        CHECK(file->read(Range{0, 2}, prefix, &got) == Error::none);
        CHECK(got == 2);
        CHECK(text_of(prefix) == "XY");

        // 超出平台偏移上限：不触碰文件，直接 too_large。
        const std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
        CHECK(file->write(Range{maximum - 1, maximum}, "x", &written) == Error::too_large);
        CHECK(written == 0);
        CHECK(file->size() == 11);

        // 大块写入：内部循环处理短写，每个字节都必须落盘并可读回。
        const std::size_t length = (3u * 1024u * 1024u) + 17u;
        std::vector<char> large(length);
        for (std::size_t index = 0; index < length; ++index)
        {
            large[index] = static_cast<char>(index % 251u);
        }

        error = Error::none;
        std::optional<File> big = open_file(root / "large.bin", OpenMode::write, &error);
        CHECK(big.has_value());
        CHECK(big->write(Range::from_size(0, length), std::string_view(large.data(), large.size()), &written)
            == Error::none);
        CHECK(written == length);
        CHECK(big->size() == length);
        CHECK(big->read_range(Range::from_size(0, length)) == large);

        return 0;
    }

    int test_append_modes(const std::filesystem::path& root)
    {
        const std::filesystem::path path = root / "append.bin";
        CHECK(write_text(path, "abc"));

        Error error = Error::none;
        std::optional<File> file = open_file(path, OpenMode::append, &error);
        CHECK(file.has_value());
        CHECK(error == Error::none);
        CHECK(file->size() == 3);

        CHECK(file->append("def") == Error::none);
        CHECK(file->size() == 6);
        CHECK(file->append("") == Error::none);
        CHECK(file->size() == 6);

        // append 模式保留内容，同时允许按偏移读写。
        CHECK(file->write(Range{0, 1}, "Z") == Error::none);
        CHECK(file->read_range(Range{0, 6}) == bytes("Zbcdef"));

        CHECK(file->append("!") == Error::none);
        CHECK(file->read_range(Range{0, 7}) == bytes("Zbcdef!"));
        file.reset();

        // write 模式会截断。
        error = Error::none;
        std::optional<File> truncated = open_file(path, OpenMode::write, &error);
        CHECK(truncated.has_value());
        CHECK(truncated->size() == 0);
        truncated.reset();

        // 只读模式拒绝写入。
        error = Error::none;
        std::optional<File> reader = open_file(path, OpenMode::read, &error);
        CHECK(reader.has_value());
        std::size_t written = 1;
        CHECK(reader->write(Range{0, 1}, "x", &written) == Error::permission_denied);
        CHECK(written == 0);
        CHECK(reader->append("x") == Error::permission_denied);

        return 0;
    }

    int test_lines(const std::filesystem::path& root)
    {
        const std::filesystem::path path = root / "lines.txt";
        CHECK(write_text(path, "alpha\nbeta\r\ngamma\r\ndelta"));

        Error error = Error::none;
        std::optional<File> file = open_file(path, OpenMode::read, &error);
        CHECK(file.has_value());
        CHECK(file->size() == 24);

        Line line;
        std::uint64_t next = 0;

        CHECK(file->read_line(0, &line, &next) == Error::none);
        CHECK(line.number == 1);
        CHECK(line.range == Range{0, 5});
        CHECK(line.text == "alpha");
        CHECK(next == 6);

        CHECK(file->read_line(6, &line, &next) == Error::none);
        CHECK(line.number == 2);
        CHECK(line.range == Range{6, 10});
        CHECK(line.text == "beta");
        CHECK(next == 12);

        CHECK(file->read_line(12, &line, &next) == Error::none);
        CHECK(line.number == 3);
        CHECK(line.range == Range{12, 17});
        CHECK(line.text == "gamma");
        CHECK(next == 19);

        // 末行没有换行符。
        CHECK(file->read_line(19, &line, &next) == Error::none);
        CHECK(line.number == 4);
        CHECK(line.range == Range{19, 24});
        CHECK(line.text == "delta");
        CHECK(next == 24);

        CHECK(file->read_line(24, &line, &next) == Error::eof);
        CHECK(file->read_line(25, &line, &next) == Error::invalid_range);
        CHECK(file->read_line(0, nullptr) == Error::invalid_range);

        // 失败时不改动输出参数。
        line.number = 99;
        line.text = "keep";
        CHECK(file->read_line(24, &line) == Error::eof);
        CHECK(line.number == 99);
        CHECK(line.text == "keep");

        // 随机访问：直接读最后一行也能得到正确行号（内部扫描前缀）。
        error = Error::none;
        std::optional<File> random = open_file(path, OpenMode::read, &error);
        CHECK(random.has_value());
        CHECK(random->read_line(19, &line, &next) == Error::none);
        CHECK(line.number == 4);
        CHECK(line.text == "delta");

        // next_offset 可以为空指针。
        CHECK(file->read_line(0, &line) == Error::none);
        CHECK(line.text == "alpha");

        // 空行也算一行。
        const std::filesystem::path empty_path = root / "empty_lines.txt";
        CHECK(write_text(empty_path, "\n\nx\n"));
        error = Error::none;
        std::optional<File> empty_lines = open_file(empty_path, OpenMode::read, &error);
        CHECK(empty_lines.has_value());

        CHECK(empty_lines->read_line(0, &line, &next) == Error::none);
        CHECK(line.number == 1);
        CHECK(line.range == Range{0, 0});
        CHECK(line.text.empty());
        CHECK(next == 1);

        CHECK(empty_lines->read_line(1, &line, &next) == Error::none);
        CHECK(line.number == 2);
        CHECK(line.range.empty());
        CHECK(next == 2);

        CHECK(empty_lines->read_line(2, &line, &next) == Error::none);
        CHECK(line.number == 3);
        CHECK(line.text == "x");
        CHECK(next == 4);

        CHECK(empty_lines->read_line(4, &line, &next) == Error::eof);

        // 单独的 '\r' 不是换行，且不被去掉。
        const std::filesystem::path cr_path = root / "lone_cr.txt";
        CHECK(write_text(cr_path, "a\rb\n"));
        error = Error::none;
        std::optional<File> cr_file = open_file(cr_path, OpenMode::read, &error);
        CHECK(cr_file.has_value());
        CHECK(cr_file->read_line(0, &line, &next) == Error::none);
        CHECK(line.text == "a\rb");
        CHECK(line.range == Range{0, 3});
        CHECK(next == 4);

        // 以 CRLF 结尾的末行。
        const std::filesystem::path crlf_path = root / "crlf_eof.txt";
        CHECK(write_text(crlf_path, "x\r\n"));
        error = Error::none;
        std::optional<File> crlf_file = open_file(crlf_path, OpenMode::read, &error);
        CHECK(crlf_file.has_value());
        CHECK(crlf_file->read_line(0, &line, &next) == Error::none);
        CHECK(line.text == "x");
        CHECK(line.range == Range{0, 1});
        CHECK(next == 3);

        // 长行跨过内部 64 KiB 读取块。
        const std::filesystem::path long_path = root / "long_line.txt";
        std::string long_text(70000, 'a');
        long_text += '\n';
        long_text += "tail";
        CHECK(write_text(long_path, long_text));

        error = Error::none;
        std::optional<File> long_file = open_file(long_path, OpenMode::read, &error);
        CHECK(long_file.has_value());
        CHECK(long_file->read_line(0, &line, &next) == Error::none);
        CHECK(line.number == 1);
        CHECK(line.text.size() == 70000);
        CHECK(line.text == std::string(70000, 'a'));
        CHECK(line.range == Range{0, 70000});
        CHECK(next == 70001);

        CHECK(long_file->read_line(next, &line, &next) == Error::none);
        CHECK(line.number == 2);
        CHECK(line.text == "tail");
        CHECK(line.range == Range{70001, 70005});
        CHECK(next == 70005);

        // 写入改变换行数量后，行号缓存必须失效。
        const std::filesystem::path cache_path = root / "line_cache.txt";
        CHECK(write_text(cache_path, "a\nb\nc\n"));
        error = Error::none;
        std::optional<File> cache_file = open_file(cache_path, OpenMode::append, &error);
        CHECK(cache_file.has_value());

        CHECK(cache_file->read_line(0, &line, &next) == Error::none);
        CHECK(line.number == 1);
        CHECK(next == 2);
        CHECK(cache_file->read_line(2, &line, &next) == Error::none);
        CHECK(line.number == 2);
        CHECK(next == 4);

        // 把第一个换行改成 'x' 后，"c" 从第 3 行变成第 2 行。
        CHECK(cache_file->write(Range{1, 2}, "x") == Error::none);
        CHECK(cache_file->read_line(4, &line, &next) == Error::none);
        CHECK(line.number == 2);
        CHECK(line.text == "c");
        CHECK(line.range == Range{4, 5});
        CHECK(next == 6);

        return 0;
    }

    int test_flush_sync(const std::filesystem::path& root)
    {
        const std::filesystem::path path = root / "flush.bin";
        Error error = Error::none;
        std::optional<File> writer = open_file(path, OpenMode::write, &error);
        CHECK(writer.has_value());
        CHECK(writer->write(Range{0, 5}, "flush") == Error::none);
        CHECK(writer->flush() == Error::none);
        CHECK(writer->sync() == Error::none);

        // 没有用户态缓冲：另一个句柄也能立即看到数据。
        error = Error::none;
        std::optional<File> reader = open_file(path, OpenMode::read, &error);
        CHECK(reader.has_value());
        CHECK(reader->read_range(Range{0, 5}) == bytes("flush"));
        CHECK(reader->flush() == Error::none);
        CHECK(reader->sync() == Error::none);

        reader.reset();
        writer.reset();

        File closed;
        CHECK(closed.flush() == Error::io_error);
        CHECK(closed.sync() == Error::io_error);

        return 0;
    }

    int test_random_access(const std::filesystem::path& root)
    {
        const std::filesystem::path path = root / "random.bin";
        Error error = Error::none;
        std::optional<File> file = open_file(path, OpenMode::write, &error);
        CHECK(file.has_value());

        constexpr std::size_t kSize = 8192;
        std::vector<char> model(kSize, '\0');
        for (std::size_t index = 0; index < kSize; ++index)
        {
            model[index] = static_cast<char>(index % 256u);
        }

        CHECK(file->write(Range::from_size(0, kSize), std::string_view(model.data(), model.size()))
            == Error::none);

        std::mt19937 generator(20241109u);
        std::uniform_int_distribution<std::size_t> offset_distribution(0, kSize - 1);
        std::uniform_int_distribution<std::size_t> length_distribution(1, 300);

        for (int iteration = 0; iteration < 400; ++iteration)
        {
            const std::size_t offset = offset_distribution(generator);
            const std::size_t available = kSize - offset;
            const std::size_t requested = length_distribution(generator);
            const std::size_t length = requested < available ? requested : available;

            std::string data(length, '\0');
            for (std::size_t index = 0; index < length; ++index)
            {
                data[index] = static_cast<char>((iteration * 37 + static_cast<int>(index)) % 256);
            }

            std::size_t written = 0;
            CHECK(file->write(Range::from_size(offset, length), data, &written) == Error::none);
            CHECK(written == length);
            std::copy(data.begin(), data.end(), model.begin() + static_cast<std::ptrdiff_t>(offset));

            const std::size_t read_offset = offset_distribution(generator);
            const std::size_t read_available = kSize - read_offset;
            const std::size_t read_requested = length_distribution(generator);
            const std::size_t read_length = read_requested < read_available ? read_requested : read_available;

            const std::vector<char> actual = file->read_range(Range::from_size(read_offset, read_length));
            const std::vector<char> expected(model.begin() + static_cast<std::ptrdiff_t>(read_offset),
                model.begin() + static_cast<std::ptrdiff_t>(read_offset + read_length));
            CHECK(actual == expected);
        }

        CHECK(file->size() == kSize);
        CHECK(file->read_range(Range{0, kSize}) == model);

        // 越过 EOF 的读取按 clamp 截断。
        CHECK(file->read_range(Range::from_size(kSize - 10, 100))
            == std::vector<char>(model.begin() + static_cast<std::ptrdiff_t>(kSize - 10), model.end()));
        CHECK(file->read_range(Range::from_size(kSize, 10)).empty());

        return 0;
    }

    int test_locking(const std::filesystem::path& root)
    {
        const std::filesystem::path path = root / "lock.bin";
        CHECK(write_text(path, "lock"));

        Error error = Error::none;
        std::optional<File> first = open_file(path, OpenMode::read, &error);
        CHECK(first.has_value());
        error = Error::none;
        std::optional<File> second = open_file(path, OpenMode::read, &error);
        CHECK(second.has_value());

        // 独占锁互斥；默认参数是独占。
        CHECK(first->lock() == Error::none);
        CHECK(second->lock(true) == Error::locked);
        CHECK(second->lock(false) == Error::locked);
        CHECK(first->unlock() == Error::none);

        // 共享锁可以共存。
        CHECK(first->lock(false) == Error::none);
        CHECK(second->lock(false) == Error::none);

        // 持有共享锁时升级为独占会被拒绝。
        CHECK(second->lock(true) == Error::locked);
        CHECK(second->unlock() == Error::none);
        CHECK(second->unlock() == Error::none);
        CHECK(first->unlock() == Error::none);

        // 独占锁可再次获取；关闭持有者后锁自动释放。
        CHECK(first->lock(true) == Error::none);
        CHECK(second->lock(true) == Error::locked);
        first->close();
        CHECK(second->lock(true) == Error::none);
        CHECK(second->unlock() == Error::none);

        // 关闭后加锁/解锁报错。
        CHECK(first->lock(true) == Error::io_error);
        CHECK(first->unlock() == Error::io_error);

        return 0;
    }

    int test_large_offsets(const std::filesystem::path& root)
    {
        // 64 位区间算术不依赖文件。
        const std::uint64_t huge = (5ull << 30) + 12345ull;
        CHECK(Range::from_size(huge, 8).start == huge);
        CHECK(Range::from_size(huge, 8).end == huge + 8);
        CHECK(Range::from_size(0, huge).size() == huge);

        const std::filesystem::path path = root / "large_file.bin";
        if (!create_sparse_file(path, huge))
        {
            // 文件系统不支持稀疏文件时无法低成本地构造 >2 GiB 的测试数据。
            std::cout << "SKIP: 当前文件系统不支持稀疏文件，跳过大偏移读写" << std::endl;
            return 0;
        }

        Error error = Error::none;
        std::optional<File> file = open_file(path, OpenMode::read, &error);
        CHECK(file.has_value());
        CHECK(error == Error::none);
        CHECK(file->size() == huge);
        CHECK(file->size() > 0x1'0000'0000ull);

        // 稀疏区域读出零字节。
        std::vector<char> tail(8, '\1');
        std::size_t got = 0;
        CHECK(file->read(Range{huge - 8, huge}, tail, &got) == Error::none);
        CHECK(got == 8);
        CHECK(tail == bytes(std::string(8, '\0')));

        // 跨越 EOF：按 clamp 只读到末尾。
        std::vector<char> crossing(8, '\1');
        CHECK(file->read(Range{huge - 4, huge + 4}, crossing, &got) == Error::none);
        CHECK(got == 4);
        CHECK(std::all_of(crossing.begin(), crossing.begin() + 4,
            [](char value) { return value == 0; }));

        // 2 GiB 之后（32 位有符号边界之外）读取。
        const std::uint64_t middle = 2ull << 30;
        std::vector<char> window(16, '\1');
        CHECK(file->read(Range{middle, middle + 16}, window, &got) == Error::none);
        CHECK(got == 16);

        CHECK(file->read(Range{huge, huge + 8}, window, &got) == Error::eof);
        CHECK(got == 0);
        CHECK(file->read_range(Range{huge - 4, huge}).size() == 4);
        CHECK(file->read_range(Range{huge + 1, huge + 2}).empty());

        file.reset();

        // 主动删除大文件；临时目录析构时还会再清理一次。
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        return 0;
    }
}

int main()
{
    const TempDirectory directory;
    CHECK(std::filesystem::is_directory(directory.path));

    CHECK(test_range() == 0);
    CHECK(test_open_move_close(directory.path) == 0);
    CHECK(test_range_io(directory.path) == 0);
    CHECK(test_write_extend(directory.path) == 0);
    CHECK(test_append_modes(directory.path) == 0);
    CHECK(test_lines(directory.path) == 0);
    CHECK(test_flush_sync(directory.path) == 0);
    CHECK(test_random_access(directory.path) == 0);
    CHECK(test_locking(directory.path) == 0);
    CHECK(test_large_offsets(directory.path) == 0);

    std::cout << "PASS: file" << std::endl;
    return 0;
}
