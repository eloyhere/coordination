# coordination 标准库基础模块调研：文件、编码、进程、线程

> 结论先行：这四个模块的难点不在"调哪个系统调用"，而在**把不可移植的语义收敛成一条可测试的契约**。coordination 应以"偏移读写 + 库内短读循环 + 错误分类"作为 I/O 唯一原语，编码默认 UTF-8 严格、替换与丢弃必须显式，进程创建走 `posix_spawn`/`CreateProcessW` 双实现并统一 `ExitStatus`，线程以 `stop_token` 式协作取消为协议、线程池内置。C++ 标准库只提供容器与同步基元，文件锁、定位读写、mmap、进程、线程池一律缺失，这一层必须自建，而不是包装 `std::filesystem`。

## 一、分层：薄适配层，厚语义层

分三层：L0 OS 原语（fd/HANDLE、`pread`/`ReadFile`、`flock`/`LockFileEx`）；L1 语义适配（把 `EINTR`、`ERROR_IO_PENDING`、短读、部分写归一为 `Result<T, IoError>`，错误分类覆盖中断、阻塞、EOF、权限、不存在）；L2 公共 API（`File`、`Process`、`ThreadPool`）。平台分支只允许出现在 L1，L2 不得出现 `#ifdef`。每个 API 都要写明：能否部分成功、是否可重试、失败后文件状态是否未定义。

## 二、文件：偏移读写是唯一原语

**Range/偏移读写。** 读接口应为 `read_at(offset: u64, buf: &mut [u8]) -> Result<usize>`，返回实际字节数而非成功/失败。POSIX 对应 `pread`/`pwrite`：从 `offset` 读写且**不改变文件偏移**，因此多线程可共享同一 fd 互不踩踏 [1]。Windows 无同名函数，但 `ReadFile`/`WriteFile` 传入 `OVERLAPPED` 的 `Offset`/`OffsetHigh` 即为定位读写，同样不动文件指针。顺序读写应实现为"自带偏移的 `read_at`"，**不要暴露 `seek`**：`seek+read` 在并发下必然竞态，POSIX 直到 Linux 3.14 才保证共享 fd 的偏移更新原子 [2]。

**短读、短写与 EINTR。** 三者必须一起设计，否则每个调用点都会重写循环：

- 短读不是错误：`read` 返回小于请求的字节数合法（管道、终端、接近 EOF）[2]，`pread` 同理 [1]。
- 短写不是错误：`write` 可能只写一部分，调用者需继续写剩余部分 [3]。
- `EINTR` 只在**一个字节都没传**时出现；`write` 若已写入部分数据则返回已写字节数而非 `EINTR` [3]。
- 因此重试逻辑是：`EINTR` 原样重试；`0 < n < len` 推进偏移继续；`n == 0` 且 `len > 0` 视为 EOF 或对端关闭。
- 单次上限：Linux 上 `read`/`write` 一次最多传 `0x7ffff000` 字节 [2][3]，大缓冲必须切片。
- Windows 无 `EINTR`，但有 `ERROR_IO_PENDING`、`ERROR_OPERATION_ABORTED`、命名管道 `ERROR_MORE_DATA`，应映射到同一 `Interrupted`/`WouldBlock` 分类。

**文件锁。** `flock` 是**劝告锁**，只约束愿意遵守协议的程序；锁绑定"打开文件描述"，`fork`/`dup` 共享同一把锁，且模式转换不保证原子（先释放再申请，中间可能被抢）[4]。`LockFileEx` 是**字节范围锁**，语义更接近强制：独占锁拒绝其他进程对该区间的读写，共享锁连持有者自己写也被拒绝；`LOCKFILE_FAIL_IMMEDIATELY` 可立即失败，异步句柄未获锁返回 `ERROR_IO_PENDING`；进程退出时系统会释放锁，但耗时取决于系统资源，必须显式解锁 [5]。Java 的文档把结论说得最清楚：平台可能强制也可能劝告，**跨平台唯一安全的用法是当作劝告锁**；锁属于整个 JVM 而非线程，关闭 channel 可能释放该文件上所有锁 [6]。故 coordination 只承诺劝告锁 + 区间 + 超时，API 用 `try_lock(timeout)` 而非阻塞 `lock()`。

