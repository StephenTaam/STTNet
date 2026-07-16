# STTNet：轻量后端“瑞士军刀”路线图

## 定位原则

STTNet 的目标不是把 ORM、模板引擎、服务注册、消息队列和所有协议都塞进核心，而是提供一个依赖少、容易嵌入、行为可预测的高性能网络内核，再用可选模块覆盖常见后端任务。

判断一个功能是否进入核心需要满足至少一项：

- 大多数 TCP/HTTP/WebSocket 服务都会使用；
- 能显著改善性能、安全或生命周期正确性；
- 没有成熟小型依赖可以更可靠地完成；
- 不使用它时几乎没有运行成本。

## 0.7.0 已补齐的易用性基础

- `STTNet::sttnet` CMake namespaced target。
- 安装、CMake package export、`find_package(STTNet)` 与 `pkg-config`。
- `add_subdirectory` 与 FetchContent 作为子项目时默认不构建测试和示例。
- 独立安装消费项目进入 CI，防止导出包失效。
- HTTP/WebSocket 最小示例和中英文分层上手教程。
- `HttpRequestInformation::headerValue/bodyView`。
- `HttpServerFDHandler::sendText/sendJson/redirect`。
- 编译期版本常量 `stt::version` 与 major/minor/patch。

## P0：下一批最值得实现

| 能力 | 用户价值 | 实现边界 |
|---|---|---|
| HTTP method router、路径参数、路由组 | REST API 不再手写 method/path 分发 | 基数树或紧凑 trie；保持现有 `setFunction` 兼容 |
| Middleware/过滤器链 | 统一做鉴权、CORS、访问日志、request-id | before/after hook；明确同步/Worker 边界 |
| 静态文件与 Range | 管理后台、下载和简单站点高频需要 | Linux `sendfile`；规范化路径；防目录穿越；有界排队 |
| Prometheus 文本导出 | 现有 metrics 快照可以立即接监控 | 独立可选 adapter，不强绑 HTTP 路由 |
| 请求耗时直方图与 trace hook | 定位 p99、Worker 堵塞和慢客户端 | 默认关闭或低开销采样，不引入完整 tracing SDK |
| Parser fuzz 与协议合规套件 | 比继续手写边界判断更能提升可靠性 | libFuzzer corpus + Autobahn WebSocket CI |

## P1-A：继续提高吞吐与大连接能力

1. `SO_REUSEPORT + 每核 Reactor`：连接、TLS 会话和协议状态固定到所属 Reactor；避免共享连接表成为新瓶颈。
2. 时间轮：替代剩余的连接/限流周期维护扫描。
3. 常见响应头模板、小对象 arena 与 buffer pool：合入条件为 Linux perf/allocator 数据能够证明收益。
4. TLS session、证书热更新指标和握手限速：降低 TLS 洪泛与重复握手成本。
5. 客户端连接池、DNS 缓存与明确超时/取消模型：让 STTNet 也适合轻量网关和反向代理。

## P1-B：架构拆分

当前单头、单实现便于复制，但不利于长期维护。公共 `<sttnet.h>` 保持不变，内部可逐步拆为：

```text
src/core/          reactor、connection、worker、backpressure
src/protocol/      http1、websocket
src/transport/     tcp、udp、tls
src/security/      limiter、validation
src/platform/      Linux epoll/eventfd/timerfd
src/observability/ metrics、logging、hooks
```

拆分目标是建立内部边界和增量编译，不是让用户包含更多头文件。

## P2：通过成熟库扩展，而不是自行重写

- HTTP/2：优先适配 nghttp2。
- HTTP/3/QUIC：优先适配成熟 QUIC 实现，不自行实现协议和拥塞控制。
- 压缩：可选 zlib/Brotli adapter，按 Accept-Encoding 和大小阈值启用。
- JWT、OpenTelemetry、Redis/数据库连接池：提供薄 adapter 或示例，不进入网络核心。

## 明确暂不进入核心

- ORM、HTML 模板、前端资源管线；
- 服务发现、配置中心、分布式事务；
- 自研 HTTP/3、密码算法或完整 tracing SDK；
- 默认开启的重量级反射、依赖注入和全局对象容器。

这些功能会扩大依赖与维护面，并削弱 STTNet“轻量、易嵌入、热路径可理解”的优势。
