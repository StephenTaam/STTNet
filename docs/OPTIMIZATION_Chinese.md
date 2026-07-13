# STTNet 优化与审计说明

本轮优化集中在网络热路径、并发正确性和可验证性。吞吐量必须在 Linux 上用相同硬件、内核参数、编译器和压测参数做前后对照；本文件不使用未经复测的 QPS 数字。

## 已完成

### 0.7.0 并发与生命周期强化

- 修复 ET accept 的关键旧问题：监听 socket 现在本身就是 `SOCK_NONBLOCK | SOCK_CLOEXEC`，不会在取空 accept 队列后把整个 Reactor 卡死。
- 停止接入由 Reactor 自己执行 `epoll DEL + close`；控制线程只改变状态并通过 eventfd 唤醒，避免跨线程 close 与 accept 之间的 fd 复用竞态。
- Worker 完成和 send-ready 共用合并门铃：同一轮已有 eventfd 通知时不再重复 write；每轮分别设置完成/发送消息预算，防止突发完成消息饿死网络事件。
- 普通 TCP 发送使用最多 64 个 `iovec` 的 `sendmsg`；部分写会精确推进多个队列块。TLS 保持 Reactor 内 `SSL_write` 状态机。
- WorkerPool 增加默认 65536 的有界等待队列、峰值统计和 `stop(drain)`；过载会显式拒绝，退出超时时可丢弃尚未开始的任务。
- `maxFD` 现在表示真实并发连接上限，不再错误地拿 Linux 数字 fd 与连接数比较；新增拒绝连接指标和端口 0 自动分配支持。
- 新增 `ServerSocketOptions` 聚合配置：TCP_NODELAY、keepalive 参数、收发缓冲、SO_REUSEPORT、TCP_DEFER_ACCEPT、TCP_FASTOPEN 和 listen backlog。
- `startListen()` 会等待 Reactor 完成 epoll/eventfd/timerfd 初始化后再返回；任何关键初始化失败均完整回收资源并返回 false。
- `close()/stopListen()` 默认给在途 Worker 和已排队响应 5 秒排空时间；停止新请求后继续推进写队列，超时才强制回收并计数。
- 停止完成后统一清空跨线程消息、续读、超时候选与写注册表，支持同一 Server 对象无旧代次污染地再次监听；实际监听端口以原子状态发布。
- 空闲连接检查从周期性全表扫描改为每秒增量轮转，避免大量长连接下的 O(n) Reactor 停顿尖峰。
- 普通 HTTPS/WSS、可选客户端证书和强制 mTLS 现在均可配置；SSL_CTX 采用加锁交换实现安全证书热重载，旧会话继续持有原上下文。
- 异步日志门铃合并，不再每 10ms 空轮询；新增日志丢弃计数。HTTP header 名和 Transfer-Encoding 热路径去除临时小字符串分配。
- 指标补充连接拒绝、Worker 拒绝、合并唤醒、写系统调用、批量 iovec、空闲检查、优雅退出超时以及待发/Worker 队列峰值。
- TLS 单次 `SSL_write` 严格限制在 OpenSSL `int` 长度范围内；File 文本行接口补齐零行号、负行号和空文件边界，二进制模式修复关闭泄漏与长度回绕，内存事务增加线程所有权和关闭等待，消除历史 UB。

### 0.6.0 基础重构

