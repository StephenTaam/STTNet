#include <sttnet.h>

#include <iostream>

int main()
{
    using namespace stt::network;
    using stt::system::ServerSetting;

    // Block SIGINT and SIGTERM before STTNet creates Reactor or Worker threads.
    // The main thread will wait for these signals synchronously near the end.
    if(!ServerSetting::blockTerminationSignals())
    {
        std::cerr << "failed to block termination signals\n";
        return 1;
    }

    // HttpServer owns the listening socket, Reactor, connections, and workers.
    HttpServer server;

    // Register a route. For HttpServer, the default route key is the URL path.
    server.setFunction("/ping",
        [](HttpServerFDHandler &client, HttpRequestInformation &) {
            // Return 1 when the request was handled successfully.
            // -2 reports send failure and requests connection closure.
            return client.sendText("pong") ? 1 : -2;
        });

    // A global handler is used when no registered route matches the request path.
    server.setGlobalSolveFunction(
        [](HttpServerFDHandler &client, HttpRequestInformation &) {
            return client.sendText("not found", "404 Not Found") ? 1 : -2;
        });

    // Start listening on TCP port 8080 with the default worker count.
    if(!server.startListen(8080))
    {
        std::cerr << "failed to listen on port 8080\n";
        return 2;
    }

    std::cout << "STTNet " << stt::version
              << " listening on http://127.0.0.1:8080\n";

    // Wait here until Ctrl-C (SIGINT) or kill -15 (SIGTERM) is received.
    ServerSetting::waitForTerminationSignal();

    // close() stops accepting new connections, drains in-flight work and writes,
    // then releases the network resources from a normal thread context.
    return server.close() ? 0 : 3;
}
