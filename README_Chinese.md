## STTNet
## C++ 轻量级高性能网络框架

STTNet 是一个**C++17标准** 的轻量级高性能服务器框架，采用 Reactor 事件驱动模型与 epoll 实现高并发非阻塞网络通信，具备完整的 **高性能网络通信能力**，支持 **TCP/UDP/HTTP/WebSocket 及其加密变种（TLS+TCP、HTTPS、WSS）**。支持文件操作，时间操作，日志操作，常见的数据处理，json格式的数据处理，加解密，信号管理，进程管理,信息安全等常用服务端功能。并内置了日志系统、epoll高并发模型事件驱动、多线程处理、线程安全、心跳监控、异常和信号处理等功能。

历史案例：旧版本曾在 4 核 4G 小型开发板上记录约 6.5 万请求/秒、平均 2–3 ms。该数据不是 0.7.0 的同机复测结果；当前性能请按仓库 benchmark 在目标 Linux 环境复测。

> 作者：StephenTaam（1356597983@qq.com）
> 语言：C++17
> 平台：Linux  
> 依赖：OpenSSL、JsonCpp、pthread

---

## 📦 框架核心特性一览
- ✅ 基于 C++17
- ✅ 简单易用，接口清晰
# 🔌 通信功能
- ✅ 单线程所有权 epoll Reactor + 有界 WorkerPool，高并发处理
- ✅ TCP、UDP、HTTP、WebSocket 通信支持
- ✅ 支持 TLS+TCP、HTTPS、WSS，以及单向 TLS/可选客户端证书/mTLS
- ✅ 支持自定义回调注册函数处理网络请求，灵活处理逻辑
# 🔧 工具与服务模块
- ✅ 日志系统封装（支持多线程写入、日志文件切割）
- ✅ 文件读写封装（线程安全、锁机制）
- ✅ 时间操作封装
- ✅ 数值工具、字符串工具、JSON 数据处理
- ✅ 加解密
# 🧿 系统增强
- ✅线程池支持
- ✅异常与信号管理
- ✅进程管理和心跳监控机制管理
- ✅易用的接口与模块化结构
- ✅ 信息安全模块

## 0.6.0 基础架构升级

- 连接状态从按 `maxFD` 预分配整张大数组改为稀疏活跃连接表，接收缓冲首次使用时再分配。
- Reactor 与 Worker 可 join；异步任务持有请求副本和连接代次，修复 fd 复用与断连竞态。
- 加入每连接有界发送队列与统一 EPOLLOUT/TLS WANT 状态机，实际 socket/SSL 写入只在 Reactor 执行。
- HTTP/1.1 增加 chunked、trailers、流水线和 request-smuggling 防护；WebSocket 补齐分片、控制帧和 UTF-8 校验。
- 增加 CMake、Linux CI、ASan/UBSan、并发回归测试与独立 benchmark。

## 0.7.0 性能与稳定性要点

- 每连接有界发送队列和统一 EPOLLOUT 状态机；Worker 不直接操作 socket/SSL。
- eventfd/日志唤醒合并、普通 TCP `sendmsg+iovec` 批量写、每轮公平预算。
- 监听 socket 真正非阻塞；连接数正确限流；Worker 队列有界，慢客户端和突发任务均有背压。
- SIGTERM/SIGINT 停止接入后排空在途响应；空闲连接增量检查；启动失败可准确反馈。
- `ServerSocketOptions` 聚合 TCP_NODELAY、keepalive、缓冲、REUSEPORT、DEFER_ACCEPT、FASTOPEN 和 backlog。
- 新增队列峰值、批量写、唤醒合并、拒绝、超时等指标；API 手册版本同步为 0.7.0。
- 增加安装导出、`STTNet::sttnet`、`find_package`、FetchContent 与 pkg-config 接入链路。
- 增加 `headerValue/bodyView` 与 `sendText/sendJson/redirect` 常用 API，以及独立 HTTP/WebSocket 示例。

常见 HTTPS/WSS 使用 `server.setTLS(cert, key)`；历史四参数版本
`server.setTLS(cert, key, password, clientCA)` 仍表示强制双向 TLS。需要可选客户端证书时使用
带 `TLSClientAuthMode` 的五参数重载。
---

## 🧱 框架模块结构

