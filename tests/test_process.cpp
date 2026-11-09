#include "stdlib/process/Process.h"

#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace
{
    int g_failures = 0;

    void check(bool condition, const char* expression, int line)
    {
        if (!condition)
        {
            ++g_failures;
            std::cout << "FAIL: line " << line << ": " << expression << '\n';
        }
    }

    // 去掉行尾换行与空格，便于比较不同平台的输出差异（Windows 为 \r\n）。
    std::string trim_end(const std::string& text)
    {
        std::string result = text;
        while (!result.empty() && (result.back() == '\r' || result.back() == '\n' || result.back() == ' '))
        {
            result.pop_back();
        }
        return result;
    }

    // 构造一段交给系统 shell 执行的脚本；这里显式调用 cmd/sh 只是为了构造测试场景，
    // 库本身仍然按参数数组启动程序，不经过 shell。
    coordination::process::Command shell_command(const std::string& script)
    {
        coordination::process::Command command;
#if defined(_WIN32)
        command.program = "cmd.exe";
        command.arguments = {"/c", script};
#else
        command.program = "/bin/sh";
        command.arguments = {"-c", script};
#endif
        return command;
    }

    // 构造一个持续数秒的命令：Windows 用 ping，POSIX 用 sleep。
    coordination::process::Command long_running_command()
    {
        coordination::process::Command command;
#if defined(_WIN32)
        command.program = "ping.exe";
        command.arguments = {"-n", "10", "127.0.0.1"};
#else
        command.program = "/bin/sleep";
        command.arguments = {"5"};
#endif
        return command;
    }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

int main()
{
    using namespace coordination::process;

    // ---------------------------------------------------------------
    // 1. 捕获 stdout
    // ---------------------------------------------------------------
    {
        const ProcessResult result = run(shell_command("echo hello"));
        CHECK(result.ok());
        CHECK(result.exited);
        CHECK(!result.timed_out);
        CHECK(!result.signaled);
        CHECK(result.exit_code == 0);
        CHECK(result.error.empty());
        CHECK(result.stderr_data.empty());
        CHECK(trim_end(result.stdout_data) == "hello");
    }

    // ---------------------------------------------------------------
    // 2. 捕获 stderr
    // ---------------------------------------------------------------
    {
        const ProcessResult result = run(shell_command("echo oops 1>&2"));
        CHECK(result.ok());
        CHECK(result.exited);
        CHECK(result.error.empty());
        CHECK(result.stdout_data.empty());
        CHECK(trim_end(result.stderr_data) == "oops");
    }

    // ---------------------------------------------------------------
    // 3. 非 0 退出码
    // ---------------------------------------------------------------
    {
        const ProcessResult result = run(shell_command("exit 3"));
        CHECK(result.exited);
        CHECK(!result.timed_out);
        CHECK(result.exit_code == 3);
        CHECK(!result.ok());
        CHECK(result.error.empty());
    }

    // ---------------------------------------------------------------
    // 4. 程序不存在：error 非空且不抛异常
    // ---------------------------------------------------------------
    {
        Command command;
        command.program = "coordination_no_such_program_7f3a";
        const ProcessResult result = run(command);
        CHECK(!result.error.empty());
        CHECK(!result.ok());
        CHECK(!result.exited);
        CHECK(result.exit_code == -1);
    }

    // ---------------------------------------------------------------
    // 5. stdin_data 回显
    // ---------------------------------------------------------------
    {
        Command command;
#if defined(_WIN32)
        command.program = "sort.exe";  // 读取 stdin 并按行输出
#else
        command.program = "cat";  // 读取 stdin 并回显
#endif
        command.stdin_data = "hello stdin\n";
        const ProcessResult result = run(command);
        CHECK(result.ok());
        CHECK(result.exited);
        CHECK(trim_end(result.stdout_data) == "hello stdin");
    }

    // ---------------------------------------------------------------
    // 6. 超时：200ms 后强制终止
    // ---------------------------------------------------------------
    {
        Command command = long_running_command();
        command.timeout = std::chrono::milliseconds(200);

        const auto start = std::chrono::steady_clock::now();
        const ProcessResult result = run(command);
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

        CHECK(result.timed_out);
        CHECK(!result.exited);
        CHECK(!result.ok());
        CHECK(elapsed >= std::chrono::milliseconds(100));
        CHECK(elapsed < std::chrono::seconds(4));
    }

    // ---------------------------------------------------------------
    // 7. 环境变量覆盖
    // ---------------------------------------------------------------
    {
        Command command;
#if defined(_WIN32)
        command = shell_command("echo %COORDINATION_PROCESS_TEST%");
#else
        command = shell_command("echo $COORDINATION_PROCESS_TEST");
#endif
        command.environment.emplace_back("COORDINATION_PROCESS_TEST", "value42");
        const ProcessResult result = run(command);
        CHECK(result.ok());
        CHECK(trim_end(result.stdout_data) == "value42");
    }

    // ---------------------------------------------------------------
    // 8. 工作目录不存在：启动失败
    // ---------------------------------------------------------------
    {
        Command command = shell_command("exit 0");
        command.working_directory = "coordination_no_such_directory_9c1f";
        const ProcessResult result = run(command);
        CHECK(!result.error.empty());
        CHECK(!result.ok());
        CHECK(!result.exited);
    }

    // ---------------------------------------------------------------
    // 9. Child：spawn / running / kill / wait
    // ---------------------------------------------------------------
    {
        Command command = long_running_command();
        std::string error = "sentinel";
        std::optional<Child> child = Child::spawn(command, &error);
        CHECK(error.empty());
        CHECK(child.has_value());
        if (child.has_value())
        {
            CHECK(child->running());
            CHECK(child->kill());
            const int exit_code = child->wait();
            CHECK(exit_code != 0);
            CHECK(!child->running());
        }
    }

    // ---------------------------------------------------------------
    // 10. Child：自然结束时的退出码
    // ---------------------------------------------------------------
    {
        std::string error;
        std::optional<Child> child = Child::spawn(shell_command("exit 4"), &error);
        CHECK(error.empty());
        CHECK(child.has_value());
        if (child.has_value())
        {
            CHECK(child->wait() == 4);
            CHECK(!child->running());
        }
    }

    // ---------------------------------------------------------------
    // 11. Child：启动失败
    // ---------------------------------------------------------------
    {
        Command command;
        command.program = "coordination_no_such_program_7f3a";
        std::string error;
        std::optional<Child> child = Child::spawn(command, &error);
        CHECK(!child.has_value());
        CHECK(!error.empty());
    }

    // ---------------------------------------------------------------
    // 12. 大块 stdin + 不读 stdin 的子进程：超时路径不能死锁
    // ---------------------------------------------------------------
    {
        Command command = long_running_command();
        command.stdin_data.assign(256 * 1024, 'x');  // 远超管道缓冲区，写线程会阻塞
        command.timeout = std::chrono::milliseconds(200);
        const ProcessResult result = run(command);
        CHECK(result.timed_out);
        CHECK(!result.ok());
        CHECK(!result.exited);
    }

    // ---------------------------------------------------------------
    // 13. Child + stdin_data：写线程在 wait 时应被回收
    // ---------------------------------------------------------------
    {
        Command command;
#if defined(_WIN32)
        command.program = "sort.exe";
#else
        command.program = "cat";
#endif
        command.stdin_data = "child stdin\n";
        std::string error;
        std::optional<Child> child = Child::spawn(command, &error);
        CHECK(error.empty());
        CHECK(child.has_value());
        if (child.has_value())
        {
            CHECK(child->wait() == 0);
            CHECK(!child->running());
        }
    }

    // ---------------------------------------------------------------
    // 14. 并发 run：各子进程只继承自己的管道句柄，互不干扰
    // ---------------------------------------------------------------
    {
        constexpr int kWorkers = 8;
        std::vector<std::string> outputs(kWorkers);
        std::vector<std::thread> workers;
        workers.reserve(kWorkers);
        for (int index = 0; index < kWorkers; ++index)
        {
            workers.emplace_back([index, &outputs]()
                                 {
                                     const ProcessResult result = run(shell_command("echo concurrent"));
                                     if (result.ok())
                                     {
                                         outputs[static_cast<std::size_t>(index)] = trim_end(result.stdout_data);
                                     }
                                 });
        }
        for (std::thread& worker : workers)
        {
            worker.join();
        }
        for (const std::string& output : outputs)
        {
            CHECK(output == "concurrent");
        }
    }

    // ---------------------------------------------------------------
    // 汇总
    // ---------------------------------------------------------------
    if (g_failures == 0)
    {
        std::cout << "PASS: process\n";
        return 0;
    }

    std::cout << "FAIL: process (" << g_failures << " failure(s))\n";
    return 1;
}
