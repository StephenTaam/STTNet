## STTNet

## Lightweight High-Performance C++ Network Framework

STTNet is a lightweight, high-performance server framework based on the **C++17 standard**.  It utilizes the Reactor event-driven model and epoll for high-concurrency, non-blocking network communication, providing complete **high-performance network communication capabilities**. It supports **TCP/UDP/HTTP/WebSocket and their encrypted variants (TLS+TCP, HTTPS, WSS)**.  It also supports common server-side functionalities such as file operations, time operations, logging, common data processing, JSON data processing, encryption/decryption, signal management, process management, and information security.  It includes built-in features such as a logging system, epoll high-concurrency event-driven model, multi-threading, thread safety, heartbeat monitoring, and exception and signal handling.

Case: A stress test of an HTTP service program written in this framework on a small development board with 4 cores and 4GB of memory achieved a throughput of 65,000 requests per second and an average latency of 2-3ms.

> Author: StephenTaam ([1356597983@qq.com](mailto:1356597983@qq.com))
> Language: C++11
> Platform: Linux
> Dependencies: OpenSSL, JsonCpp, pthread

---

## 📦 Core Framework Features

* ✅ Based on modern C++11 standard
* ✅ Simple and easy to use, clear API

# 🔌 Communication Features

* ✅ Epoll + multithreaded consumer model for high-concurrency processing
* ✅ TCP, UDP, HTTP, WebSocket support
* ✅ Supports encrypted communication (TLS+TCP, HTTPS, WSS)
* ✅ Supports custom callback registration for flexible request handling

# 🔧 Tools and Service Modules

* ✅ Logging system encapsulation (multi-thread write support, log rotation)
* ✅ File I/O encapsulation (thread-safe, lock mechanisms)
* ✅ Time utility wrappers
* ✅ Numeric utilities, string utilities, JSON data handling
* ✅ Encryption and decryption utilities

# 🧿 System Enhancements

* ✅ Thread pool support
* ✅ Exception and signal management
* ✅ Process management and heartbeat monitoring
* ✅ User-friendly interface and modular structure
- ✅ Information security module
---

## 🧱 Framework Module Structure

```
stt
├── file
│   ├── FileTool / File / LogFile
│   └── File operation tool + file read and write encapsulation + log module
├── time
│   ├── DateTime / Duration
│   └── Time tools
├── data
│   ├── CryptoUtil / BitUtil / RandomUtil / NetworkOrderUtil / PrecisionUtil / HttpStringUtil / WebsocketStringUtil / NumberStringConvertUtil / 
│       NumberStringConvertUtil / JsonHelper
│   └── Data processing tools (encryption and decryption, numerical values, strings, Json)
├── network
│   ├── TcpServer / UdpServer / HttpServer / WebSocketServer / TcpClient / UdpClient / HttpClient / WebSocketClient
│   └── Multithreaded epoll network server encapsulation Client communication encapsulation
├── system
│   ├── ServerSetting / HBSystem /Process
│   └── Framework initialization, signal/process/heartbeat management
├── security
│   ├── ConnectionLimiter
│   └── Current limiting module
```

---

## 🚀 Quick Start

# Sample Project `main`

The sample project uses various system and third-party libraries: `jsoncpp`, `OpenSSL`, and `pthread`, and includes the framework module `sttnet.h/.cpp`.

## 🧹 Installing Dependencies

Before compiling the project, ensure the following libraries are installed:

* [jsoncpp](https://github.com/open-source-parsers/jsoncpp)
* OpenSSL (`libssl`, `libcrypto`)
* POSIX Threads (`pthread`)
* g++ compiler (supporting C++11 or higher)

Install these dependencies using the following commands for different Linux distributions:

# 🐧 Ubuntu / Debian (APT-based)

```bash
sudo apt-get update
sudo apt-get install libjsoncpp-dev libssl-dev build-essential
```

# 🐧 Fedora / RHEL / CentOS (DNF/YUM-based)

```bash
sudo yum update
sudo yum install -y gcc-c++ jsoncpp-devel openssl-devel
```

# 🐧 Arch / Manjaro

```bash
sudo pacman update
sudo pacman -S --noconfirm jsoncpp openssl base-devel
```

### 🛠️ Compile

```bash
g++ -std=c++11 -o main main.cpp src/sttnet.cpp -ljsoncpp -lssl -lcrypto -lpthread

# Or use `make` to manage the build.
```

(`main.cpp` is the sample entry demonstrating use of this framework)

---

## 🧪 Sample Code: Starting an HTTP Server

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

## 📖 Documentation

- `docs/api/html_Chinese/index.html` 👉 Class and method documentation(Chinese)
- `docs/api/html_English/index.html` 👉 Class and method documentation(English)

---

## 📁 Recommended Project Structure

```
.
├── src/                 # Source files
│   ├── sttnet.cpp
├── include/             # Header files
│   ├── sttnet.h
│   ├── sttnet_English.h  # the Header file in English Version
├── main.cpp             # Sample project entry
├── server_log           # Log folder generated after successful run
├── docs/                # Documentation directory
│   ├── api              #api documentation
├── README_English.md            # Project description
├── Makefile             # Build configuration
```

## 📄 License

This project is licensed under the MIT License. You are free to use, modify, and distribute it commercially, but please retain the author attribution.


## 📝 Changelog

### v0.2.0 - 2025-07-05

🚀 Major architecture upgrade:

- All server modules refactored to use **non-blocking I/O with epoll edge-triggered mode (EPOLLET)**
- Introduced **state-machine-based connection handling**
- Improved performance and clarity under high concurrency
- Better compatibility with multi-threading and multi-process modules
- Some APIs are no longer compatible

⚠ There is a critical error in the receive buffer for the service class function, please deprecate that version and upgrade to v0.3.1

### v0.3.0 - 2025-07-07

- The stt::d ata::JsonHelper::getValue function has been simplified, and the meaning of parameters and return values have been modified, which are no longer compatible with the previous version.

- The return value of stt::d ata::HttpStringUtil::get_split_str has been changed and is no longer compatible with previous versions.

⚠ There is a critical error in the receive buffer for the service class function, please deprecate that version and upgrade to v0.3.1

### v0.3.1 - 2025-07-07

fix bug

### v.0.3.4 - 2025-08-28
Added information security module and updated network optimization.

### v.0.3.4 - 2025-12-14
1. The logging system was changed to asynchronous logging to optimize performance.
2. Minor functionalities were added to the information security module.
3. Numerous bugs were fixed.

### v.0.4.0 - 2025-12-31
🚀 Major architecture upgrade:

- Changed to a true reactor model
