#include <sttnet.h>

#include <iostream>
#include <memory>
#include <string>

int main()
{
    using namespace stt::network;
    using stt::system::ServerSetting;

    // Block SIGINT/SIGTERM before STTNet creates Reactor or Worker threads.
    // This lets the main thread perform cleanup in normal C++ context later.
    if(!ServerSetting::blockTerminationSignals())
    {
        std::cerr << "failed to block termination signals\n";
        return 1;
    }

    HttpServer server;

    // GET /inspect shows how STTNet exposes the parsed HTTP request.
    server.setFunction("/inspect",
        [](HttpServerFDHandler &client, HttpRequestInformation &request) {
            // headerValue() is case-insensitive and returns a non-owning view.
            // Copy it before keeping the value beyond this callback.
            const std::string contentType(request.headerValue("content-type"));

            // bodyView() works for both Content-Length and chunked request bodies.
            const std::string body(request.bodyView());

            Json::Value response;
            response["ok"] = true;
            response["method"] = request.type;
            response["path"] = request.loc;
            response["query"] = request.para;
            response["content_type"] = contentType;
            response["body"] = body;

            // A successful send means the response was accepted by STTNet's
            // bounded write queue. -2 asks STTNet to close on send failure.
            return client.sendJson(response) ? 1 : -2;
        });

    // POST /json parses a client-supplied JSON body and validates its fields.
    server.setFunction("/json",
        [](HttpServerFDHandler &client, HttpRequestInformation &request) {
            if(request.type != "POST")
            {
                Json::Value error;
                error["ok"] = false;
                error["error"] = "POST required";
                return client.sendJson(error, "405 Method Not Allowed",
                    "Allow: POST\r\n") ? 1 : -2;
            }

            // JsonCpp's CharReader reports syntax errors without throwing.
            const std::string body(request.bodyView());
            Json::CharReaderBuilder builder;
            std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
            Json::Value input;
            std::string parseError;

            if(!reader->parse(body.data(), body.data() + body.size(),
                              &input, &parseError))
            {
                Json::Value error;
                error["ok"] = false;
                error["error"] = "invalid JSON";
                error["detail"] = parseError;
                return client.sendJson(error, "400 Bad Request") ? 1 : -2;
            }

            if(!input.isMember("name") || !input["name"].isString() ||
               input["name"].asString().empty())
            {
                Json::Value error;
                error["ok"] = false;
                error["error"] = "field 'name' must be a non-empty string";
                return client.sendJson(error, "422 Unprocessable Entity") ? 1 : -2;
            }

            Json::Value response;
            response["ok"] = true;
            response["message"] = "hello, " + input["name"].asString();
            return client.sendJson(response, "201 Created") ? 1 : -2;
        });

    // The global callback is the fallback for paths without a registered route.
    server.setGlobalSolveFunction(
        [](HttpServerFDHandler &client, HttpRequestInformation &) {
            Json::Value error;
            error["ok"] = false;
            error["error"] = "route not found";
            return client.sendJson(error, "404 Not Found") ? 1 : -2;
        });

    if(!server.startListen(8081))
    {
        std::cerr << "failed to listen on port 8081\n";
        return 2;
    }

    std::cout << "HTTP/JSON demo listening on http://127.0.0.1:8081\n";
    ServerSetting::waitForTerminationSignal();
    return server.close() ? 0 : 3;
}
