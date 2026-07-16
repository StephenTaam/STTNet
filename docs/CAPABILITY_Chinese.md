# STTNet 0.7.0 能力与性能定位

## 结论

STTNet 现在适合定位为“Linux 上轻量、可嵌入、以 HTTP/1.1 与 WebSocket 为主的 C++17 Reactor 框架”。本轮之后，它已经具备生产服务最基本的并发正确性：连接状态按实际活跃 fd 分配，业务 Worker 不直接操作 socket/SSL，每连接有界发送队列统一由 Reactor 推进，并对慢客户端实施背压和公平调度。

它还不是 uWebSockets、Drogon、userver 这类成熟头部框架的同等级替代品。差距主要不在“会不会 epoll”，而在多 Reactor 扩展、协议广度、路由与中间件生态、持续模糊测试、标准基准、可观测性和长期生产验证。

## 当前能力矩阵

| 能力 | 当前状态 | 说明 |
|---|---|---|
| Linux epoll Reactor | 可用 | 每个 Server 一个 Reactor，非阻塞 ET，eventfd 跨线程唤醒 |
| WorkerPool | 可用 | 固定线程池、有界任务背压、可选择排空/丢弃等待任务、任务异常隔离 |
| TCP / TLS | 可用 | 服务端 I/O 归 Reactor；TLS 最低 1.2；单向 TLS、可选客户端证书和 mTLS；安全热重载 |
| HTTP/1.1 | 可用 | Content-Length、chunked、trailers、keep-alive、流水线延续解析 |
| HTTP 易用 API | 可用 | header 查询、统一 body view、文本/JSON/重定向响应 |
| WebSocket | 可用 | 严格握手、mask/opcode/长度校验、分片、ping/pong/close、UTF-8 校验 |
| UDP | 可用 | 保持数据报边界，使用线程安全地址解析 |
| 背压 | 可用 | 默认每连接 4 MiB 高水位，超限返回 -101 并淘汰慢客户端 |
| 写公平性 | 可用 | 默认单连接单轮 256 KiB，预算耗尽后重新调度 |
| TLS 单线程所有权 | 可用 | Worker 仅提交响应，`send`/`SSL_write`/关闭由 Reactor 统一执行 |
| 优雅退出 | 可用 | SIGTERM/SIGINT 同步等待；停止接收后排空在途 Worker/响应；超时强制回收 |
| 运行指标 | 基础可用 | 连接、TLS、HTTP、发送/批量写、队列峰值、唤醒合并、超时和过载快照 |
| Socket 调优 | 可用 | TCP_NODELAY、keepalive、缓冲、REUSEPORT、DEFER_ACCEPT、FASTOPEN、backlog 聚合配置 |
| 限流 | 可用 | 连接/IP/path 策略；空闲超时已增量轮转，限流表仍可继续按 Reactor 分片 |
| 构建与分发 | 可用 | install/export、`STTNet::sttnet`、find_package、add_subdirectory、FetchContent、pkg-config |
| HTTP/2 / HTTP/3 | 不支持 | 当前只实现 HTTP/1.1 |
| 多 Reactor/每核分片 | 不支持 | 单 Server 的网络推进受单 Reactor 核心上限约束 |
| writev/sendmsg | 可用 | 普通 TCP 最多 64 个发送块合并为一次 sendmsg；TLS 仍按 SSL_write 推进 |
| 零拷贝/sendfile | 不支持 | 静态大文件仍有进一步减少用户态复制的空间 |
| Prometheus/Tracing | 不支持 | 已有快照指标，但没有 exporter、直方图、trace context |

## 性能大概处于哪里

本轮没有给出新的绝对 QPS，原因是当前开发环境不是可运行 epoll 的 Linux 压测机，而且跨机器、跨内核参数和跨压测配置的数字没有比较意义。README 中 4 核开发板约 6.5 万请求/秒是旧版本历史记录，不能直接代表 0.7.0，也不能拿它和其他框架公开榜单直接相除。

可以有把握地判断以下趋势：

- 启动和空闲连接内存显著改善：旧实现按 `maxFD` 构造整张连接对象数组；新实现只为活跃连接建状态，接收缓冲从每连接立即 256 KiB 改为首次读取 8 KiB、按需增长。
- 慢客户端下吞吐和尾延迟会比旧实现稳定：业务线程不再阻塞写 socket/SSL，待发内存有上限，单连接写预算避免大响应长期占用 Reactor。
- Worker/send-ready 的 eventfd 门铃会合并，普通 TCP 小块响应会用 iovec 批量发送；高完成速率和 WebSocket 小消息场景的系统调用数应明显下降。
- ET listener 已改为真正的非阻塞 socket；这修复了 accept 队列取空后 Reactor 可能阻塞的旧问题，属于并发能力的正确性修复而不只是微优化。
- 空闲连接检查已从 Reactor 周期性 O(n) 全表停顿改为按秒增量轮转，连接规模较大时尾延迟更平滑。
- 多核纯网络上限仍会早于每核独立 Reactor 的框架出现：一个 Server 目前只有一个 Reactor；Worker 可并行计算，但 accept、协议读取、TLS 网络推进和发送仍归一个网络线程。
- 小型同步 HTTP 路由应明显快于 thread-per-connection 设计，并有机会接近普通异步框架的中高区间；但在没有同机数据前，不应宣称超过 uWebSockets、Drogon 或 TechEmpower 头部实现。

主流框架定位参考：

