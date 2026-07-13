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

    HttpServer server;
    server.setFunction("/ping",[](HttpServerFDHandler &client,HttpRequestInformation &) {
        return client.sendText("pong")?1:-2;
    });
    server.setFunction("/health",[](HttpServerFDHandler &client,HttpRequestInformation &request) {
        Json::Value response;
        response["status"]="ok";
        response["version"]=std::string(stt::version);
        response["user_agent"]=std::string(request.headerValue("user-agent"));
        return client.sendJson(response)?1:-2;
    });
    server.setGlobalSolveFunction([](HttpServerFDHandler &client,HttpRequestInformation &) {
        return client.sendText("not found","404 Not Found")?1:-2;
    });

    if(!server.startListen(8080))
    {
        std::cerr<<"failed to listen on port 8080\n";
        return 2;
    }
    std::cout<<"STTNet "<<stt::version<<" listening on http://127.0.0.1:8080\n";
    ServerSetting::waitForTerminationSignal();
    return server.close()?0:3;
}