**原子替换与持久化。** POSIX 的 `rename` 在同目录内原子替换目录项，但**不保证数据落盘**：要 `fsync` 新文件，再 `fsync` 父目录，目录项才算持久 [7]。Windows 用 `ReplaceFileW`：保留被替换文件的创建时间、短名、DACL、加密/压缩属性与命名流，要求同卷，失败可能留下"两个文件都在"的状态（`ERROR_UNABLE_TO_MOVE_REPLACEMENT` 1176、`ERROR_UNABLE_TO_REMOVE_REPLACED` 1175）[8]；`MoveFileEx(REPLACE_EXISTING)` 更轻但不保留元数据。`FlushFileBuffers` 对应 `fsync`，但目录没有句柄可 flush，对卷调用需管理员权限 [9]。故 `atomic_write(path, bytes)` 应是标准库单一函数，内部固定"同目录临时文件 → 写 → `fsync` → `rename`/`ReplaceFile` → `fsync` 目录"，契约写明"掉电后只可能是旧版本或新版本，绝不是半个文件"。

**大文件。** 32 位 POSIX 需 `_FILE_OFFSET_BITS=64` 才能超过 2 GiB，`_TIME_BITS=64` 解决 2038 [10]；Windows 的 `LARGE_INTEGER` 天生 64 位。API 统一用 `u64` 偏移与长度，签名禁止 `long`/`int`。

**内存映射。** `mmap` 的 `offset` 必须页对齐；文件尾不足一页的部分补零，**对这些字节的修改不会写回文件**；访问超出文件末尾的页收到 `SIGBUS`；`MAP_SHARED` 的可见性要靠 `msync` 精确控制 [11]。Windows 要两步 `CreateFileMapping` + `MapViewOfFile`，`offset` 必须是**分配粒度**（通常 64 KiB）的倍数，视图与 `ReadFile`/`WriteFile` 的数据**不保证一致**，访问错误以 `EXCEPTION_IN_PAGE_ERROR` 抛出，必须用 SEH 包住 [12]。映射适合只读大文件随机访问，不适合写路径。

表 1：文件语义对照

| 语义 | POSIX | Windows | coordination 策略 |
|---|---|---|---|
| 定位读写 | `pread`/`pwrite`，不动偏移 | `OVERLAPPED` 偏移 | 唯一原语 `read_at`/`write_at` |
| 短读短写 | 合法，需循环 | 合法，需循环 | 库内循环，API 返回 `usize` |
| 中断 | `EINTR`，仅未传数据时 | 无 `EINTR`；`ERROR_IO_PENDING` | 统一 `Interrupted`，自动重试 |
| 锁 | `flock`/`fcntl`，劝告 | `LockFileEx`，区间、近强制 | 只承诺劝告 + 超时 |
| 原子替换 | `rename` + 目录 `fsync` | `ReplaceFileW` 保留元数据 | `atomic_write` 单一入口 |
| 持久化 | `fsync`/`fdatasync` | `FlushFileBuffers` | `sync()`/`sync_dir()`（Windows 空实现并文档说明） |
| 大文件 | `_FILE_OFFSET_BITS=64` | 原生 64 位 | 签名只用 `u64` |
| 映射 | 页对齐、`SIGBUS` | 分配粒度对齐、SEH | 只读映射，写映射标实验性 |

## 三、编码：UTF-8 是内部唯一表示

**BOM。** UTF-8 的 BOM 是 `EF BB BF`，UTF-16LE/BE 是 `FF FE`/`FE FF`，UTF-32 同样有 BOM [13]。BOM 只是**签名**，不表示 UTF-8 有字节序；编码已显式声明时（如 `UTF-16LE`）BOM **既无必要也不允许**。UTF-8 BOM 会破坏以 `#!` 开头的脚本和任何期望首字节为 ASCII 的协议 [13]。策略：读取时识别并剥离 BOM 并记录原文件带 BOM，默认写出**不带** BOM，仅显式选项才写。

**UTF-8 校验。** 用表驱动 DFA 一遍扫描，拒绝过长编码、代理区 `U+D800..DFFF`、`> U+10FFFF`。错误恢复按 Unicode 的规范说法：遇非法序列可以报错、可以丢弃该字节、也可以输出 `U+FFFD`，然后**从下一个字节继续**，绝不能把后续合法字节一起吞掉 [13]。因此 API 要分开：`validate` 与 `decode_lossy`（返回替换次数），不要用一个"宽容解码"糊过去。