| 框架 | 相比 STTNet 的主要优势 | STTNet 的相对特点 |
|---|---|---|
| uWebSockets | 极致低开销、成熟压测/模糊测试/Autobahn、长期针对热路径优化 | API 和代码面更轻，便于学习、嵌入和按项目定制 |
| Drogon | 成熟异步 Web 框架、路由/中间件/ORM/生态、公开基准经验 | 依赖和抽象更少，适合小型 TCP/HTTP/WS 服务 |
| userver | 大型生产级异步框架、组件、可观测性和服务治理能力完整 | 学习和部署成本更低，但生产配套远少于 userver |

参考：uWebSockets 官方仓库 <https://github.com/uNetworking/uWebSockets>，Drogon 官方文档 <https://drogonframework.github.io/drogon-docs/>，userver 官方文档 <https://userver.tech/docs/v2.15/>，TechEmpower FrameworkBenchmarks <https://github.com/TechEmpower/FrameworkBenchmarks>。

## 如何得到可信数字

应在同一台 Linux 裸机或固定 CPU 配额容器中，用相同编译器、`-O3 -DNDEBUG`、内核、连接数、响应体和 keep-alive 参数，对修改前 commit、0.7.0、uWebSockets/Drogon 分别预热后测试至少 3 轮，并同时记录：

- Requests/sec、传输吞吐；
- p50/p95/p99/p999 延迟；
- CPU、RSS、上下文切换和系统调用；
- 512/2,000/10,000 长连接下的结果；
- 128 个慢读客户端并存时的正常请求吞吐和 p99；
- HTTP、HTTPS、WebSocket 三种场景分别测试。

仓库已提供 `benchmarks/run_http.sh` 和 `benchmarks/run_slow_clients.sh`，用于普通与慢客户端对照。

## API 与 ABI 兼容性

常用业务 API 没有被重写：`setFunction`、`setGetKeyFunction`、`putTask`、`startListen`、`sendBack`、`sendMessage` 以及 HTTP/WebSocket 请求结构的常用字段仍然保留。绝大多数应用只需重新编译，不需要修改业务代码。

0.7.0 不是二进制 ABI 兼容升级，因此升级过程包含框架与所有依赖目标的重新编译：

| 变化 | 源代码影响 | 说明 |
|---|---|---|
| 服务端 handler 的发送改为有界队列 | 常见调用不变 | 成功表示“已入队”，不是“对端已收到”；超限新增 -101 |
| `HttpServerFDHandler::solveRequest` 新增默认参数 | 旧源码可编译 | 可配置 HTTP header 上限；C++ 符号变化，需重链 |
| `WorkerPool::submit` 从 `void` 改为 `bool` | 忽略返回值的旧调用仍可编译 | 停止后提交现在明确返回 false |
| `TcpServer::close`/析构改为 virtual | 派生类语义更正确 | 类布局/vtable 改变，属于 ABI 变化 |
| 新增发送配置和 `getMetrics()` | 纯新增 | 不使用则无需改业务代码 |
| `WorkerPool(size, capacity)` / `stop(drain)` | 源码兼容新增 | 默认任务上限 65536；过载不再无界占用内存 |
| `ServerSocketOptions` / `getListenPort()` | 纯新增 | 可配置 TCP_NODELAY、keepalive、缓冲、SO_REUSEPORT、backlog，支持端口 0 |
| `setGracefulShutdownTimeout()` | 纯新增 | close 默认最多等待 5 秒完成网络排空；运行中的用户任务仍需自行返回 |
| `TLSClientAuthMode` 与 `setTLS` 重载 | 纯新增 | 普通 HTTPS/WSS 不再被迫要求客户端证书；旧四参数版本仍保持强制 mTLS 语义 |
| `WebSocketClient::getServerPort()` | 行为修复 | 仍返回 string，但现在是端口而不是错误的服务器 IP；新增整数版本 |
| TCP/TLS 客户端连接 | 行为修复 | 默认阻塞连接、线程安全 DNS、SNI/主机名验证、空 CA 使用系统信任库 |
| WebSocket 协议校验更严格 | 非法客户端可能被拒绝 | 拒绝未 mask、非法关闭码、非法 UTF-8 和错误 Upgrade 握手 |
| `File::closeFile()` / 内存事务 | 签名不变、行为收紧 | close 可重复调用；会等待其他线程事务；unlock 仅允许由加锁线程执行；非法行号改为安全失败 |
| HTTP 请求/响应便利 API | 纯新增 | `headerValue/bodyView/sendText/sendJson/redirect`，旧代码不需修改 |
| `JsonHelper::toString()` | 行为修复 | 序列化完整 Json::Value；字符串标量现在包含合法 JSON 引号 |

Doxygen 的规范声明位于 `include/sttnet.h`；`include/sttnet_English.h` 现在只转发到这一个规范头，避免两套声明再次发生 ABI 漂移。Doxyfile 项目版本已同步为 0.7.0。

## 下一批最值得投入的工作

1. HTTP method/path-parameter router、路由组与 before/after middleware，让 REST 服务不需自己组合 key。
2. 静态文件 `sendfile` + Range + 路径规范化，同时保持有界背压和目录穿越防护。
3. Prometheus exporter、请求耗时直方图与 trace hook，基于现有 metrics 快照做可选 adapter。
4. `SO_REUSEPORT + 每核独立 Reactor`，把连接和 TLS 会话固定到所属 Reactor。
5. 拆分 reactor/http/websocket/tls/security/platform 内部模块，并接入 libFuzzer 与 Autobahn Testsuite。
6. 升级时间轮与限流分片；再根据 perf 数据决定预生成响应头和 arena/pool。
7. HTTP/2/3 优先适配成熟库，不自行实现 QUIC/拥塞控制。完整取舍见 `docs/ROADMAP_Chinese.md`。
