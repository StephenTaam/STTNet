#include <sttnet.h>

#include <iostream>
#include <string>

int main()
{
    using stt::data::HttpStringUtil;
    using stt::data::WebsocketStringUtil;

    const std::string url="http://127.0.0.1:8080/users?userid=7&id=42&page=2";
    std::string ip;
    int port=0;
    std::string locationAndQuery;
    std::string path;
    std::string query;
    std::string id;

    // These are lightweight extraction helpers. They do not URL-decode percent
    // escapes and are not a standards-complete URI parser for hostile input.
    HttpStringUtil::getIP(url,ip);
    HttpStringUtil::getPort(url,port);
    HttpStringUtil::getLocPara(url,locationAndQuery);
    HttpStringUtil::get_location_str(locationAndQuery,path);
    HttpStringUtil::getPara(locationAndQuery,query);

    // Query keys are matched on field boundaries: "id" does not match "userid".
    HttpStringUtil::get_value_str(locationAndQuery,id,"id");
    std::cout << "ip=" << ip << '\n'
              << "port=" << port << '\n'
              << "location=" << locationAndQuery << '\n'
              << "path=" << path << '\n'
              << "query=" << query << '\n'
              << "id=" << id << '\n';

    const std::string rawHeaders=
        "content-type:\tapplication/json\r\n"
        "Connection: keep-alive\r\n";
    std::string contentType;

    // HTTP field names are case-insensitive. The helper trims optional spaces and
    // tabs after the colon. Server handlers should normally use headerValue().
    HttpStringUtil::get_value_header(rawHeaders,contentType,"Content-Type");
    std::cout << "content-type=" << contentType << '\n';

    // This computes RFC 6455 Sec-WebSocket-Accept. Applications normally let the
    // WebSocket server perform the handshake automatically.
    std::string websocketKey="dGhlIHNhbXBsZSBub25jZQ==";
    WebsocketStringUtil::transfer_websocket_key(websocketKey);
    std::cout << "websocket_accept=" << websocketKey << '\n';
    return 0;
}