**编码检测是启发式，不是判定。** ICU 的 `CharsetDetector` 用统计与合法字节模式匹配，官方明确说这是"imprecise operation"，需要至少几百字节且最好单一语言，输出按置信度排序的候选（`getConfidence()` 0–100，`detectAll()`）[14]；ICU 还提示短样本可用 Compact Encoding Detector 降低错误率。`uchardet` 是 Mozilla 通用检测器的移植，覆盖类似启发式集合 [15]。结论：检测只用于**导入遗留文件**，结果必须带置信度；低于阈值就报"无法确定编码"，由用户显式指定。

**转换。** `iconv` 是有状态流式接口：逐字符转换、更新描述符状态，错误分 `EILSEQ`（非法序列）、`EINVAL`（不完整序列）、`E2BIG`（输出不足），结束时必须用 `inbuf = NULL` 冲刷移位序列 [16]。ICU 的 `ucnv` 同理，但**不做自动检测**（只有 UTF-16/32 读 BOM 决定字节序）[17]。转换必须支持分块 + 保留状态，否则多字节字符跨缓冲区就会损坏。

**错误策略。** 这正是 ICU 回调已抽象好的四种：`STOP`（严格报错）、`SUBSTITUTE`（替换，默认）、`SKIP`（丢弃）、`ESCAPE`（转义成 `%UXXXX`/`\uXXXX` 便于调试）[17]。不要依赖 libc 默认值：glibc/libiconv 默认严格，`//TRANSLIT` 音译、`//IGNORE` 静默丢弃；musl 用 `'*'` 兜底、FreeBSD/NetBSD/Solaris 用 `'?'` 兜底，**同一段代码在不同 libc 上行为不同** [16]。coordination 应把策略做成显式参数 `ErrorPolicy::{Strict, Replace(char), Skip, Escape}`，默认 `Strict`，并让"替换了多少字符"成为返回值的一部分。

表 2：错误策略

| 策略 | 行为 | 适用 | 风险 |
|---|---|---|---|
| Strict | 首个错误即失败，带字节偏移 | 编译输入、配置、协议 | 一个坏字节毁掉整次读取 |
| Replace | 写 `U+FFFD` 或指定串 | 展示、日志、搜索索引 | 静默数据失真 |
| Skip | 丢弃非法单元 | 清洗、尽力而为导入 | 同上，且长度变化 |
| Escape | 转义为可见文本 | 诊断、调试输出 | 输出不可直接回写 |

## 四、进程：不要假装 fork 可移植

**创建。** Windows 只有 `CreateProcessW`：一次调用完成"创建 + 加载"，没有 fork，因此没有"子进程 exec 前"的钩子。两个坑必须写进封装：`lpApplicationName` 传 `NULL` 时系统按空格猜测可执行文件，`"C:\Program Files\MyApp"` 可能被解析成 `C:\Program.exe`，这是文档明示的提权风险 [18]；`bInheritHandles=TRUE` 会继承**所有**可继承句柄，多线程同时创建进程时应改用 `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` 精确指定 [18]。POSIX 侧 `fork` 在多线程程序里只允许调用异步信号安全函数，实践上应优先 `posix_spawn`：它把 fork+exec 合成一次调用，用 file actions 描述 `dup2`/`close`/`open`，glibc 2.24 起用 `clone(CLONE_VM|CLONE_VFORK)` 实现，且**不执行 `pthread_atfork` 处理器**；子进程在 pre-exec 阶段失败统一以 127 退出 [19]。127 与程序自己返回 127 无法区分，文档要写明。

**管道与死锁。** 经典死锁是父进程先 `wait` 再读管道：子进程写满管道缓冲后阻塞，父进程等它退出。Go 把这套规则写进了文档：`StdoutPipe` 返回的管道由 `Wait` 关闭，因此"在所有读取完成前调用 `Wait` 是错误的"，也不能配 `Cmd.Run` 使用；`WaitDelay` 用于兜底"子进程已退出但管道未关闭"，超时返回 `ErrWaitDelay` [20]。coordination 的 `Command` 应默认并发泵出 stdout/stderr（或提供 `output()` 一次性收集），禁止"先等后读"。Windows 侧还有句柄泄漏死锁：父进程不关自己那份写端，子进程等不到 EOF。

