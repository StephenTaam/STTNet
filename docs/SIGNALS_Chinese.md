# STTNet 服务进程信号约定

## 信号行为

| 信号 | 行为 |
|---|---|
| `SIGTERM`（`kill -15`） | 优雅退出：Reactor 停止接入、排空在途 Worker/响应、关闭连接、排空日志 |
| `SIGINT`（Ctrl-C） | 与 SIGTERM 相同 |
| `SIGPIPE` | 框架忽略；socket 写入通过返回值报告失败 |
| `SIGHUP` | 保留默认语义；应用可自行实现配置/证书重载 |
| `SIGCHLD` | 不忽略，进程管理代码可正常 `waitpid` 回收子进程 |
| `SIGSEGV`/`SIGABRT`/`SIGBUS`/`SIGILL`/`SIGFPE` | 致命退出，不运行复杂 C++ 清理；应保留 core dump 供诊断 |
| `SIGKILL`（`kill -9`） | 内核立即终止，无法捕获、忽略或优雅退出 |
| `SIGSTOP` | 无法捕获或忽略，只暂停进程 |

## 同步信号接入流程

终止信号的阻塞发生在日志线程、Reactor 或 WorkerPool 创建之前：

```cpp
if (!stt::system::ServerSetting::blockTerminationSignals())
    return 1;

stt::file::LogFile log;
stt::system::ServerSetting::init(&log, "Chinese");

// create and start servers...

const int signal = stt::system::ServerSetting::waitForTerminationSignal();
if (signal < 0)
    return 1;

// 在正常主线程上下文中执行 close/delete。
```

`TcpServer::close()` 默认给网络排空 5 秒，可在监听前或运行期间调用
`setGracefulShutdownTimeout(milliseconds)` 调整。超时后框架停止 Reactor、丢弃尚未开始的
Worker 任务并强制关闭连接；`graceful_shutdown_timeouts` 指标会增加。设置为 0 表示立即停止。

服务级 `close()/stopListen()` 由控制线程调用；Reactor 同步回调只处理连接级关闭，使用
handler 的 `close()`。监听 fd、客户端 fd 和 SSL 对象的实际关闭均由 Reactor 所有权路径执行，避免
控制线程 close 与网络线程 accept/send 并发时发生 fd 复用串线。

异步 signal handler 不具备执行 `delete`、日志写入、互斥锁、`iostream`、内存分配或 OpenSSL 清理的安全条件。这些操作不是 async-signal-safe，可能导致死锁或内存状态损坏。

## 为什么 kill -9 不能优雅退出

SIGKILL 不会把执行机会交给目标进程。操作系统会回收进程的内存、线程和文件描述符，但应用层清理不会运行。因此：

- 重要文件的持久化可由临时文件、原子 rename、`fsync` 或事务机制保证；
- 共享内存和 System V IPC 需要启动时清理陈旧状态；
- 心跳表不能仅凭旧 PID 判断进程身份；
- 数据库操作应依赖数据库事务恢复，不能依赖析构函数提交。

HBSystem 会先发送 SIGTERM，最多等待 8 秒，然后发送 SIGKILL。Linux 支持时使用 pidfd 锁定目标进程，避免 PID 重用后误杀无关进程；强杀后仍无法确认退出时，不会无限循环或贸然启动重复实例。

优雅退出无法安全强制终止任意 C++ 线程。网络排空超时可以丢弃尚未开始的任务，但已经运行的 C++ 回调仍依赖自身返回；死循环或无超时的第三方 I/O 会使 `close()` 继续等待线程 join。生产环境中的外部 I/O 具有有限超时，systemd/Kubernetes 可在宽限期结束后升级为 SIGKILL。
