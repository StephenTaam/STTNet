# STTNet 0.7.0 Getting Started and Integration

STTNet targets Linux and C++17. The normal workflow is to link `STTNet::sttnet`, include `<sttnet.h>`, register callbacks, and start listening.

## 1. Choose an integration method

| Scenario | Recommended method |
|---|---|
| STTNet source is vendored in your repository | `add_subdirectory` |
| CMake should fetch a pinned version | `FetchContent` |
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

Examples, tests, and STTNet install rules default to off when consumed as a subproject. Set `STTNET_ENABLE_INSTALL=ON` explicitly if a superproject should install STTNet as well.

## 5. FetchContent

Pin a release tag or a full commit SHA in production:

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

Run `curl -i http://127.0.0.1:8080/ping`, then stop the process with `kill -15 <pid>` or Ctrl-C.

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

A successful send means the complete response was accepted by the bounded per-connection queue; it does not mean that the peer has received it. Stop producing data if enqueueing fails or `sendData` returns `-101`.

## 9. Callback results and workers

Callback results are `1` for success, `0` for work submitted to the WorkerPool, `-1` for failure without closing, and `-2` for failure followed by connection close. Blocking database, filesystem, or RPC work belongs in `putTask`, not in the Reactor callback.

## 10. Graceful production lifecycle

- Call `blockTerminationSignals()` before creating worker or Reactor threads.
- Wait with `waitForTerminationSignal()` on the main thread and then call `close()`.
- Never delete servers from an asynchronous signal handler.
- SIGTERM and SIGINT can be graceful; SIGKILL cannot be caught.
- Configure socket options, write/worker queue limits, TLS, and graceful timeout before `startListen()`.

See [`SIGNALS_Chinese.md`](SIGNALS_Chinese.md), [`CAPABILITY_Chinese.md`](CAPABILITY_Chinese.md), and [`ROADMAP_Chinese.md`](ROADMAP_Chinese.md) for lifecycle, performance boundaries, and planned capabilities.
