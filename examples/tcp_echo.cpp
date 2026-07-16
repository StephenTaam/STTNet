#include <sttnet.h>

#include <iostream>

class EchoTcpServer final : public stt::network::TcpServer
{
protected:
    // TcpServer leaves heartbeat policy to derived protocols.
    // This byte-echo demo does not need application-level heartbeat.
    void handleHeartbeat() override {}
};

int main()
{
    using namespace stt::network;
    using stt::system::ServerSetting;

    if(!ServerSetting::blockTerminationSignals())
        return 1;

    EchoTcpServer server;

    // Raw TCP is a byte stream: one callback is one received chunk, not
    // necessarily one complete application message. Real protocols add
    // framing such as a fixed header, a length prefix, or a delimiter.
    server.setGlobalSolveFunction(
        [](TcpFDHandler &client, TcpInformation &info) {
            // In a server callback, sendData() submits bytes to the bounded
            // Reactor write queue; it does not block on the peer's network.
            return client.sendData(info.data) > 0;
        });

    if(!server.startListen(6060))
    {
        std::cerr << "failed to listen on port 6060\n";
        return 2;
    }

    std::cout << "TCP echo listening on 127.0.0.1:6060\n";
    ServerSetting::waitForTerminationSignal();
    return server.close() ? 0 : 3;
}
