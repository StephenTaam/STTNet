# STTNet 0.7.0 Build and Integration

STTNet targets Linux and C++17. The normal workflow is to link `STTNet::sttnet`, include `<sttnet.h>`, register callbacks, and start listening.

## 1. Integration Methods

| Scenario | Recommended method |
|---|---|
| STTNet source is vendored in your repository | `add_subdirectory` |
| CMake-managed pinned version | `FetchContent` |
| Several applications share one installation | install + `find_package` |
| A non-CMake build system is used | `pkg-config` |

Every CMake method exposes the same target: `STTNet::sttnet`.

## 2. Dependencies

Ubuntu/Debian:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config libjsoncpp-dev libssl-dev
```

## 3. Install and find_package

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSTTNET_BUILD_TESTS=OFF -DSTTNET_BUILD_EXAMPLE=OFF
cmake --build build
cmake --install build --prefix "$HOME/.local"
```

Consumer `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_server LANGUAGES CXX)

find_package(STTNet 0.7 CONFIG REQUIRED)
add_executable(my_server main.cpp)
target_link_libraries(my_server PRIVATE STTNet::sttnet)
```

Configure with `-DCMAKE_PREFIX_PATH="$HOME/.local"` when using a non-system prefix.

## 4. add_subdirectory

```cmake
set(STTNET_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(STTNET_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/STTNet)

add_executable(my_server main.cpp)
target_link_libraries(my_server PRIVATE STTNet::sttnet)
```

Examples, tests, and STTNet install rules default to off when consumed as a subproject. `STTNET_ENABLE_INSTALL=ON` enables STTNet installation rules inside a superproject.

## 5. FetchContent

Production integration uses a release tag or a full commit SHA as the fixed source revision:

```cmake
include(FetchContent)
set(STTNET_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(STTNET_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  sttnet_source
  GIT_REPOSITORY https://github.com/StephenTaam/STTNet.git
  GIT_TAG        v0.7.0 # use after the release tag is published
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(sttnet_source)

add_executable(my_server main.cpp)
target_link_libraries(my_server PRIVATE STTNet::sttnet)
```

## 6. pkg-config

```bash
export PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig:$PKG_CONFIG_PATH"
c++ -std=c++17 main.cpp $(pkg-config --cflags --libs sttnet) -o my_server
```

## 7. Five-minute HTTP server

See [`examples/http_hello.cpp`](../examples/http_hello.cpp) for the complete program:

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

A request can be sent with `curl -i http://127.0.0.1:8080/ping`; `kill -15 <pid>` or Ctrl-C enters the graceful shutdown path.

## 8. Common HTTP operations

```cpp
const std::string_view contentType=request.headerValue("content-type");
const std::string_view body=request.bodyView();

client.sendText("created", "201 Created");
Json::Value value;
value["ok"]=true;
client.sendJson(value);
client.redirect("/login");
```

A successful send means the complete response was accepted by the bounded per-connection queue; it does not mean that the peer has received it. Enqueue failure or a `sendData` result of `-101` means the connection has crossed its send boundary and no further response data can be accepted.

## 9. Callback results and workers

Callback results are `1` for success, `0` for work submitted to the WorkerPool, `-1` for failure without closing, and `-2` for failure followed by connection close. Blocking database, filesystem, or RPC work belongs in `putTask`, not in the Reactor callback.

## 10. Graceful production lifecycle

- `blockTerminationSignals()` runs before Worker or Reactor thread creation.
- The main thread waits through `waitForTerminationSignal()` and then calls `close()`.
- Server destruction remains outside asynchronous signal handlers.
- SIGTERM and SIGINT can be graceful; SIGKILL cannot be caught.
- Socket options, write/Worker queue limits, TLS, and graceful timeout are configured before `startListen()`.

See [`SIGNALS_Chinese.md`](SIGNALS_Chinese.md), [`CAPABILITY_Chinese.md`](CAPABILITY_Chinese.md), and [`ROADMAP_Chinese.md`](ROADMAP_Chinese.md) for lifecycle, performance boundaries, and planned capabilities.
