# STTNet 0.7.0 上手与集成指南

STTNet 面向 Linux 和 C++17。最短路径是：安装依赖、把 `STTNet::sttnet` 链接到目标、包含 `<sttnet.h>`、注册回调并监听。

## 1. 选择接入方式

| 场景 | 推荐方式 | 特点 |
|---|---|---|
| 应用仓库内已经放入 STTNet 源码 | `add_subdirectory` | 最直接，跟随应用一起编译 |
| 希望 CMake 自动获取固定版本 | `FetchContent` | 无需手工安装，必须固定 tag 或 commit |
| 多个应用共享同一份框架 | 安装后 `find_package` | 最标准，构建速度快，适合系统镜像/SDK |
| 非 CMake 工程 | `pkg-config` | 自动给出 include、库和依赖参数 |
| 临时试验 | 直接编译源码 | 简单，但不建议长期维护 |

无论使用哪一种方式，应用代码都统一链接目标 `STTNet::sttnet`。

## 2. 安装系统依赖

Ubuntu/Debian：

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config libjsoncpp-dev libssl-dev
```

Fedora/RHEL：

```bash
sudo dnf install -y gcc-c++ cmake ninja-build pkgconf-pkg-config jsoncpp-devel openssl-devel
```

Arch Linux：

```bash
sudo pacman -S --needed base-devel cmake ninja pkgconf jsoncpp openssl
```

## 3. 方式 A：安装后使用 find_package

构建并安装到用户目录：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSTTNET_BUILD_TESTS=OFF -DSTTNET_BUILD_EXAMPLE=OFF
cmake --build build
cmake --install build --prefix "$HOME/.local"
```

用户项目：

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_server LANGUAGES CXX)

find_package(STTNet 0.7 CONFIG REQUIRED)
add_executable(my_server main.cpp)
target_link_libraries(my_server PRIVATE STTNet::sttnet)
```

若安装前缀不在 CMake 默认搜索路径：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$HOME/.local"
cmake --build build
```

## 4. 方式 B：add_subdirectory

目录结构：

```text
my_server/
├── CMakeLists.txt
├── main.cpp
└── third_party/STTNet/
```

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_server LANGUAGES CXX)

set(STTNET_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(STTNET_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/STTNet)

add_executable(my_server main.cpp)
target_link_libraries(my_server PRIVATE STTNet::sttnet)
```

当 STTNet 作为子项目时，示例、测试和安装规则默认关闭；上面的显式设置便于老版本 CMake 和团队成员理解。若宿主项目确实需要一起安装 STTNet，可显式设置 `STTNET_ENABLE_INSTALL=ON`。

## 5. 方式 C：FetchContent

正式项目必须固定发布 tag 或完整 commit，不要长期跟踪移动分支：

```cmake
include(FetchContent)
set(STTNET_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(STTNET_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  sttnet_source
  GIT_REPOSITORY https://github.com/StephenTaam/STTNet.git
  GIT_TAG        v0.7.0 # 发布 tag 创建后使用；也可以填完整 commit SHA
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(sttnet_source)

add_executable(my_server main.cpp)
target_link_libraries(my_server PRIVATE STTNet::sttnet)
```

## 6. 方式 D：pkg-config

安装后可用于 Makefile 或其他构建系统：

```bash
export PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig:$PKG_CONFIG_PATH"
c++ -std=c++17 main.cpp $(pkg-config --cflags --libs sttnet) -o my_server
```

## 7. 五分钟 HTTP 服务

完整源码见 [`examples/http_hello.cpp`](../examples/http_hello.cpp)：

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

启动后：

```bash
curl -i http://127.0.0.1:8080/ping
kill -15 <pid>
```

## 8. 常用 HTTP API

请求对象：

```cpp
const std::string_view contentType=request.headerValue("content-type");
const std::string_view body=request.bodyView(); // 自动覆盖 Content-Length/chunked
```

响应对象：

```cpp
client.sendText("created", "201 Created");

Json::Value value;
value["ok"]=true;
client.sendJson(value);

client.redirect("/login");
```

`sendBack/sendText/sendJson/sendMessage` 成功表示数据已进入该连接的有界发送队列，不表示对端已经收到。返回失败或 `sendData` 返回 `-101` 时，应停止继续生成数据；框架会淘汰超过发送高水位的慢客户端。

## 9. 回调返回值与 WorkerPool

HTTP/TCP/WebSocket 路由回调遵循：

| 返回值 | 含义 |
|---|---|
| `1` | 当前处理成功 |
| `0` | 已投递 Worker，等待异步完成 |
| `-1` | 处理失败，但暂不要求关闭连接 |
| `-2` | 处理失败并关闭连接 |

阻塞数据库、磁盘或外部 RPC 应放入 WorkerPool：

```cpp
server.setFunction("/slow",[&server](auto &client,auto &request) {
    server.putTask([](auto &workerClient,auto &) {
        return workerClient.sendText("done") ? 1 : -2;
    },client,request);
    return 0;
});
```

不要在 Worker 中保存回调引用到任务返回以后；框架会复制 handler 和请求，并用连接代次避免 fd 复用串线。

## 10. WebSocket Echo

完整源码见 [`examples/websocket_echo.cpp`](../examples/websocket_echo.cpp)。核心只有一个处理器：

```cpp
WebSocketServer server;
server.setGlobalSolveFunction([](WebSocketServerFDHandler &client,
                                 WebSocketFDInformation &message) {
    return client.sendMessage(message.message);
});
```

框架会处理握手、mask、分片、ping/pong、close 与 UTF-8 校验。

## 11. 生产配置清单

监听前完成配置：

```cpp
ServerSocketOptions options;
options.tcp_no_delay=true;
options.keep_alive=true;
options.listen_backlog=4096;
server.setSocketOptions(options);
server.setMaxPendingWriteBytes(4 * 1024 * 1024);
server.setWriteBudgetPerEvent(256 * 1024);
server.setMaxPendingWorkerTasks(65536);
server.setGracefulShutdownTimeout(5000);
```

- 在创建任何线程前调用 `blockTerminationSignals()`。
- 在主线程调用 `waitForTerminationSignal()`；不要从异步 signal handler 删除服务对象。
- `kill -15`/Ctrl-C 可以优雅退出；`kill -9` 无法捕获，只能依赖操作系统回收资源。
- HTTPS/WSS 使用 `setTLS(cert, key, password)`；需要 mTLS 时再显式配置 CA 和客户端证书模式。
- 根据真实 Linux 机器的 p99、RSS、队列峰值和慢客户端压测调整参数。

## 12. 下一步

- API 手册：`docs/api/html_Chinese/index.html`
- 信号与退出：[`SIGNALS_Chinese.md`](SIGNALS_Chinese.md)
- 性能与边界：[`CAPABILITY_Chinese.md`](CAPABILITY_Chinese.md)
- 功能路线图：[`ROADMAP_Chinese.md`](ROADMAP_Chinese.md)
