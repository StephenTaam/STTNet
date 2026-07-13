# STTNet 服务进程信号约定

## 推荐行为

| 信号 | 行为 |
|---|---|
| `SIGTERM`（`kill -15`） | 优雅退出：停止接收连接、等待 Reactor/WorkerPool、关闭连接、排空日志 |
| `SIGINT`（Ctrl-C） | 与 SIGTERM 相同 |
| `SIGPIPE` | 框架忽略；socket 写入通过返回值报告失败 |
| `SIGHUP` | 保留默认语义；应用可自行实现配置/证书重载 |
| `SIGCHLD` | 不忽略，进程管理代码可正常 `waitpid` 回收子进程 |
| `SIGSEGV`/`SIGABRT`/`SIGBUS`/`SIGILL`/`SIGFPE` | 致命退出，不运行复杂 C++ 清理；应保留 core dump 供诊断 |
| `SIGKILL`（`kill -9`） | 内核立即终止，无法捕获、忽略或优雅退出 |
| `SIGSTOP` | 无法捕获或忽略，只暂停进程 |

## 正确接入方式

必须在创建日志线程、Reactor 或 WorkerPool 之前阻塞终止信号：

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

不要在异步 signal handler 中执行 `delete`、日志写入、获取互斥锁、`iostream`、内存分配或 OpenSSL 清理。这些操作不是 async-signal-safe，可能死锁或破坏内存状态。

## 为什么 kill -9 不能优雅退出

SIGKILL 不会把执行机会交给目标进程。操作系统会回收进程的内存、线程和文件描述符，但应用层清理不会运行。因此：

- 重要文件必须采用临时文件加原子 rename、`fsync` 或事务机制；
- 共享内存和 System V IPC 需要启动时清理陈旧状态；
- 心跳表不能仅凭旧 PID 判断进程身份；
- 数据库操作应依赖数据库事务恢复，不能依赖析构函数提交。

HBSystem 会先发送 SIGTERM，最多等待 8 秒，然后发送 SIGKILL。Linux 支持时使用 pidfd 锁定目标进程，避免 PID 重用后误杀无关进程；强杀后仍无法确认退出时，不会无限循环或贸然启动重复实例。

优雅退出不能安全地“杀掉”任意 C++ 线程。STTNet 会先 shutdown 客户端 socket 以唤醒阻塞的网络 I/O，但如果业务回调自身永不返回（死循环、无超时的第三方 I/O），进程仍可能超过退出时限。生产环境应为所有外部 I/O 设置超时，并让 systemd/Kubernetes 在宽限期结束后最终升级到 SIGKILL。
