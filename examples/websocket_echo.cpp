#include <sttnet.h>

#include <iostream>

int main()
{
    using namespace stt::network;
    using stt::system::ServerSetting;

    if(!ServerSetting::blockTerminationSignals())
    {
        std::cerr<<"failed to block termination signals\n";
        return 1;
    }

    WebSocketServer server;
    server.setGlobalSolveFunction([](WebSocketServerFDHandler &client,
                                     WebSocketFDInformation &message) {
        return client.sendMessage(message.message);
    });
    if(!server.startListen(5050))
    {
        std::cerr<<"failed to listen on port 5050\n";
        return 2;
    }
    std::cout<<"WebSocket echo listening on ws://127.0.0.1:5050\n";
    ServerSetting::waitForTerminationSignal();
    return server.close()?0:3;
}