```
stt
├── file
│   ├── FileTool / File / LogFile
│   └── 文件操作工具 + 文件读写封装 + 日志模块
├── time
│   ├── DateTime / Duration
│   └── 时间工具类
├── data
│   ├── CryptoUtil / BitUtil / RandomUtil / NetworkOrderUtil / PrecisionUtil / HttpStringUtil / WebsocketStringUtil / NumberStringConvertUtil / 
│       NumberStringConvertUtil / JsonHelper
│   └── 数据处理工具（加解密、数值、字符串、Json）
├── network
│   ├── TcpServer / UdpServer / HttpServer / WebSocketServer / TcpClient / UdpClient / HttpClient / WebSocketClient
│   └── 多线程 epoll 网络服务端封装 客户端通信封装
├── system
│   ├── ServerSetting / HBSystem /Process
│   └── 框架初始化、信号/进程/心跳管理
├── security
│   ├── ConnectionLimiter
│   └── 限流模块
```
---

## 🚀 快速开始

# 示例项目main

文件中的示例项目，使用依赖多种系统和第三方库: `jsoncpp`、`OpenSSL` 和 `pthread`，包含框架模块 `sttnet.h/.cpp`。

## 🧩 安装依赖

在编译本项目前，请确保系统中已安装以下库：
- [jsoncpp](https://github.com/open-source-parsers/jsoncpp)
- OpenSSL (`libssl`, `libcrypto`)
- POSIX Threads (`pthread`)
- g++ 编译器（支持 C++17 或以上）

在不同发行版的Linux系统中，你可以通过以下命令安装这些依赖：

 # 🐧 Ubuntu / Debian（APT 系统）
```bash
sudo apt-get update
sudo apt-get install libjsoncpp-dev libssl-dev build-essential
```

 # 🐧 Fedora / RHEL / CentOS（DNF/YUM 系统）
```bash
sudo yum update
sudo yum install -y gcc-c++ jsoncpp-devel openssl-devel
```

 # 🐧 Arch / Manjaro
```bash
sudo pacman update
sudo pacman -S --noconfirm jsoncpp openssl base-devel
```

### 🛠️ 编译

```bash
g++ -std=c++17 -o main main.cpp src/sttnet.cpp -ljsoncpp -lssl -lcrypto -lpthread
```

也可使用 `make` 管理仓库内的示例构建。真实用户项目推荐使用下面的 CMake 目标接入。

（`main.cpp` 是这个文件示例中调用这个框架写的实际应用入口）

### 推荐：在用户项目中引入

安装后的 CMake 项目只需：

```cmake
find_package(STTNet 0.7 CONFIG REQUIRED)
target_link_libraries(my_server PRIVATE STTNet::sttnet)
```

也可把仓库放入 `third_party/STTNet`：

```cmake
set(STTNET_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(STTNET_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/STTNet)
target_link_libraries(my_server PRIVATE STTNet::sttnet)
```

FetchContent、pkg-config、安装前缀、WorkerPool 与生产配置见 [`docs/GETTING_STARTED_Chinese.md`](docs/GETTING_STARTED_Chinese.md)。

推荐使用 CMake 构建并运行并发回归测试：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

性能与并发优化的设计、验证方法及后续路线见 [`docs/OPTIMIZATION_Chinese.md`](docs/OPTIMIZATION_Chinese.md)。
服务进程的 SIGTERM/SIGINT/SIGKILL 处理约定见 [`docs/SIGNALS_Chinese.md`](docs/SIGNALS_Chinese.md)。
能力边界、框架对比与 API/ABI 兼容说明见 [`docs/CAPABILITY_Chinese.md`](docs/CAPABILITY_Chinese.md)。
从引入依赖到生产配置的完整教程见 [`docs/GETTING_STARTED_Chinese.md`](docs/GETTING_STARTED_Chinese.md)。
后续“轻量瑞士军刀”功能路线见 [`docs/ROADMAP_Chinese.md`](docs/ROADMAP_Chinese.md)。
本轮完整改动和可直接使用的 commit 文案见 [`docs/CHANGELOG_2026-07-13_Chinese.md`](docs/CHANGELOG_2026-07-13_Chinese.md)。

---

## 🧪 示例代码：启动一个 HTTP 服务

STTNet 的常见 HTTP 服务只需要“创建、注册路由、监听”三步：

```cpp
#include <sttnet.h>

int main()
{
    using namespace stt::network;
    using stt::system::ServerSetting;
    if(!ServerSetting::blockTerminationSignals()) return 1;

    HttpServer server;
    server.setFunction("/ping",[](HttpServerFDHandler &client,
                                  HttpRequestInformation &) {
        return client.sendText("pong") ? 1 : -2;
    });
    if(!server.startListen(8080)) return 2;
    ServerSetting::waitForTerminationSignal();
    return server.close() ? 0 : 3;
}
```

运行后执行 `curl http://127.0.0.1:8080/ping` 即可得到 `pong`。下面是同时展示异步任务、WebSocket 和日志的完整示例。

```cpp
#include <sttnet.h>

using namespace std;
using namespace stt::file;
using namespace stt::network;
using namespace stt::system;

/*
 * Global objects (for demo simplicity)
 * 全局对象（Demo 简化写法）
 */
LogFile* lf = nullptr;
HttpServer* httpserver = nullptr;
WebSocketServer* wsserver = nullptr;

int main(int argc, char* argv[])
{
    /*
     * Block SIGTERM/SIGINT before creating any worker thread.
     * 在创建任何线程前阻塞退出信号。
     */
    if(!ServerSetting::blockTerminationSignals())
        return 1;

    /*
     * Initialize logfile system
     * 初始化日志系统（第二个参数指定语言，默认英文）
     */
    lf = new LogFile();
    ServerSetting::init(lf, "Chinese");

    /*
     * Create HTTP server
     * 创建 HTTP 服务器对象
     */
    httpserver = new HttpServer();

    /*
     * HTTP: key extraction function
     * HTTP：从请求中提取 key（用于路由/上下文）
     */
    httpserver->setGetKeyFunction(
        [](HttpServerFDHandler& k, HttpRequestInformation& inf) -> int {
            inf.ctx["key"] = inf.loc;  // use URL as key
            return 1;
        }
    );

    /*
     * HTTP: /ping
     * Simple synchronous response
     * HTTP：/ping，同步返回
     */
    httpserver->setFunction(
        "/ping",
        [](HttpServerFDHandler& k, HttpRequestInformation& inf) -> int {
            k.sendBack("pong");
            return 1;
        }
    );

    /*
     * HTTP: /async
     * Demonstrates task dispatch to worker thread
     * HTTP：/async，演示投递到工作线程池
     */
    httpserver->setFunction(
        "/async",
        [](HttpServerFDHandler& k, HttpRequestInformation& inf) -> int {
            httpserver->putTask(
                [](HttpServerFDHandler& k2, HttpRequestInformation& inf) -> int {
                    k2.sendBack("async pong");
                    return 1;
                },
                k,
                inf
            );
            return 0;  // handled asynchronously
        }
    );

    /*
     * Start HTTP server
     * 启动 HTTP 监听（端口 8080，2 个 worker）
     */
    httpserver->startListen(8080, 2);

    /*
     * Create WebSocket server
     * 创建 WebSocket 服务器
     */
    wsserver = new WebSocketServer();

    /*
     * WebSocket: global fallback handler
     * WebSocket：全局兜底处理函数
     */
    wsserver->setGlobalSolveFunction(
        [](WebSocketServerFDHandler& k, WebSocketFDInformation& inf) -> bool {
            return k.sendMessage(inf.message); // echo
        }
    );

    /*
     * WebSocket: key extraction
     * WebSocket：提取 key
     */
    wsserver->setGetKeyFunction(
        [](WebSocketServerFDHandler&, WebSocketFDInformation& inf) -> int {
            inf.ctx["key"] = inf.message;
            return 1;
        }
    );

    /*
     * WebSocket: "ping" command
     * WebSocket：ping → pong
     */
    wsserver->setFunction(
        "ping",
        [](WebSocketServerFDHandler& k, WebSocketFDInformation& inf) -> int {
            k.sendMessage("pong");
            return 1;
        }
    );

    /*
     * WebSocket heartbreath (mins)
     * WebSocket 心跳时间(分钟)
     */
    wsserver->setTimeOutTime(1);

    /*
     * Start WebSocket server
     * 启动 WebSocket 监听（端口 5050）
     */
    wsserver->startListen(5050, 2);

    /*
     * Wait synchronously; cleanup is performed in normal thread context.
     * 同步等待 kill -15/Ctrl-C，然后在正常线程上优雅清理。
     */
    ServerSetting::waitForTerminationSignal();
    delete wsserver;
    delete httpserver;
    delete lf;
    return 0;
}

```

---

## 📖 后续文档

- `docs/api/html_Chinese/index.html` 👉 类和方法注释说明（中文）
- `docs/api/html_English/index.html` 👉 类和方法注释说明（英文）
- [`docs/GETTING_STARTED_Chinese.md`](docs/GETTING_STARTED_Chinese.md) 👉 安装、引入、回调语义和生产配置
- [`docs/ROADMAP_Chinese.md`](docs/ROADMAP_Chinese.md) 👉 功能取舍与后续路线

---

## 📁 建议项目结构

```
.
├── src/                 # 源码文件 public.cpp
    ├── sttnet.cpp
├── include/             # 头文件 public.h
    ├── sttnet.h
    ├── sttnet_English.h  #英文版头文件
├── main.cpp             # 示例项目
├── server_log           # 假设启用日志文件系统而且运行成功后会自动生成一个日志文件文件夹
├── docs/                # 文档目录
│   ├── api              #api说明文档
├── README_Chinese.md            #项目说明
├── Makefile             #makefile管理项目构建
```

## 📄 License

本项目采用 MIT License 开源协议，你可以自由使用、修改、商用此项目，但请保留作者署名。

---


### v0.2.0 - 2025-07-05

🚀 Major architecture upgrade / 架构重大升级：

- All server modules refactored to use **non-blocking I/O with epoll edge-triggered mode (EPOLLET)**  
  所有服务器模块重构为 **非阻塞 I/O + epoll 边缘触发（EPOLLET）模式**

- Introduced **state-machine-based connection handling**  
  引入 **基于状态机的连接处理机制**

- Improved performance and clarity under high concurrency  
  在高并发场景下大幅提升性能与逻辑清晰度

- Better compatibility with multi-threading and multi-process modules  
  更好地兼容多线程与多进程模块的协同工作

- Some APIs are no longer compatible
  部分api不再兼容

⚠服务类函数的接收缓冲区存在严重错误，请弃用该版本并升级到v0.3.1

### v0.3.0 - 2025-07-07

- 精简了stt::data::JsonHelper::getValue函数，修改了参数意义，返回值等，不再兼容前面的版本。

- stt::data::HttpStringUtil::get_split_str返回值改变，不再兼容前面的版本。

⚠服务类函数的接收缓冲区存在严重错误，请弃用该版本并升级到v0.3.1

### v0.3.1 - 2025-07-07

fix bug

### v.0.3.4 - 2025-08-28
加入信息安全模块，更新了网络优化。

### v.0.3.4 - 2025-12-14
1，日志系统改为异步日志，优化性能。2，补完信息安全模块的小功能 3，修复大量bug。

### v.0.4.0 - 2025-12-31
🚀 Major architecture upgrade / 架构重大升级：
- 改成真正的reactor模型

### v.0.4.1 - 2026-01-01
-修复TLS连接的bug

### v.0.5.0 - 2026-01-09
-升级信息安全的限流模块
-修复TLS连接的bug:错误时候的关闭方式

### v0.6.0 - 2026-07-13

- 稀疏连接表、按需接收缓冲、可 join Reactor/Worker，显著降低启动与空闲连接内存。
- 每连接有界发送队列、统一 EPOLLOUT/TLS 状态机、连接代次与 Worker 生命周期安全。
- 重写并加固 HTTP/1.1 与 WebSocket 解析，加入 CMake、CI、Sanitizer 和 benchmark。

### v0.7.0 - 2026-07-13

- 修复 ET accept 阻塞、加入 eventfd 唤醒合并、`sendmsg+iovec`、有界 WorkerPool、写入公平性与完整背压指标。
- 增加 socket 聚合调优、TLS 客户端认证模式/热更新、优雅排空、端口 0、停止后重启和更完整的可观测性。
- 修复 HTTP/WebSocket/TCP/TLS/File/信号处理中的多个潜在越界、泄漏、竞态和协议正确性问题。
- 增加标准 CMake 安装包、`find_package`/FetchContent/pkg-config、HTTP 便利 API、可运行示例和分层教程。
- 常用业务 API 保持源码兼容，但类布局和符号已变化，升级必须完整重新编译。
