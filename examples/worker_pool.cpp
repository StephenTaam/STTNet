#include <sttnet.h>

#include <chrono>
#include <iostream>
#include <thread>

int main()
{
    using namespace stt::network;
    using stt::system::ServerSetting;

    if(!ServerSetting::blockTerminationSignals())
    {
        std::cerr << "failed to block termination signals\n";
        return 1;
    }

    HttpServer server;

    // Bound the number of waiting Worker tasks. When this limit is reached,
    // STTNet rejects new tasks instead of allowing unbounded memory growth.
    server.setMaxPendingWorkerTasks(4096);

    server.setFunction("/slow",
        [&server](HttpServerFDHandler &client,
                  HttpRequestInformation &request) {
            // Reactor callbacks must return quickly. Put database calls,
            // filesystem access, RPC, sleeping, and unpredictable lock waits
            // in the WorkerPool instead of blocking the network event loop.
            server.putTask(
                [](HttpServerFDHandler &workerClient,
                   HttpRequestInformation &workerRequest) {
                    // client/request are safe snapshots supplied by STTNet.
                    // Do not capture references to the outer callback here.
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));

                    const std::string result =
                        "worker finished for " + workerRequest.loc;

                    // In a server Worker, sendText() enqueues the response for
                    // the Reactor; the Worker never writes socket/TLS directly.
                    return workerClient.sendText(result) ? 1 : -2;
                },
                client,
                request);

            // 0 means: this processing stage continues in the WorkerPool.
            // When the task returns 1, STTNet resumes the next registered stage.
            return 0;
        });

    // The second argument is the WorkerPool thread count, not Reactor count.
    // One server instance uses one Reactor thread to drive network events.
    if(!server.startListen(8082, 4))
    {
        std::cerr << "failed to listen on port 8082\n";
        return 2;
    }

    std::cout << "WorkerPool demo listening on http://127.0.0.1:8082\n";
    ServerSetting::waitForTerminationSignal();
    return server.close() ? 0 : 3;
}
