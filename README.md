## STTNet

## Lightweight High-Performance C++ Network Framework

STTNet is a lightweight, high-performance server framework written in **C++11**, featuring comprehensive **high-performance network communication capabilities**. It supports **TCP/UDP/HTTP/WebSocket and their encrypted variants (TLS+TCP, HTTPS, WSS)**. It also includes file operations, time operations, logging, common data processing, JSON handling, encryption/decryption, signal management, and process management. Built-in features include a logging system, an epoll-based high-concurrency event-driven model, multithreaded processing, thread safety, heartbeat monitoring, and exception/signal handling.

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

---

## 🧱 Framework Module Structure

```
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
#include"include/sttnet.h"
using namespace std;
using namespace stt::file;
using namespace stt::time;
using namespace stt::data;
using namespace stt::network;
using namespace stt::system;
//set global variables
LogFile lf;
HttpServer *httpserver;
int main(int argc,char *argv[])
{
	//init logfile and use logfile system and ignore all signal but 15. And all error signals or unknown exception will transmit signal 15.
	//remember to set the second parameter to "Chinese" if you want your logfile write in Chinese (default in English)
	ServerSetting::init(&lf);

	//new a HttpServer Objection
	httpserver=new HttpServer;

	//set a callbacl function after signal 15 to quit decently.
	signal(15,[](int signal){
		lf.writeLog("have received signal 15... Now ready to quit. ");
		delete httpserver;
		/*
			...
			*********You can write the necessary exit process here*************
			...
		*/
	});

	//set a callback function to handle http request
	httpserver->setFunction([](const HttpRequestInformation &info, HttpServerFDHandler &conn) -> bool {
        if (info.loc == "/ping") {
            conn.sendBack("pong");
        } else {
            conn.sendBack("404 Not Found", "", "404");
        }
        return true;
    });

	//start listen port 8080 and add logfile to this server objection.
	httpserver->startListen(8080,&lf);

	//block the main thread
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
├── main.cpp             # Sample project entry
├── server_log           # Log folder generated after successful run
├── docs/                # Documentation directory
│   ├── api              #api documentation
│   ├── include_file     #Copy of the English and Chinese versions in the header file
├── README_English.md            # Project description
├── Makefile             # Build configuration
```

## 📄 License

This project is licensed under the MIT License. You are free to use, modify, and distribute it commercially, but please retain the author attribution.







## STTNet
## C++ 轻量级高性能网络框架

STTNet是一个基于 **C++11 标准** 编写的轻量级高性能服务器框架，具备完整的 **高性能网络通信能力**，支持 **TCP/UDP/HTTP/WebSocket 及其加密变种（TLS+TCP、HTTPS、WSS）**。支持文件操作，时间操作，日志操作，常见的数据处理，json格式的数据处理，加解密，信号管理，进程管理等常用服务端功能。并内置了日志系统、epoll高并发模型事件驱动、多线程处理、线程安全、心跳监控、异常和信号处理等功能。

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

---

## 🧱 框架模块结构

```
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
#include"include/sttnet.h"
using namespace std;
using namespace stt::file;
using namespace stt::time;
using namespace stt::data;
using namespace stt::network;
using namespace stt::system;
//设置全局变量
LogFile lf;
HttpServer *httpserver;
int main(int argc,char *argv[])
{
	//初始化日志文件，启用日志系统和设置系统信号；屏蔽除了15外所有信号，所有错误信号和异常都会发送信号15
	//如果你想日志系统使用中文记得填入第二个参数（默认英文）
	ServerSetting::init(&lf,"Chinese");

	//new a HttpServer Objection
	//新建一个HttpServer对象
	httpserver=new HttpServer;

	//设置收到信号15后的回调函数，为了优雅退出程序。
	signal(15,[](int signal){
		lf.writeLog("收到信号15，正在执行退出前的流程");
		delete httpserver;
		/*
			...
			*********可以在这里继续写退出之前的处理流程*****************
			...
		*/
	});

	//设置回调函数处理Http请求
	httpserver->setFunction([](const HttpRequestInformation &info, HttpServerFDHandler &conn) -> bool {
        if (info.loc == "/ping") {
            conn.sendBack("pong");
        } else {
            conn.sendBack("404 Not Found", "", "404");
        }
        return true;
    });

	//监听8080端口，并且加入日志文件到这个服务对象
	httpserver->startListen(8080,&lf);

	//阻塞主线程
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
├── main.cpp             # 示例项目
├── server_log           # 假设启用日志文件系统而且运行成功后会自动生成一个日志文件文件夹
├── docs/                # 文档目录
│   ├── api              #api说明文档
│   ├── include_file     #头文件中英文版本副本
├── README_Chinese.md            #项目说明
├── Makefile             #makefile管理项目构建
```

## 📄 License

本项目采用 MIT License 开源协议，你可以自由使用、修改、商用此项目，但请保留作者署名。

---
