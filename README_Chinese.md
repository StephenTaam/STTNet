# STTNet 0.7.0

**面向 Linux 的轻量级高性能 C++17 网络框架。**

[English](README_English.md) · [在线文档](https://sttnet.pages.dev/) · [中文指导手册](docs/guide/Chinese/index.html) · [完整注释 Demo](docs/guide/Chinese/demos.html) · [中文 API](docs/api/html_Chinese/index.html)

STTNet 基于 Linux `epoll` 和 Reactor 事件驱动架构，统一提供 TCP、UDP、HTTP/1.1、WebSocket、TLS、HTTPS 与 WSS 能力。框架负责连接管理、协议解析、事件循环和并发调度，业务代码主要围绕清晰的回调 API 编写。

适合轻量 API 服务、网关、IoT 接入、实时通信、游戏服务端，以及嵌入现有 C++ 工程的网络模块。

## 文档与示例

- 在线指导手册：`docs/guide/Chinese/index.html`（20 章，从构建到生产部署）
- 完整注释 Demo：`docs/guide/Chinese/demos.html`
- 中文 API 参考：`docs/api/html_Chinese/index.html`
- 英文指导手册：`docs/guide/English/index.html`

仓库提供 19 个 CMake 示例目标，覆盖网络服务与客户端、JSON、时间、文件、异步日志、数据工具、安全限流、信号、系统设置和进程监督。

## 主要特点

- C++17 与 Linux `epoll`
- 非阻塞 one-loop-per-thread Reactor 架构
- TCP、UDP、HTTP/1.1、WebSocket、TLS、HTTPS、WSS
- WorkerPool 支持耗时业务异步投递
- 每连接有界发送队列与慢客户端背压
- 优雅退出、信号处理、日志、JSON、文件、时间和常用工具模块
- 统一 CMake 目标：`STTNet::sttnet`
- 支持安装、`find_package`、`add_subdirectory`、FetchContent 和 pkg-config

## 五分钟运行

### 1. 安装依赖

Ubuntu / Debian：

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libjsoncpp-dev libssl-dev
```

Fedora / RHEL：

```bash
sudo dnf install -y gcc-c++ cmake pkgconf-pkg-config jsoncpp-devel openssl-devel
```

Arch Linux：

```bash
sudo pacman -S --needed base-devel cmake pkgconf jsoncpp openssl
```

### 2. 下载并构建

```bash
git clone https://github.com/StephenTaam/STTNet.git
cd STTNet
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DSTTNET_BUILD_EXAMPLE=ON \
  -DSTTNET_BUILD_TESTS=OFF \
  -DSTTNET_BUILD_BENCHMARK=OFF
cmake --build build --parallel
```

### 3. 启动 HTTP 示例

```bash
./build/examples/sttnet_http_hello
```

在另一个终端测试：

```bash
curl -i http://127.0.0.1:8080/ping
```

响应正文应为：

```text
pong
```

按 `Ctrl-C` 可触发优雅退出。

## 最小 HTTP 服务

```cpp
#include <sttnet.h>

int main()
{
    using namespace stt::network;
    using stt::system::ServerSetting;

    // 在 Reactor / Worker 线程创建前阻塞 SIGINT 和 SIGTERM。
    if (!ServerSetting::blockTerminationSignals())
        return 1;

    // 创建服务对象并注册 URL path 为 /ping 的处理函数。
    HttpServer server;
    server.setFunction("/ping",
        [](HttpServerFDHandler &client,
           HttpRequestInformation &) {
            // 响应成功返回 1；发送失败返回 -2 并关闭连接。
            return client.sendText("pong") ? 1 : -2;
        });

    // 开始监听 8080 端口。
    if (!server.startListen(8080))
        return 2;

    // 主线程同步等待 Ctrl-C 或 kill -15。
    ServerSetting::waitForTerminationSignal();

    // 在正常线程上下文中执行优雅关闭。
    return server.close() ? 0 : 3;
}
```

常见服务只需要三步：

1. 创建 Server。
2. 注册回调。
3. 开始监听。

## 常用 HTTP 操作

```cpp
server.setFunction("/user",
    [](HttpServerFDHandler &client,
       HttpRequestInformation &request) {
        const std::string_view contentType =
            request.headerValue("content-type");
        const std::string_view body = request.bodyView();

        Json::Value response;
        response["ok"] = true;
        response["content_type"] = std::string(contentType);
        response["body_size"] =
            static_cast<Json::UInt64>(body.size());

        return client.sendJson(response) ? 1 : -2;
    });
```

常用响应接口：

```cpp
client.sendText("created", "201 Created");
client.sendJson(value);
client.redirect("/login");
```

发送成功表示响应已经进入该连接的有界发送队列，不表示对端已经收到数据。

## WorkerPool

Reactor 回调不应执行阻塞操作。数据库、磁盘和外部 RPC 等耗时业务可投递到 WorkerPool：

```cpp
server.setFunction("/slow",
    [&server](HttpServerFDHandler &client,
              HttpRequestInformation &request) {
        server.putTask(
            [](HttpServerFDHandler &workerClient,
               HttpRequestInformation &) {
                return workerClient.sendText("done") ? 1 : -2;
            },
            client,
            request);
        return 0;
    });
```

回调返回值：

| 返回值 | 含义 |
|---:|---|
| `1` | 当前阶段成功；如果还有后续阶段则继续执行 |
| `0` | 当前阶段已投递 WorkerPool；任务完成后从下一阶段继续 |
| `-1` | 停止当前请求的剩余阶段，但不要求关闭连接 |
| `-2` | 停止处理并要求关闭连接 |

## 最小 WebSocket Echo 服务

```cpp
#include <sttnet.h>

int main()
{
    using namespace stt::network;
    using stt::system::ServerSetting;

    if (!ServerSetting::blockTerminationSignals())
        return 1;

    WebSocketServer server;

    // 没有自定义消息 key 时，所有消息都会进入这个回调。
    server.setGlobalSolveFunction(
        [](WebSocketServerFDHandler &client,
           WebSocketFDInformation &message) {
            // 将客户端消息原样发送回当前连接。
            return client.sendMessage(message.message);
        });

    if (!server.startListen(5050))
        return 2;

    ServerSetting::waitForTerminationSignal();
    return server.close() ? 0 : 3;
}
```

仓库内包含 19 个可直接构建的完整注释示例：

- `examples/http_hello.cpp`：最小 HTTP 路由、404、信号等待和优雅退出
- `examples/http_json.cpp`：解析客户端 JSON，处理 400/422/201 响应
- `examples/worker_pool.cpp`：安全投递阻塞任务、请求快照和队列上限
- `examples/websocket_echo.cpp`：握手检查、文本/二进制帧 Echo 和心跳
- `examples/tcp_echo.cpp`：派生 `TcpServer` 编写原始 TCP Echo
- `examples/udp_echo.cpp`：接收并回发 UDP 数据报
- `examples/http_client.cpp`：同步 HTTP Client、超时和完整响应判断
- `examples/websocket_client.cpp`：WebSocket Client 连接、发送、回调和关闭
- `examples/tls_https.cpp`：加载证书并启动 HTTPS 服务
- `examples/signal_shutdown.cpp`：信号设置、同步等待和安全关闭
- `examples/system_settings.cpp`：日志、Socket、背压、Worker 上限和指标
- `examples/json_tools.cpp`：创建、序列化、解析和校验 JSON
- `examples/time_tools.cpp`：本地时间文本；`checkTime()`、`endTiming()`、`getDt()` 返回 `stt::time::Duration`；时间间隔运算和单调计时
- `examples/file_logging.cpp`：目录、文件读写和有界异步日志
- `examples/crypto_encoding.cpp`：Base64、SHA-1 兼容用途和 AES-CBC 边界
- `examples/data_tools.cpp`：数值转换、精度工具、随机数和网络字节序
- `examples/protocol_parsing.cpp`：URL、Header、Query 和 WebSocket 握手字符串工具
- `examples/security_limiter.cpp`：连接/请求限流与黑名单行为
- `examples/process_supervisor.cpp`：子进程重启监督和安全终止
详细的逐章说明见 [`docs/guide/Chinese/index.html`](docs/guide/Chinese/index.html)。

### 构建并运行测试

```bash
cmake -S . -B build -DSTTNET_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

目前共有 7 个测试目标，包含协议互操作和工具类契约测试。`tests/tool_contract_tests.cpp` 会锁定 `DateTime`/`Duration` 的准确返回类型与行为，并验证严格数字/Base64 解析、二进制文件复制、异步日志排空、AES 实际输出长度和 JSON 类型。

## 接入现有 CMake 项目

### 源码放入工程

推荐目录：

```text
my_server/
├── CMakeLists.txt
├── main.cpp
└── third_party/STTNet/
```

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_server LANGUAGES CXX)

