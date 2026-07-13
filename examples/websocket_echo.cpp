#include <sttnet.h>

#include <iostream>
#include <string_view>

int main()
{
    using namespace stt::network;
    using namespace stt::data;
    using stt::system::ServerSetting;

    if(!ServerSetting::blockTerminationSignals())
    {
        std::cerr << "failed to block termination signals\n";
        return 1;
    }

    WebSocketServer server;

    // Check the HTTP upgrade request before accepting the WebSocket handshake.
    // This demo accepts only /echo and an optional query string.
    server.setJudgeFunction(
        [](WebSocketFDInformation &connection) {
            const bool correctPath =
                connection.locPara == "/echo" ||
                connection.locPara.rfind("/echo?", 0) == 0;

            // A real service may also inspect connection.header here, for
            // example with HttpStringUtil::get_value_header().
            return correctPath;
        });

    // The global callback receives messages that do not use a custom key route.
    // It returns bool: false tells STTNet that message handling failed.
    server.setGlobalSolveFunction(
        [](WebSocketServerFDHandler &client,
           WebSocketFDInformation &message) {
            // RFC 6455 opcode 1 is text and opcode 2 is binary.
            const std::string frameType =
                message.message_type == 2 ? "0010" : "0001";

            // Echo the payload using the same text/binary frame type.
            return client.sendMessage(message.message, frameType);
        });

    // Close idle connections and require a heartbeat response within 30 seconds.
    server.setTimeOutTime(20);   // Idle timeout in minutes.
    server.setHBTimeOutTime(30); // Heartbeat response timeout in seconds.

    if(!server.startListen(5050))
    {
        std::cerr << "failed to listen on port 5050\n";
        return 2;
    }

    std::cout << "WebSocket echo listening on ws://127.0.0.1:5050/echo\n";
    ServerSetting::waitForTerminationSignal();
    return server.close() ? 0 : 3;
}