**超时与终止。** POSIX 先 `SIGTERM`，宽限期后 `SIGKILL`，且要杀整个进程组而非单个 pid，否则孙进程变孤儿。Windows 没有信号：`TerminateProcess` 立即杀死、不给清理机会 [18]；优雅终止靠 `GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT)`（需 `CREATE_NEW_PROCESS_GROUP`），最稳的是 Job Object + `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`，父进程崩溃时子进程一并回收。API 应是 `wait_timeout(d)` + `terminate()` + `kill()` 三档，而不是一个 `kill()`。

**环境变量。** POSIX 是 `char*[]` 的 `k=v`，允许重复键；Windows 环境块是排序的、键大小写不敏感、还有 `=C:` 这类隐藏的当前目录变量，传自定义环境块时要加 `CREATE_UNICODE_ENVIRONMENT`，否则按 ANSI 解释 [18]。环境块与句柄继承都要显式构造，不要"继承当前进程"了事：构建可复现性依赖这里。

**退出码与信号。** POSIX 的 `wait` 状态要用 `WIFEXITED`/`WEXITSTATUS`/`WIFSIGNALED`/`WTERMSIG` 拆开；Windows 只有一个 32 位 `DWORD` 退出码，`0xC0000005` 这类 NTSTATUS 会被当成普通退出码 [18]。统一类型建议 `ExitStatus = Exited(u8) | Signaled(sig: i32, core: bool) | Unknown(u32)`；**绝不能用 `128 + signal` 冒充退出码**，那会与真实退出码冲突。

表 3：进程语义对照

| 维度 | POSIX | Windows | 统一抽象 |
|---|---|---|---|
| 创建 | `posix_spawn`（优先）/`fork+exec` | `CreateProcessW` | `Command::spawn()` |
| 参数 | `argv` 数组，无引号问题 | 单个命令行字符串，需按 `CommandLineToArgvW` 规则转义 | 内部生成 argv/命令行 |
| 标准流 | `dup2` 到 0/1/2 | `STARTUPINFO` 句柄 + 继承 | `Stdio::{Inherit,Pipe,Null}` |
| 终止 | `SIGTERM` → `SIGKILL` | `TerminateProcess`；`CTRL_BREAK` 优雅 | `terminate()`/`kill()`/`wait_timeout` |
| 退出 | 退出码或信号 | 32 位退出码 | `ExitStatus` 枚举 |
| 环境 | `environ`，可重复键 | 排序、大小写不敏感、`=C:` | `EnvMap` + 显式构造 |

## 五、线程：`std::thread` 是基元，不是模型

**可移植性边界。** `std::thread` 只保证"能创建、能 join/detach"：线程函数抛出的异常无法捕获（逃逸即 `std::terminate`），不能取消，栈大小/优先级要 `native_handle()` 走平台 API，`hardware_concurrency()` 只是提示。构造失败抛 `std::system_error`，必须捕获并降级。C++20 才加入 `jthread`，而 GCC 直到 16 才把 C++20 设为默认 [21]，"用 `jthread`"本身就是一条工具链门槛。

**`jthread`/`stop_token` 的正确用法。** `stop_source::request_stop()` 一次性、不可撤回；`stop_token` 可轮询 `stop_requested()`，也可注册 `stop_callback`；标准明确规定 `stop_callback` 的析构会**阻塞等待正在其他线程执行的回调返回**，回调抛异常则 `std::terminate` [22]。因此回调只能做"置标志 + 唤醒"这类短操作。`jthread` 的价值是把"析构时 request_stop 再 join"变成默认行为，消除"忘记 join 导致 terminate"这一类 bug；但它不解决"任务不响应停止"，取消仍是协作式的。

**线程池设计。** 按重要性：

