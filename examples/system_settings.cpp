#include <sttnet.h>

#include <iostream>

int main()
{
    using namespace stt::network;
    using stt::system::ServerSetting;

    // Signal blocking must happen before the logger, Reactor, or Worker threads exist.
    if(!ServerSetting::blockTerminationSignals())
    {
        std::cerr << "failed to block termination signals\n";
        return 1;
    }

    // Create an asynchronous logger with a 16K-entry queue.
    // openFile() creates missing directories when possible.
    stt::file::LogFile log(16384);
    if(!log.openFile("logs/sttnet.log"))
    {
        std::cerr << "failed to open logs/sttnet.log\n";
        return 2;
    }

    // init() configures safe signal defaults and connects the logger to STTNet.
    // Use "Chinese" for Chinese framework log messages; other values use English.
    ServerSetting::init(&log, "English");

    // Constructor settings define capacity and built-in protection defaults.
    HttpServer server(
        100000, // Maximum simultaneously tracked connections.
        64,     // Maximum receive buffer per connection, in KiB.
        65536,  // Worker-completion queue capacity; use a power of two.
        true    // Enable the built-in connection/request security limiter.
    );

    ServerSocketOptions socketOptions;
    socketOptions.tcp_no_delay = true;          // Reduce latency for small responses.
    socketOptions.keep_alive = true;            // Enable kernel TCP keepalive.
    socketOptions.keep_alive_idle_seconds = 60; // Probe after 60 seconds of idleness.
    socketOptions.keep_alive_interval_seconds = 10;
    socketOptions.keep_alive_probe_count = 3;
    socketOptions.listen_backlog = 4096;
    server.setSocketOptions(socketOptions);

    // Bound memory used by slow clients and prevent one connection from monopolizing I/O.
    server.setMaxPendingWriteBytes(4 * 1024 * 1024);
    server.setWriteBudgetPerEvent(256 * 1024);

    // Bound request/header and WorkerPool pressure.
    server.setMaxHttpHeaderBytes(64 * 1024);
    server.setMaxPendingWorkerTasks(32768);

    // Allow five seconds for graceful network draining during close().
    server.setGracefulShutdownTimeout(5000);

    server.setFunction("/metrics",
        [&server](HttpServerFDHandler &client, HttpRequestInformation &) {
            // Metrics are lock-free snapshots and may be read from any thread.
            const ServerMetricsSnapshot metrics = server.getMetrics();

            Json::Value response;
            response["active_connections"] = Json::UInt64(metrics.active_connections);
            response["parsed_http_requests"] = Json::UInt64(metrics.parsed_http_requests);
            response["pending_write_bytes"] = Json::UInt64(metrics.pending_write_bytes);
            response["worker_task_rejections"] = Json::UInt64(metrics.worker_task_rejections);
            return client.sendJson(response) ? 1 : -2;
        });

    if(!server.startListen(8084, 8))
    {
        std::cerr << "failed to listen on port 8084\n";
        return 3;
    }

    std::cout << "Configured server listening on http://127.0.0.1:8084\n";
    ServerSetting::waitForTerminationSignal();

    // The LogFile object remains alive until after the server finishes closing.
    return server.close() ? 0 : 4;
}
