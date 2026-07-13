#include <sttnet.h>

#include <atomic>
#include <iostream>
#include <thread>

int main()
{
    using namespace stt::network;
    using stt::system::ServerSetting;

    if(!ServerSetting::blockTerminationSignals())
        return 1;

    // Blocking UDP socket with a one-second receive timeout. The timeout lets
    // the loop notice the shutdown flag without unsafe asynchronous handlers.
    UdpServer server(7070, false, 1, true);
    if(server.getFD() < 0)
    {
        std::cerr << "failed to bind UDP port 7070\n";
        return 2;
    }

    std::atomic<bool> running{true};
    std::thread signalThread([&running] {
        ServerSetting::waitForTerminationSignal();
        running.store(false, std::memory_order_release);
    });

    std::cout << "UDP echo listening on 127.0.0.1:7070\n";

    while(running.load(std::memory_order_acquire))
    {
        std::string data;
        std::string peerIP;
        int peerPort = 0;

        const int received = server.recvData(data, 64 * 1024, peerIP, peerPort);
        if(received == -100)
            continue; // Receive timeout: check the shutdown flag again.
        if(received <= 0)
            continue;

        // UDP preserves datagram boundaries. Echo this datagram to its sender.
        const int sent = server.sendData(data, peerIP, peerPort);
        if(sent <= 0)
            std::cerr << "failed to reply to " << peerIP << ':' << peerPort << '\n';
    }

    server.close();
    signalThread.join();
    return 0;
}