1. **有界队列**：队列满时显式选择策略（阻塞生产者 / 返回 `QueueFull` / 丢弃最旧），默认阻塞，避免无界内存增长。
2. **异常传播**：任务用 `packaged_task` 包进 `std::function<void()>`，异常存进 future；`future` 析构会阻塞等待（`std::async` 的坑），故对外返回自有 `TaskHandle`。
3. **两阶段 shutdown**：先 `stop_accepting()`，再 `drain()`，最后 `join()`；析构默认"drain + join"，另提供 `shutdown_now()` 丢弃未开始任务。绝不用 detached 线程。
4. **不要在池内等待池**：任务 A 提交 B 并等 B，在固定线程数下必然死锁；要么提供 caller-runs/窃取，要么在 API 层禁止嵌套等待。
5. **每任务 `stop_token`**：让长任务可取消，而不是靠 `pthread_cancel`（资源状态不可控）。
6. **可观测**：队列深度、活跃线程、完成/拒绝计数。

表 4：线程机制取舍

| 机制 | 语义 | 陷阱 | 用途 |
|---|---|---|---|
| `std::thread` | 一对一 OS 线程 | 异常逃逸 terminate、无取消 | 底层基元 |
| `std::jthread` | 析构 request_stop + join | 需 C++20 工具链 | 推荐默认 |
| `stop_token` | 协作取消、一次性 | 回调需短、抛异常 terminate | 任务取消协议 |
| 线程池 | 复用线程、有界队列 | 池内嵌套等待死锁 | 编译器并行、I/O 并发 |
| `std::async` | 最简异步 | future 析构阻塞、可能串行 | 不推荐对外 |

## 六、建议新增模块（按"编译器标准库基础"排序）

排序依据：编译器自身最先依赖的排前面，纯生态功能排后面。

表 5：推荐模块清单