set(STTNET_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(STTNET_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/STTNet)

add_executable(my_server main.cpp)
target_link_libraries(my_server PRIVATE STTNet::sttnet)
```

### 安装后使用

构建并安装 STTNet：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DSTTNET_BUILD_EXAMPLE=OFF \
  -DSTTNET_BUILD_TESTS=OFF
cmake --build build --parallel
cmake --install build --prefix "$HOME/.local"
```

用户工程：

```cmake
find_package(STTNet 0.7 CONFIG REQUIRED)
add_executable(my_server main.cpp)
target_link_libraries(my_server PRIVATE STTNet::sttnet)
```

非系统安装目录可在配置用户工程时指定：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$HOME/.local"
```

### FetchContent

正式项目应固定发布标签或完整 commit SHA：

```cmake
include(FetchContent)

set(STTNET_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(STTNET_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  sttnet_source
  GIT_REPOSITORY https://github.com/StephenTaam/STTNet.git
  GIT_TAG        <release-tag-or-full-commit-sha>)
FetchContent_MakeAvailable(sttnet_source)

add_executable(my_server main.cpp)
target_link_libraries(my_server PRIVATE STTNet::sttnet)
```

## 生产配置基础

以下配置应在 `startListen()` 之前完成：

```cpp
ServerSocketOptions options;
options.tcp_no_delay = true;
options.keep_alive = true;
options.listen_backlog = 4096;
server.setSocketOptions(options);

server.setMaxPendingWriteBytes(4 * 1024 * 1024);
server.setWriteBudgetPerEvent(256 * 1024);
server.setMaxPendingWorkerTasks(65536);
server.setGracefulShutdownTimeout(5000);
```

生命周期注意事项：

- 在创建 Reactor 或 Worker 线程前调用 `blockTerminationSignals()`。
- 主线程使用 `waitForTerminationSignal()` 等待 `SIGTERM` 或 `SIGINT`。
- 收到退出信号后调用 `close()` 执行优雅排空。
- `SIGKILL` 无法捕获，不能触发优雅退出。
- TLS、队列限制、Socket 参数和退出超时应在监听前配置。

## 性能说明

旧版本曾在 4 核 4G 小型开发板上记录约 **6.5 万请求/秒**、平均 **2–3 ms**。这是历史案例，不是 STTNet 0.7.0 在所有机器上的固定结果。

有意义的性能数据应在目标 Linux 机器上，结合实际内核、连接方式、TLS 配置、报文大小和业务逻辑进行测试。

压测脚本位于 `benchmarks/`。

## 文档

- [在线手册](https://sttnet.pages.dev/)
- [中文上手与集成指南](docs/GETTING_STARTED_Chinese.md)
- [English Getting Started](docs/GETTING_STARTED_English.md)
- [中文 API 参考](docs/api/html_Chinese/index.html)
- [English API Reference](docs/api/html_English/index.html)
- [能力与性能边界](docs/CAPABILITY_Chinese.md)
- [信号和优雅退出](docs/SIGNALS_Chinese.md)
- [路线图](docs/ROADMAP_Chinese.md)

## 环境要求

- Linux
- 支持 C++17 的编译器
- CMake 3.16+
- OpenSSL 1.1.1+
- JsonCpp
- pthread

## 许可证

MIT License，详见 [LICENSE](LICENSE)。

作者：StephenTaam · [1356597983@qq.com](mailto:1356597983@qq.com)
