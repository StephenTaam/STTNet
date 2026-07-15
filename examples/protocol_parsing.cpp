#include <sttnet.h>

#include <iostream>
#include <string>

int main()
{
    using stt::data::HttpStringUtil;
    using stt::data::WebsocketStringUtil;

    const std::string url = "http://127.0.0.1:8080/users?page=2&limit=20";

    std::string ip;
    int port = 0;
    std::string locationAndQuery;
    std::string path;
    std::string query;
    std::string page;

    // These helpers perform lightweight string extraction. They do not URL-decode
    // percent escapes and should not replace a full URI parser for untrusted input.
    HttpStringUtil::getIP(url, ip);
    HttpStringUtil::getPort(url, port);
    HttpStringUtil::getLocPara(url, locationAndQuery);
    HttpStringUtil::get_location_str(locationAndQuery, path);
    HttpStringUtil::getPara(locationAndQuery, query);
    HttpStringUtil::get_value_str(locationAndQuery, page, "page");

    std::cout << "ip=" << ip << '\n'
              << "port=" << port << '\n'
              << "location=" << locationAndQuery << '\n'
              << "path=" << path << '\n'
              << "query=" << query << '\n'
              << "page=" << page << '\n';

    const std::string rawHeaders =
        "Host: example.com\r\n"
        "Connection: keep-alive\r\n";
    std::string host;

    // get_value_header() is a low-level, case-sensitive helper. HTTP server users
    // should normally prefer HttpRequestInformation::headerValue().
    HttpStringUtil::get_value_header(rawHeaders, host, "Host");
    std::cout << "host=" << host << '\n';

    // WebsocketStringUtil computes the RFC 6455 Sec-WebSocket-Accept value from
    // a client key. Applications normally let WebSocketServer handle this step.
    std::string websocketKey = "dGhlIHNhbXBsZSBub25jZQ==";
    WebsocketStringUtil::transfer_websocket_key(websocketKey);
    std::cout << "websocket_accept=" << websocketKey << '\n';

    return 0;
}
