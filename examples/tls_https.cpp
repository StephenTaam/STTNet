#include <sttnet.h>

#include <iostream>

int main(int argc, char **argv)
{
    using namespace stt::network;
    using stt::system::ServerSetting;

    if(argc != 3)
    {
        std::cerr << "usage: " << argv[0] << " <server.crt> <server.key>\n";
        return 1;
    }

    if(!ServerSetting::blockTerminationSignals())
        return 2;

    HttpServer server;

    // Configure TLS before startListen(). This is ordinary one-way HTTPS:
    // clients verify the server certificate; the server does not require a
    // client certificate. Use the five-argument overload for explicit mTLS.
    if(!server.setTLS(argv[1], argv[2]))
    {
        std::cerr << "failed to load certificate or private key\n";
        return 3;
    }

    server.setFunction("/secure",
        [](HttpServerFDHandler &client, HttpRequestInformation &) {
            return client.sendJson(Json::Value("TLS is active")) ? 1 : -2;
        });

    if(!server.startListen(8443))
    {
        std::cerr << "failed to listen on port 8443\n";
        return 4;
    }

    std::cout << "HTTPS demo listening on https://127.0.0.1:8443/secure\n";
    ServerSetting::waitForTerminationSignal();
    return server.close() ? 0 : 5;
}
