## STTNet
## C++ 轻量级高性能网络框架

STTNet 是一个**C++17标准** 的轻量级高性能服务器框架，采用 Reactor 事件驱动模型与 epoll 实现高并发非阻塞网络通信，具备完整的 **高性能网络通信能力**，支持 **TCP/UDP/HTTP/WebSocket 及其加密变种（TLS+TCP、HTTPS、WSS）**。支持文件操作，时间操作，日志操作，常见的数据处理，json格式的数据处理，加解密，信号管理，进程管理,信息安全等常用服务端功能。并内置了日志系统、epoll高并发模型事件驱动、多线程处理、线程安全、心跳监控、异常和信号处理等功能。

案例：在4核4G内存的小型开发板上压测这个框架编写的http服务程序，达到了每秒6.5万的吞吐量，时延平均2-3ms。

> 作者：StephenTaam（1356597983@qq.com）
> 语言：C++11  
> 平台：Linux  
> 依赖：OpenSSL、JsonCpp、pthread

---

## 📦 框架核心特性一览
- ✅ 基于C++11现代标准
- ✅ 简单易用，接口清晰
# 🔌 通信功能
- ✅ epoll + 多线程消费者模型，高并发处理
- ✅ TCP、UDP、HTTP、WebSocket 通信支持
- ✅ 支持 TLS+TCP、HTTPS、WSS 加密通信
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
- g++ 编译器（支持 C++11 或以上）

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
g++ -std=c++11 -o main main.cpp src/sttnet.cpp -ljsoncpp -lssl -lcrypto -lpthread

或使用 `make` 管理项目构建。
```

（`main.cpp` 是这个文件示例中调用这个框架写的实际应用入口）

---

## 🧪 示例代码：启动一个 HTTP 服务

```cpp
#include "include/sttnet.h"

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
     * Initialize logfile system
     * 初始化日志系统（第二个参数指定语言，默认英文）
     */
    lf = new LogFile();
    ServerSetting::init(lf, "Chinese");

    /*
     * Create HTTP server
     * 创建 HTTP 服务器对象
     */
    httpserver = new HttpServer(50000, false);

    /*
     * Graceful exit on signal 15 (SIGTERM)
     * 收到 15 号信号时优雅退出
     */
    signal(15, [](int) {
        delete httpserver;
        delete wsserver;
        delete lf;
    });

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
    wsserver = new WebSocketServer(5000, false);

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
     * Block main thread
     * 阻塞主线程，Reactor 在内部运行
     */
    pause();
    return 0;
}

```

---

## 📖 后续文档

- `docs/api/html_Chinese/index.html` 👉 类和方法注释说明（中文）
- `docs/api/html_English/index.html` 👉 类和方法注释说明（英文）

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
