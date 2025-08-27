## STTNet

## Lightweight High-Performance C++ Network Framework

STTNet is a lightweight, high-performance server framework written in **C++11**, featuring comprehensive **high-performance network communication capabilities**. It supports **TCP/UDP/HTTP/WebSocket and their encrypted variants (TLS+TCP, HTTPS, WSS)**. It also includes file operations, time operations, logging, common data processing, JSON handling, encryption/decryption, signal management, information security ，and process management. Built-in features include a logging system, an epoll-based high-concurrency event-driven model, multithreaded processing, thread safety, heartbeat monitoring, and exception/signal handling.

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
#include"include/sttnet.h"
using namespace std;
using namespace stt::file;
using namespace stt::time;
using namespace stt::data;
using namespace stt::network;
using namespace stt::system;
using namespace stt::security;
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