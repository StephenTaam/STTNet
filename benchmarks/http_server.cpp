#include "sttnet.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <thread>

int main()
{
    if(!stt::system::ServerSetting::blockTerminationSignals())
        return EXIT_FAILURE;

    // Security and application logging are intentionally disabled so this
    // executable measures the network/HTTP hot path rather than rate limiting.
    stt::network::HttpServer server(1'000'000,64,65'536,false);
    stt::network::ServerSocketOptions socketOptions;
    socketOptions.tcp_no_delay=true;
    socketOptions.keep_alive=true;
    socketOptions.keep_alive_idle_seconds=60;
    socketOptions.keep_alive_interval_seconds=10;
    socketOptions.keep_alive_probe_count=3;
    socketOptions.reuse_port=false;
    socketOptions.defer_accept_seconds=1;
    socketOptions.listen_backlog=4096;
    server.setSocketOptions(socketOptions);
    server.setMaxPendingWorkerTasks(65'536);
    const std::string largePayload(1024UL*1024UL,'x');
    server.setFunction("/ping",[](stt::network::HttpServerFDHandler &handler,
                                  stt::network::HttpRequestInformation &) {
        return handler.sendBack("pong")?1:-2;
    });
    server.setFunction("/large",[&largePayload](stt::network::HttpServerFDHandler &handler,
                                                stt::network::HttpRequestInformation &) {
        return handler.sendBack(largePayload)?1:-2;
    });

    const unsigned int workers=std::max(1u,std::thread::hardware_concurrency());
    if(!server.startListen(8080,static_cast<int>(workers)))
    {
        std::cerr<<"failed to listen on port 8080\n";
        return EXIT_FAILURE;
    }

    stt::system::ServerSetting::waitForTerminationSignal();
    server.close();
    const auto metrics=server.getMetrics();
    std::cout<<"accepted="<<metrics.accepted_connections
             <<" rejected="<<metrics.rejected_connections
             <<" requests="<<metrics.parsed_http_requests
             <<" sent_bytes="<<metrics.sent_bytes
             <<" pending_write_peak="<<metrics.peak_pending_write_bytes
             <<" pending_worker_peak="<<metrics.peak_pending_worker_tasks
             <<" write_syscalls="<<metrics.write_syscalls
             <<" batched_writes="<<metrics.batched_write_syscalls
             <<" reactor_wakeups="<<metrics.reactor_wakeups
             <<" coalesced_wakeups="<<metrics.reactor_wakeups_coalesced
             <<" worker_rejections="<<metrics.worker_task_rejections<<'\n';
    return EXIT_SUCCESS;
}
