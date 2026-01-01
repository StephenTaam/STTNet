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
