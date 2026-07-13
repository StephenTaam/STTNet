#include <sttnet.h>

#include <chrono>
#include <iostream>
#include <thread>

int main()
{
    using stt::network::WebSocketClient;

    WebSocketClient client;

    // The callback runs on the client's internal listening thread.
    client.setFunction(
        [](const std::string &message, WebSocketClient &) {
            std::cout << "received: " << message << '\n';
            return true; // false closes the WebSocket connection.
        });

    if(!client.connect("ws://127.0.0.1:5050/echo"))
    {
        std::cerr << "WebSocket connection failed\n";
        return 1;
    }

    if(!client.sendMessage("hello from STTNet client"))
    {
        std::cerr << "send failed\n";
        client.close(1011, "send failed");
        return 2;
    }

    // Give the asynchronous receive callback time to print the echo.
    std::this_thread::sleep_for(std::chrono::seconds(1));
    client.close(1000, "demo complete");
    return 0;
}
