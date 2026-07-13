# STTNet 0.7.0

**A lightweight, high-performance C++17 network framework for Linux.**

[中文说明](README_Chinese.md) · [Online documentation](https://sttnet.pages.dev/) · [Programming Guide](docs/guide/English/index.html) · [Commented Demos](docs/guide/English/demos.html) · [English API](docs/api/html_English/index.html)

STTNet is built around Linux `epoll` and a Reactor event-driven architecture. It provides TCP, UDP, HTTP/1.1, WebSocket, TLS, HTTPS, and WSS support while keeping application code centered on small callback-based APIs.

It is designed for lightweight API services, gateways, IoT access, real-time communication, game backends, and networking modules embedded in existing C++ applications.

## Documentation and examples

- Programming guide: `docs/guide/English/index.html` (14 chapters, build through production)
- Fully commented demos: `docs/guide/English/demos.html`
- English core API quick reference: `docs/guide/English/api-quick-reference.html`
- Complete generated API tree: `docs/api/html_English/index.html`

The repository includes 11 CMake example targets covering HTTP, JSON, WorkerPool, WebSocket, TCP, UDP, HTTP Client, WebSocket Client, TLS, signals, and system settings.

## Highlights

- C++17 and Linux `epoll`
- Non-blocking one-loop-per-thread Reactor architecture
- TCP, UDP, HTTP/1.1, WebSocket, TLS, HTTPS, and WSS
- WorkerPool dispatch for blocking or expensive application work
- Bounded per-connection write queues and slow-client backpressure
- Graceful shutdown, signal handling, logging, JSON, file, time, and utility modules
- CMake target: `STTNet::sttnet`
- Installation, `find_package`, `add_subdirectory`, FetchContent, and pkg-config support

## Five-minute start

### 1. Install dependencies

Ubuntu / Debian:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libjsoncpp-dev libssl-dev
```

Fedora / RHEL:

```bash
sudo dnf install -y gcc-c++ cmake pkgconf-pkg-config jsoncpp-devel openssl-devel
```

Arch Linux:

```bash
sudo pacman -S --needed base-devel cmake pkgconf jsoncpp openssl
```

### 2. Download and build

```bash
git clone https://github.com/StephenTaam/STTNet.git
cd STTNet
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DSTTNET_BUILD_EXAMPLE=ON \
  -DSTTNET_BUILD_TESTS=OFF \
  -DSTTNET_BUILD_BENCHMARK=OFF
cmake --build build --parallel
```

### 3. Run the HTTP example

```bash
./build/examples/sttnet_http_hello
```

In another terminal:

```bash
curl -i http://127.0.0.1:8080/ping
```

The response body should be:

```text
pong
```

Press `Ctrl-C` to trigger graceful shutdown.

## Minimal HTTP server

```cpp
#include <sttnet.h>

int main()
{
    using namespace stt::network;
    using stt::system::ServerSetting;

    // Block SIGINT and SIGTERM before Reactor or Worker threads exist.
    if (!ServerSetting::blockTerminationSignals())
        return 1;

    // Create the server and register the /ping path.
    HttpServer server;
    server.setFunction("/ping",
        [](HttpServerFDHandler &client,
           HttpRequestInformation &) {
            // Return 1 on success, or -2 to close after send failure.
            return client.sendText("pong") ? 1 : -2;
        });

    // Start listening on TCP port 8080.
    if (!server.startListen(8080))
        return 2;

    // Wait synchronously for Ctrl-C or kill -15.
    ServerSetting::waitForTerminationSignal();

    // Perform graceful cleanup from normal thread context.
    return server.close() ? 0 : 3;
}
```

The normal flow is simple:

1. Create a server.
2. Register callbacks.
3. Start listening.

## Common HTTP operations

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

Useful response helpers include:

```cpp
client.sendText("created", "201 Created");
client.sendJson(value);
client.redirect("/login");
```

A successful send means the response was accepted by the connection's bounded write queue. It does not mean the remote peer has already received it.

## WorkerPool

Reactor callbacks should remain non-blocking. Database access, filesystem work, and external RPC calls can be dispatched to the WorkerPool:

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

Callback return values:

| Value | Meaning |
|---:|---|
| `1` | The current stage succeeded; continue when another stage exists |
| `0` | The current stage was submitted to WorkerPool; resume at the next stage |
| `-1` | Stop the remaining stages without requesting connection close |
| `-2` | Stop processing and request connection close |

## Minimal WebSocket echo server

```cpp
#include <sttnet.h>

int main()
{
    using namespace stt::network;
    using stt::system::ServerSetting;

    if (!ServerSetting::blockTerminationSignals())
        return 1;

    WebSocketServer server;

    // Messages without a custom key route are handled here.
    server.setGlobalSolveFunction(
        [](WebSocketServerFDHandler &client,
           WebSocketFDInformation &message) {
            // Echo the received payload to the same connection.
            return client.sendMessage(message.message);
        });

    if (!server.startListen(5050))
        return 2;

    ServerSetting::waitForTerminationSignal();
    return server.close() ? 0 : 3;
}
```

The repository includes 11 fully commented, buildable examples:

- `examples/http_hello.cpp`: minimal HTTP route, fallback, signal wait, graceful close
- `examples/http_json.cpp`: parse client JSON and return 400/422/201 responses
- `examples/worker_pool.cpp`: safe blocking work, request snapshots, bounded queues
- `examples/websocket_echo.cpp`: handshake checks, text/binary echo, heartbeat
- `examples/tcp_echo.cpp`: derive from `TcpServer` and implement a raw TCP echo
- `examples/udp_echo.cpp`: receive and echo complete UDP datagrams
- `examples/http_client.cpp`: synchronous HTTP client, timeout, complete-response check
- `examples/websocket_client.cpp`: connect, send, receive callback, and close
- `examples/tls_https.cpp`: load a certificate and start HTTPS
- `examples/signal_shutdown.cpp`: signal setup, synchronous wait, safe close
- `examples/system_settings.cpp`: logging, sockets, backpressure, Worker limits, metrics
See the chapter-by-chapter [`Programming Guide`](docs/guide/English/index.html).

## Use STTNet in another CMake project

### Vendored source

Recommended layout:

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

### Installed package

Build and install STTNet:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DSTTNET_BUILD_EXAMPLE=OFF \
  -DSTTNET_BUILD_TESTS=OFF
cmake --build build --parallel
cmake --install build --prefix "$HOME/.local"
```

Consumer project:

```cmake
find_package(STTNet 0.7 CONFIG REQUIRED)
add_executable(my_server main.cpp)
target_link_libraries(my_server PRIVATE STTNet::sttnet)
```

When using a non-system prefix, configure the consumer with:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$HOME/.local"
```

### FetchContent

Pin a release tag or a full commit SHA in production:

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

## Production basics

Configure limits and socket behavior before `startListen()`:

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

Important lifecycle rules:

- Call `blockTerminationSignals()` before Reactor or Worker threads are created.
- Wait for `SIGTERM` or `SIGINT` on the main thread with `waitForTerminationSignal()`.
- Call `close()` for graceful drain.
- `SIGKILL` cannot be caught and cannot trigger graceful shutdown.
- Configure TLS, queue limits, socket options, and shutdown timeout before listening.

## Performance note

An older build recorded about **65,000 requests per second** with **2–3 ms average latency** on a 4-core/4GB development board. This is a historical case, not a fixed result for STTNet 0.7.0 or every machine.

Meaningful performance numbers must come from benchmarks on the target Linux host with the intended kernel, connection pattern, TLS settings, payload size, and business logic.

Benchmark scripts are available under `benchmarks/`.

## Documentation

- [Online documentation](https://sttnet.pages.dev/)
- [English getting-started guide](docs/GETTING_STARTED_English.md)
- [Chinese getting-started guide](docs/GETTING_STARTED_Chinese.md)
- [English API reference](docs/api/html_English/index.html)
- [Chinese API reference](docs/api/html_Chinese/index.html)
- [Capability and performance boundaries](docs/CAPABILITY_Chinese.md)
- [Signal and graceful-shutdown guide](docs/SIGNALS_Chinese.md)
- [Roadmap](docs/ROADMAP_Chinese.md)

## Requirements

- Linux
- C++17 compiler
- CMake 3.16+
- OpenSSL 1.1.1+
- JsonCpp
- pthread

## License

MIT License. See [LICENSE](LICENSE).

Author: StephenTaam · [1356597983@qq.com](mailto:1356597983@qq.com)
