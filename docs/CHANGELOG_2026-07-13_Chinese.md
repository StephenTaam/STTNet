# 2026-07-13 优化、修复与架构升级总览

## 推荐 Git commit

标题：

```text
perf: unify reactor writes and harden lifecycle/protocol parsing
```

正文：

```text
- replace maxFD-sized connection arrays with sparse active-connection state
- route all server socket/TLS writes through bounded per-connection queues
- add unified EPOLLOUT/TLS WANT state handling, backpressure and write fairness
- make reactor/worker/logging shutdown joinable and SIGTERM/SIGINT-safe
- protect async results with connection generations and delayed fd teardown
- harden HTTP/1.1 and WebSocket parsing against malformed/smuggling inputs
- fix TCP/TLS client DNS, blocking connect, CA, SNI and hostname verification
- add metrics snapshots, Linux CI, sanitizer tests and benchmark tooling
- update Doxygen/API compatibility, signal and capability documentation
```

## 今天完成的全部内容

### 1. 连接内存与基础并发模型

- 删除按默认 `maxFD=1,000,000` 一次性创建完整连接对象数组的设计，改为只保存活跃 fd 的稀疏表。
- 接收缓冲改为首次读取时分配 8 KiB，并按需倍增到服务器配置上限；空闲长连接不再立即占用 256 KiB。
- Reactor 线程从 detach 改为对象持有并 join；运行状态改为原子变量。
- WorkerPool 支持安全重复停止、排空已接受任务、停止后拒绝新任务并返回 false；任务异常不会终止整个服务进程。
- MPSC 队列支持非默认构造类型，consumer head 原子化并做 cache-line 隔离；满队列使用显式后备队列，不再静默丢完成消息。

### 2. 每连接发送队列与统一网络状态机

- 为每个连接增加线程安全、有界发送队列。TCP、HTTP、WebSocket 的业务回调和 Worker 只提交完整响应，不直接调用服务端 socket/SSL。
- 所有实际 `send`/`SSL_write` 都归 Reactor，收拢 OpenSSL 会话的线程所有权。
- 增加 MPSC send-ready 通知与 eventfd 唤醒，并合并重复通知。
- 建立统一 EPOLLOUT 状态机，覆盖 socket `EAGAIN`、TLS `WANT_WRITE` 和 TLS 写过程中的 `WANT_READ`。
- 默认每连接待发高水位 4 MiB；超过时返回 -101 并关闭慢客户端，避免无界内存增长。
- 默认单连接单轮写预算 256 KiB；预算耗尽后重新排队，避免大响应独占 Reactor。
- 关闭连接时丢弃并计量未发送字节；支持“关闭帧/错误响应写完后再关闭”。

### 3. fd 复用、Worker 生命周期和跨线程安全

- 每个连接引入单调代次号，Worker 完成消息、发送通知和 WebSocket 单播都验证 fd + 代次，防止旧任务串到复用后的新连接。
- 有在途 Worker 时先 shutdown 唤醒阻塞 I/O，延迟释放 fd、TLS、请求状态和缓冲区，避免 use-after-free。
- Worker 使用 handler/请求副本，完成后由 Reactor 合并，不再引用 Reactor 栈对象或跨线程修改协议表。
- WebSocket 单播、广播和关闭改用线程安全连接注册表；广播不再遍历 Reactor 私有协议状态。

### 4. HTTP/1.1 解析与流水线

- 替换旧请求解析器，字段名按大小写不敏感处理。
- 严格校验请求行、HTTP 版本、Host、字段 token 和控制字符。
- 支持 Content-Length、chunked extension、trailers 和请求流水线。
- 拒绝重复 Content-Length、CL+TE 并存、重复 Host、未知/组合 Transfer-Encoding、数字溢出和非最小 framing。
- 新增 64 KiB 默认 header 上限和总接收上限，避免大 header/body 占用失控。
- 一次读入多条流水线请求时保留剩余字节，通过有预算的用户态 continuation 队列继续解析，避免等待一个不会再来的 EPOLLIN。
- 修复解析 key、限流忽略、业务返回 -1 等分支未弹出请求队列导致连接永久卡住的问题。

### 5. WebSocket 协议完整性

- 严格验证 GET Upgrade 握手、Connection token、Version 13 和 16 字节随机 key；无效握手返回 400 后关闭。
- 修复握手后按配置上限 memset 实际 8 KiB 缓冲造成的堆越界，并保留与握手同包到达的首个 WebSocket 帧。
- 重写帧编码：正确处理 FIN/opcode、16/64 位网络序长度、客户端 mask 和服务端无 mask。
- 解析端要求客户端 mask，拒绝 RSV、保留 opcode、非最小长度、过长控制帧和非法 close payload。
- 支持消息分片与分片之间的 ping/pong；ping 原样回显 payload。
- 校验文本消息和 close reason 的 UTF-8；拒绝保留关闭码及 1016–2999 非法线路关闭码。
- Close/Ping/Pong payload 强制不超过 125 字节；服务端关闭帧排队完成后再断开 TCP。