- 将按 `maxFD` 一次性构造的连接数组改为稀疏连接表。默认配置不再在启动时构造约一百万个包含字符串、队列和 `std::any` 的对象。
- Reactor 线程由 `detach()` 改为对象持有并 `join()`，运行标志改为原子变量，关闭过程会等待 Reactor 和 WorkerPool 退出。
- 异步任务改为持有 handler 和请求信息的副本，不再引用 Reactor 栈变量；完成消息携带连接代次，避免 fd 被复用后把旧结果交给新连接。
- 连接存在异步任务时延迟释放 fd、TLS 和请求状态，避免 worker 与断连清理并发造成 use-after-free。
- Worker 完成环形队列满时进入带锁溢出队列，避免静默丢失结果后连接永久卡住。
- 服务端增加每连接发送队列；TCP、HTTP、WebSocket 的同步/异步业务回调只提交完整响应，实际 `send`/`SSL_write` 统一由 Reactor 执行，消除了跨线程操作 socket 和 OpenSSL 会话。
- 增加统一的 `EPOLLOUT` 状态机，覆盖普通 socket 的 `EAGAIN` 以及 TLS 的 `WANT_WRITE`/`WANT_READ`；队列由 `eventfd` 唤醒，并对重复通知做合并。
- 每连接默认 4 MiB 待发高水位，超过后主动淘汰慢客户端；Reactor 默认每轮每连接最多发送 256 KiB，避免单个大响应独占事件循环。可在启动监听前通过 `setMaxPendingWriteBytes()` 和 `setWriteBudgetPerEvent()` 调整。
- WebSocket 单播、广播和关闭改用线程安全连接注册表；关闭帧写完后再断开，不再从调用线程遍历或修改 Reactor 的协议状态表。
- 接收缓冲由“每连接立即分配 256 KiB”改为首次读取时分配 8 KiB、按需倍增至配置上限，显著降低大量空闲长连接的常驻内存。
- 修复 TCP 分段发送的偏移错误；去掉每次发送创建 `substr` 的额外分配；修复 WebSocket 缓冲移动长度和 HTTP 重叠内存复制问题。
- UDP 使用线程安全的 `getaddrinfo`，并按数据报语义只发送一个完整 datagram。
- 使用 `accept4(..., SOCK_NONBLOCK | SOCK_CLOEXEC)`，分别设置 `SO_REUSEADDR` 和 `SO_REUSEPORT`，限制单次 `epoll_wait` 批量为 4096。
- TLS 服务端改用 `TLS_server_method()`，最低 TLS 1.2，正确组合双向认证标志，并补齐失败路径资源释放和 `WANT_WRITE` 事件处理。
- 修复 WebSocket 遍历中 erase 后解引用迭代器、关闭码字节序、HTTP 二进制响应和响应头换行问题。
- HTTP/1.1 解析器增加大小写不敏感字段、流水线、chunked/trailers、header/body 上限及 CL/TE request-smuggling 防护。
- WebSocket 握手和帧解析增加 mask、RSV/opcode、最小长度、分片、控制帧、关闭码及 UTF-8 校验。
- 增加无锁运行指标快照，覆盖连接、TLS、HTTP、发送字节、当前待发字节与队列溢出。
- 异步日志改为条件变量唤醒，停止时排空队列；MPSC 的 consumer head 改为原子量并隔离缓存行。
- 英文头文件转发到唯一的 ABI 头文件，避免两份声明与实现长期漂移。
- 增加 CMake、Linux CI、ASan/UBSan 配置、并发回归测试及独立 HTTP 压测入口。
- 并发测试新增：多生产者同时写同一连接、发送顺序、socket 背压、队列高水位和二进制 payload 覆盖。

## 发送路径

业务线程调用 `sendData()` / `sendBack()` / `sendMessage()` 后，数据进入对应连接的有界队列，并通过 MPSC 队列与 `eventfd` 通知 Reactor。Reactor 按以下状态推进：

1. 可以写：在单连接公平预算内持续发送。
2. socket `EAGAIN` 或 TLS `WANT_WRITE`：订阅 `EPOLLOUT`，等待内核通知。
3. TLS `WANT_READ`：等待下一次 `EPOLLIN` 后继续写。
4. 队列仍有数据但公平预算耗尽：重新排队，让出 Reactor 给其他连接。
5. 超过高水位、传输错误或业务请求关闭：由 Reactor 统一释放 fd、TLS 和连接状态。

这套模型的目标不是让队列无限吸收数据，而是让快连接保持高吞吐，同时对慢客户端施加明确、可配置的内存上限。

## 验证方式

Linux 构建和测试：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Sanitizer：

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DSTTNET_ENABLE_SANITIZERS=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

安装 `wrk` 后运行 HTTP 基准：

```bash
bash benchmarks/run_http.sh
CONNECTIONS=2000 THREADS=8 DURATION=60s bash benchmarks/run_http.sh
```

在 128 个故意每 200ms 只读取 1 字节的慢客户端同时存在时，测量 `/ping` 延迟与吞吐：

```bash
SLOW_CLIENTS=128 CONNECTIONS=2000 THREADS=8 DURATION=60s \
  bash benchmarks/run_slow_clients.sh
```

## 下一阶段建议

1. 在多核服务器上增加 `SO_REUSEPORT + 每核独立 Reactor` 模式；连接与 TLS 会话固定归属一个 Reactor，跨 Reactor 仅传递业务消息。
2. 将当前约万行实现按 `core/reactor`、`protocol/http`、`protocol/websocket`、`tls`、`security` 拆分，并把平台 API 放进独立 backend。
3. 将 HTTP parser 接入 libFuzzer/AFL corpus，WebSocket 接入 Autobahn Testsuite，并把回归 corpus 放进 CI。
4. 将当前增量空闲检查升级为时间轮，并把限流表按 Reactor/哈希分片，进一步压低超大连接规模下的维护成本。
5. 增加静态文件 `sendfile`、请求耗时直方图、限流命中、Prometheus exporter 和 tracing hook。
6. 预生成常见 HTTP 响应头，引入小对象/请求 arena，并以同机 perf、火焰图和 allocator 统计决定是否继续池化。
