#include <sttnet.h>

#include <csignal>
#include <iostream>

int main()
{
    using namespace stt::network;
    using stt::system::ServerSetting;

    // Configure STTNet's safe process-level signal defaults.
    // In particular, SIGPIPE is ignored so a closed socket cannot kill the process.
    ServerSetting::setExceptionHandling();

    // Block graceful-termination signals before any framework threads are created.
    // The same signal mask is inherited by the Reactor and Worker threads.
    if(!ServerSetting::blockTerminationSignals())
    {
        std::cerr << "failed to block SIGINT/SIGTERM\n";
        return 1;
    }

    HttpServer server;
    server.setFunction("/ping",
        [](HttpServerFDHandler &client, HttpRequestInformation &) {
            return client.sendText("pong") ? 1 : -2;
        });

    // Allow up to five seconds for in-flight work and queued responses to drain.
    server.setGracefulShutdownTimeout(5000);

    if(!server.startListen(8083))
    {
        std::cerr << "failed to listen on port 8083\n";
        return 2;
    }

    std::cout << "Signal demo listening on http://127.0.0.1:8083\n"
              << "Press Ctrl-C or run: kill -15 " << ::getpid() << "\n";

    // sigwait-style synchronous waiting keeps cleanup out of an async signal handler.
    const int receivedSignal = ServerSetting::waitForTerminationSignal();
    if(receivedSignal < 0)
    {
        std::cerr << "failed while waiting for termination signal\n";
        return 3;
    }

    std::cout << "received "
              << (receivedSignal == SIGINT ? "SIGINT" : "SIGTERM")
              << ", starting graceful shutdown\n";

    // Never delete or close the server inside an asynchronous signal handler.
    return server.close() ? 0 : 4;
}