### 6. TCP/TLS/UDP 与通用 socket bug

- 修复 `blockSet` 把 `F_GETFL` 当成设置命令的错误，并同时设置收发超时。
- TCP 客户端恢复文档承诺的默认阻塞 socket，避免普通 `connect` 因 EINPROGRESS 被误判失败。
- TCP 地址解析改用线程安全 `getaddrinfo`，连接成功后正确保存服务器主机与端口，HTTP keep-alive 不再每次误重连。
- 修复 TLS 客户端初始化参数顺序错误导致私钥参数丢失的问题，并补齐失败路径释放。
- TLS 客户端最低 TLS 1.2；空 CA 使用系统信任库；显式 CA 仍支持；增加 SNI 和证书主机名验证。
- TLS 服务端使用现代 method、正确设置双向认证标志，并覆盖握手 WANT_READ/WANT_WRITE。
- `SO_REUSEADDR` 与 `SO_REUSEPORT` 分别设置；accept 使用 NONBLOCK+CLOEXEC。
- UDP 地址解析线程安全化，并保持一个调用对应一个完整 datagram。
- 修复 TCP 分段发送偏移、WebSocket 缓冲移动、HTTP 二进制响应、响应头 CRLF、VLA 和有符号 char 转十六进制等问题。

### 7. 服务退出、SIGTERM、SIGKILL 与进程守护

- 不再在异步 signal handler 中执行日志、delete、锁和 OpenSSL 清理。
- 新增 `blockTerminationSignals()` + `waitForTerminationSignal()`，要求在创建线程前屏蔽 SIGTERM/SIGINT，并在主线程同步等待后执行正常 close/delete。
- SIGPIPE 被忽略；SIGSEGV/SIGABRT 等致命错误保留系统默认行为和 core dump 机会。
- 明确 SIGKILL/`kill -9` 无法捕获和优雅退出；系统资源恢复不能依赖析构函数。
- HBSystem 采用 SIGTERM -> 最多等待 8 秒 -> SIGKILL 的升级策略；Linux 优先使用 pidfd 避免 PID 复用误杀。
- 修复 Process 定时拉起中的重复 fork、wait 对象错误、父进程信号处置被污染和 exec 失败继续运行等问题；Linux 子进程可在父守护进程死亡时收到 SIGTERM。
- `EpollSingle` 改为拥有可 join 线程，并用 eventfd 即时停止；回调 setter 加锁，避免析构时 detached 线程访问已释放对象。

### 8. 可观测性、工程化与验证

- 新增无锁 `ServerMetricsSnapshot`：累计接收/关闭连接、当前连接、accept 错误、TLS 失败、HTTP 请求、排队/已发送/待发字节及各类队列溢出。
- 新增 CMake（C++17、OpenSSL >= 1.1.1）、Release/ASan+UBSan Linux CI 和 graceful shutdown smoke test。
- 新增并发、发送顺序、背压、高水位、TCP 客户端、EpollSingle、信号、HTTP parser、WebSocket 协议回归测试。
- 新增普通 HTTP 压测、慢读客户端压测和大响应端点。
- Doxygen 规范头、项目版本、所有新增 API 和行为变化已同步；英文兼容头转发到唯一规范声明，避免 ABI 漂移。
- 新增优化设计、信号语义、能力/性能定位和 API/ABI 兼容说明。

## 兼容性提醒

- 常用路由、回调、`startListen`、`sendBack`、`sendMessage` 等业务 API 保持不变。
- 服务端回调里的发送成功现在表示“响应已进入有界队列”，实际网络写由 Reactor 异步完成。
- 0.6.0 修改了类布局、virtual 函数和部分 C++ 符号，不保证旧二进制 ABI；提交后应完整重新编译所有目标。
- 非法 HTTP/WebSocket 输入现在会更早被拒绝，这属于安全收紧。

## 本轮验证

- 全量 `src/sttnet.cpp` C++17 语法检查通过。
- concurrency、signal、HTTP parser、WebSocket 四组回归程序通过。
- HTTP request-smuggling、流水线、chunked/trailers、header/body 上限测试通过。
- WebSocket mask、分片、交错控制帧、关闭码、UTF-8 和帧编码测试通过。
- 仓库配置了 Linux Release 与 ASan/UBSan CI；最终 Linux epoll 集成结果以 CI 为准。
