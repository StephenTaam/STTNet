#include "sttnet.h"
#include "test_assert.h"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

uint16_t boundPort(const int fd)
{
    sockaddr_in address{};
    socklen_t length=sizeof(address);
    STTNET_CHECK(::getsockname(fd,reinterpret_cast<sockaddr*>(&address),&length)==0);
    return ntohs(address.sin_port);
}

void testUdpReceiveReturnsHostOrderPort()
{
    stt::network::UdpServer server(0,false,2,true);
    stt::network::UdpClient client(false,2);
    STTNET_CHECK(server.getFD()>=0);
    STTNET_CHECK(client.getFD()>=0);

    const uint16_t serverPort=boundPort(server.getFD());
    STTNET_CHECK(client.sendData("udp-port-check","127.0.0.1",serverPort)>0);
    const uint16_t clientPort=boundPort(client.getFD());

    std::string payload;
    std::string peerIP;
    int peerPort=0;
    STTNET_CHECK(server.recvData(payload,1024,peerIP,peerPort)>0);
    STTNET_CHECK(payload=="udp-port-check");
    STTNET_CHECK(peerIP=="127.0.0.1");
    STTNET_CHECK(peerPort==clientPort);

    STTNET_CHECK(server.sendData(payload,peerIP,peerPort)>0);
    std::string echoed;
    std::string serverIP;
    int echoedFromPort=0;
    STTNET_CHECK(client.recvData(echoed,1024,serverIP,echoedFromPort)>0);
    STTNET_CHECK(echoed==payload);
    STTNET_CHECK(echoedFromPort==serverPort);
}

std::string readHttpHeader(const int fd)
{
    std::string header;
    char buffer[1024];
    while(header.find("\r\n\r\n")==std::string::npos)
    {
        const ssize_t received=::recv(fd,buffer,sizeof(buffer),0);
        STTNET_CHECK(received>0);
        header.append(buffer,static_cast<size_t>(received));
    }
    return header;
}

std::string requestHeaderValue(const std::string &header,const std::string &name)
{
    const std::string marker=name+":";
    const size_t begin=header.find(marker);
    STTNET_CHECK(begin!=std::string::npos);
    size_t valueBegin=begin+marker.size();
    while(valueBegin<header.size()&&(header[valueBegin]==' '||header[valueBegin]=='\t'))
        ++valueBegin;
    const size_t valueEnd=header.find("\r\n",valueBegin);
    STTNET_CHECK(valueEnd!=std::string::npos);
    return header.substr(valueBegin,valueEnd-valueBegin);
}

void testWebSocketClientHandshakeInteroperability()
{
    const int listener=::socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);
    STTNET_CHECK(listener>=0);
    int reuse=1;
    STTNET_CHECK(::setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse))==0);
    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    address.sin_port=0;
    STTNET_CHECK(::bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
    STTNET_CHECK(::listen(listener,1)==0);
    const uint16_t port=boundPort(listener);

    std::atomic<bool> responseSent{false};
    std::atomic<bool> clientConnected{false};
    std::thread server([&] {
        const int peer=::accept4(listener,nullptr,nullptr,SOCK_CLOEXEC);
        STTNET_CHECK(peer>=0);
        const std::string request=readHttpHeader(peer);
        const std::string key=requestHeaderValue(request,"Sec-WebSocket-Key");
        STTNET_CHECK(stt::data::EncodingUtil::base64_decode(key).size()==16);

        std::string accept=key;
        stt::data::WebsocketStringUtil::transfer_websocket_key(accept);
        const std::string response=
            "HTTP/1.1 101 Switching Protocols\r\n"
            "upgrade:WebSocket\r\n"
            "connection:keep-alive, UpGrAdE\r\n"
            "sec-websocket-accept:"+accept+"\r\n\r\n";
        STTNET_CHECK(::send(peer,response.data(),response.size(),MSG_NOSIGNAL)==
                     static_cast<ssize_t>(response.size()));
        responseSent.store(true,std::memory_order_release);
        while(!clientConnected.load(std::memory_order_acquire))
            std::this_thread::yield();

        const unsigned char closeFrame[]={0x88,0x02,0x03,0xE8};
        STTNET_CHECK(::send(peer,closeFrame,sizeof(closeFrame),MSG_NOSIGNAL)==
                     static_cast<ssize_t>(sizeof(closeFrame)));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ::close(peer);
    });

    stt::network::WebSocketClient client;
    STTNET_CHECK(client.connect("ws://127.0.0.1:"+std::to_string(port)+"/interop",1));
    STTNET_CHECK(responseSent.load(std::memory_order_acquire));
    clientConnected.store(true,std::memory_order_release);
    server.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ::close(listener);
}


void testWebSocketClientRejectsInvalidAccept()
{
    const int listener=::socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);
    STTNET_CHECK(listener>=0);
    int reuse=1;
    STTNET_CHECK(::setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse))==0);
    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    address.sin_port=0;
    STTNET_CHECK(::bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
    STTNET_CHECK(::listen(listener,1)==0);
    const uint16_t port=boundPort(listener);

    std::thread server([&] {
        const int peer=::accept4(listener,nullptr,nullptr,SOCK_CLOEXEC);
        STTNET_CHECK(peer>=0);
        (void)readHttpHeader(peer);
        const std::string response=
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: invalid\r\n\r\n";
        STTNET_CHECK(::send(peer,response.data(),response.size(),MSG_NOSIGNAL)==
                     static_cast<ssize_t>(response.size()));
        ::close(peer);
    });

    stt::network::WebSocketClient client;
    STTNET_CHECK(!client.connect("ws://127.0.0.1:"+std::to_string(port)+"/invalid",1));
    STTNET_CHECK(!client.isConnect());
    server.join();
    ::close(listener);
}

} // namespace

int main()
{
    testUdpReceiveReturnsHostOrderPort();
    testWebSocketClientHandshakeInteroperability();
    testWebSocketClientRejectsInvalidAccept();
    return 0;
}