| # | 模块 | 优先级 | 依赖 | 平台差异 | 最小 API |
|---|---|---|---|---|---|
| 1 | path | P0 | encoding | Windows 盘符/UNC/`\\?\` 长路径/大小写不敏感；POSIX 单字节、`/`、大小写敏感 | `join`/`normalize`/`is_absolute`（纯词法，不做 I/O） |
| 2 | time | P0 | 无 | Windows `FILETIME` 自 1601、100ns；POSIX `timespec` 自 1970；单调钟与墙钟分离 | `Instant`/`Duration`/`SystemTime` |
| 3 | memory/allocator | P0 | 无 | 对齐分配、大页、overcommit 策略不同；`HeapAlloc`/`VirtualAlloc` vs `mmap` | `Arena`/`alloc_aligned`/`page_size` |
| 4 | dynamic library | P1 | path, encoding | `LoadLibraryW`/`GetProcAddress` vs `dlopen`/`dlsym`；rpath/`@rpath`、符号可见性 | `Library::open`/`symbol<T>` |
| 5 | env | P1 | encoding | Windows 键大小写不敏感、排序、`=C:` 隐藏变量；POSIX 允许重复键 | `get`/`set`/`snapshot` |
| 6 | console/tty | P1 | encoding, env | Windows 控制台 UTF-16 + VT 序列；POSIX `termios`/`isatty` | `is_tty`/`width`/`set_color` |
| 7 | socket | P1 | time, thread | Winsock 需 `WSAStartup`、错误码独立、重叠 I/O；POSIX fd + `EINTR` | `TcpListener`/`TcpStream`/`set_timeout` |
| 8 | fs watch | P2 | path, thread | `inotify`/`kqueue`/FSEvents vs `ReadDirectoryChangesW`；两者都会丢事件（溢出返回 0 字节） | `Watcher::watch(path, recursive)` |
| 9 | compression | P2 | memory | 依赖 zlib/zstd；Windows 无系统 zstd | `deflate`/`inflate` |
| 10 | serialization | P2 | memory, encoding | 端序、对齐、版本兼容需自定 | `encode`/`decode`/稳定哈希 |

前三个是编译器自身就要用的：`path` 定位源文件与缓存，`time` 支撑增量比较，`memory` 支撑 AST/IR 的 arena。`dynamic library` 排在 `socket` 前，因为 LLVM/插件加载是硬需求，网络在 LSP 前可用 stdio 顶替。

## 七、参考实现的取舍

表 6：六个生态对照

| 维度 | Rust std | Go | C++ std | Boost | .NET | Java NIO |
|---|---|---|---|---|---|---|
| 定位读写 | `FileExt::read_at/write_at`，显式短读，`read_exact_at` 忽略 `Interrupted` [23] | `os.File.ReadAt` + `io` 包统一流抽象 | 无 | 无 | `FileStream` + 共享模式 | `FileChannel.read(buf,pos)` |
| 文件锁 | 无跨平台 | 无（需 `x/sys`） | 无 | 无 | `FileShare` 打开参数 | `FileLock`，JVM 级、按劝告用 [6] |
| 进程 | `std::process::Command` + `Stdio` | `os/exec`：`WaitDelay`/`ErrDot`/`Context` [20] | 无 | `process` | `Process` | `ProcessBuilder` |
| 编码 | `str` 强制 UTF-8，`OsStr` 区分 | `string` 任意字节 + `utf8.Valid` | 无 | 无 | `Encoding` | `Charset` |
| 线程池 | 无（rayon/tokio） | goroutine + 有界 channel | 无 | 有 | `Task`/线程池 | `ExecutorService` |
| 判断 | 显式优先，值得抄 | 进程模型最完整 | 只借基元 | 太重 | 平台绑定深 | 锁语义最清楚 |

取舍：借 Rust 的显式短读与错误分类、Go 的进程死锁文档、Java 的"锁只承诺劝告"、C++ 的 RAII；不借 Rust 的 `OsStr` 双类型复杂度（内部统一 UTF-8）、不借 Go 的 GC 运行时假设、不把 Boost 作为依赖。

## 八、结论：五条可执行决策

1. **文件 API 只暴露偏移读写**，短读短写在库内循环、`EINTR` 自动重试，`seek` 不进入公共 API。
2. **`atomic_write` 是唯一写文件入口**，内部固定临时文件 + `fsync` + `rename`/`ReplaceFileW` + 目录 `fsync`，不提供"直接覆盖写"。
3. **编码默认 UTF-8 + `Strict`**，检测只作带置信度的提示，替换/丢弃/转义必须显式传参并回传统计。
4. **进程统一为 `Command` + `ExitStatus` + 三档终止**，POSIX 走 `posix_spawn`，Windows 走 `CreateProcessW` + Job Object，管道默认并发泵出。
5. **线程池内置、取消用 `stop_token` 协议**，`jthread` 作默认线程，异常经 `TaskHandle` 传播。

## 参考资料

1. pread(2) — https://man7.org/linux/man-pages/man2/pread.2.html
2. read(2) — https://man7.org/linux/man-pages/man2/read.2.html
3. write(2) — https://man7.org/linux/man-pages/man2/write.2.html
4. flock(2) — https://man7.org/linux/man-pages/man2/flock.2.html
5. LockFileEx (Microsoft Learn) — https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex
6. java.nio.channels.FileLock (Java SE 21) — https://docs.oracle.com/en/java/javase/21/docs/api/java.base/java/nio/channels/FileLock.html
7. fsync(2) — https://man7.org/linux/man-pages/man2/fsync.2.html
8. ReplaceFileW (Microsoft Learn) — https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew
9. FlushFileBuffers (Microsoft Learn) — https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers
10. feature_test_macros(7) — https://man7.org/linux/man-pages/man7/feature_test_macros.7.html
11. mmap(2) — https://man7.org/linux/man-pages/man2/mmap.2.html
12. MapViewOfFile (Microsoft Learn) — https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile
13. Unicode FAQ: UTF-8, UTF-16, UTF-32 & BOM — https://unicode.org/faq/utf_bom.html
14. ICU User Guide: Character Set Detection — https://unicode-org.github.io/icu/userguide/conversion/detection.html
15. uchardet（Mozilla 检测器移植） — https://github.com/BYVoid/uchardet
16. iconv(3) — https://man7.org/linux/man-pages/man3/iconv.3.html
17. ICU User Guide: Using Converters — https://unicode-org.github.io/icu/userguide/conversion/converters.html
18. CreateProcessW (Microsoft Learn) — https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw
19. posix_spawn(3) — https://man7.org/linux/man-pages/man3/posix_spawn.3.html
20. os/exec (Go) — https://pkg.go.dev/os/exec
21. C++ Standards Support in GCC — https://gcc.gnu.org/projects/cxx-status.html
22. C++ 工作草案 [thread.stoptoken] — https://timsong-cpp.github.io/cppwp/thread.stoptoken
23. std::os::unix::fs::FileExt (Rust) — https://doc.rust-lang.org/std/os/unix/fs/trait.FileExt.html
