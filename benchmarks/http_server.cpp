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
    return EXIT_SUCCESS;
}
