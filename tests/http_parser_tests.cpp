#include "sttnet.h"
#include "test_connection.h"

#include "test_assert.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>

namespace {

void setInput(stt::network::TcpFDInf &connection,const std::string &input,const size_t capacity=4096)
{
    const size_t nextCapacity=std::max(capacity,input.size());
    auto nextBuffer=std::make_unique<char[]>(nextCapacity);
    std::memcpy(nextBuffer.get(),input.data(),input.size());
    delete[] connection.buffer;
    connection.buffer=nextBuffer.release();
    connection.buffer_capacity=nextCapacity;
    connection.p_buffer_now=input.size();
    connection.status=0;
}

int parse(stt::network::TcpFDInf &connection,stt::network::HttpRequestInformation &request,
          const size_t bufferLimit=4096,const size_t headerLimit=1024)
{
    stt::network::HttpServerFDHandler handler;
    return handler.solveRequest(connection,request,bufferLimit,0,headerLimit);
}

void testContentLengthAndCaseInsensitiveNames()
{
    stt::test::TcpConnectionFixture connection;
    stt::network::HttpRequestInformation request;
    setInput(connection,"POST /submit?q=1 HTTP/1.1\r\nhOsT: example.test\r\ncOnTeNt-LeNgTh: 4\r\n\r\ndata");
    STTNET_CHECK(parse(connection,request)==1);
    STTNET_CHECK(request.type=="POST");
    STTNET_CHECK(request.loc=="/submit");
    STTNET_CHECK(request.para=="?q=1");
    STTNET_CHECK(request.body=="data");
    STTNET_CHECK(request.bodyView()=="data");
    STTNET_CHECK(request.headerValue("HOST")=="example.test");
    STTNET_CHECK(request.headerValue("content-length")=="4");
    STTNET_CHECK(request.headerValue("missing").empty());
    STTNET_CHECK(connection.p_buffer_now==0);
}

void testRequestSmugglingInputsAreRejected()
{
    stt::test::TcpConnectionFixture connection;
    stt::network::HttpRequestInformation request;
    setInput(connection,"POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 4\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
    STTNET_CHECK(parse(connection,request)==-1);

    setInput(connection,"POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 4\r\nContent-Length: 4\r\n\r\ndata");
    STTNET_CHECK(parse(connection,request)==-1);

    setInput(connection,"GET / HTTP/1.1\r\nUser-Agent: test\r\n\r\n");
    STTNET_CHECK(parse(connection,request)==-1);

    setInput(connection,"POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: gzip, chunked\r\n\r\n0\r\n\r\n");
    STTNET_CHECK(parse(connection,request)==-1);
}

void testChunkedBodyAndTrailers()
{
    stt::test::TcpConnectionFixture connection;
    stt::network::HttpRequestInformation request;
    setInput(connection,
             "POST /chunk HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n"
             "4;extension=yes\r\nWiki\r\n5\r\npedia\r\n0\r\nX-Trace: ok\r\n\r\n");
    STTNET_CHECK(parse(connection,request)==1);
    STTNET_CHECK(request.body.empty());
    STTNET_CHECK(request.body_chunked=="Wikipedia");
    STTNET_CHECK(request.bodyView()=="Wikipedia");

    setInput(connection,
             "POST /chunk HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: ChUnKeD\r\n\r\n"
             "3\r\nabc\r\n0\r\n\r\n");
    STTNET_CHECK(parse(connection,request)==1);
    STTNET_CHECK(request.body_chunked=="abc");
}

void testConvenienceResponses()
{
    stt::network::HttpServerFDHandler handler;
    handler.setFD(42,nullptr,true);
    std::string wire;
    handler.setTransportFunctions([&wire](std::string data) {
        wire=std::move(data);
        return static_cast<int>(wire.size());
    });

    STTNET_CHECK(handler.sendText("hello"));
    STTNET_CHECK(wire.find("HTTP/1.1 200 OK\r\n") == 0);
    STTNET_CHECK(wire.find("Content-Type: text/plain; charset=utf-8\r\n")!=std::string::npos);
    STTNET_CHECK(wire.substr(wire.size()-5)=="hello");

    Json::Value json;
    json["ok"]=true;
    STTNET_CHECK(handler.sendJson(json));
    STTNET_CHECK(wire.find("Content-Type: application/json; charset=utf-8\r\n")!=std::string::npos);
    STTNET_CHECK(wire.find("\"ok\"")!=std::string::npos);

    STTNET_CHECK(handler.redirect("/login"));
    STTNET_CHECK(wire.find("HTTP/1.1 302 Found\r\n") == 0);
    STTNET_CHECK(wire.find("Location: /login\r\n")!=std::string::npos);
    STTNET_CHECK(!handler.redirect("/safe\r\nX-Injected: yes"));
    STTNET_CHECK(!handler.sendText("x","200 OK","text/plain\r\nX-Injected: yes"));
}

void testPipelinedRequestsRemainParseable()
{
    stt::test::TcpConnectionFixture connection;
    stt::network::HttpRequestInformation first;
    stt::network::HttpRequestInformation second;
    const std::string secondWire="GET /two HTTP/1.1\r\nHost: x\r\n\r\n";
    setInput(connection,"GET /one HTTP/1.1\r\nHost: x\r\n\r\n"+secondWire);
    STTNET_CHECK(parse(connection,first)==1);
    STTNET_CHECK(first.loc=="/one");
    STTNET_CHECK(connection.p_buffer_now==secondWire.size());
    STTNET_CHECK(parse(connection,second)==1);
    STTNET_CHECK(second.loc=="/two");
    STTNET_CHECK(connection.p_buffer_now==0);
}

void testHeaderAndBodyLimits()
{
    stt::test::TcpConnectionFixture connection;
    stt::network::HttpRequestInformation request;
    setInput(connection,"GET / HTTP/1.1\r\nHost: x\r\nX-Large: 12345678901234567890\r\n\r\n");
    STTNET_CHECK(parse(connection,request,4096,32)==-1);

    setInput(connection,"POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 4096\r\n\r\n");
    STTNET_CHECK(parse(connection,request,4096,1024)==-1);
}

} // namespace

int main()
{
    testContentLengthAndCaseInsensitiveNames();
    testRequestSmugglingInputsAreRejected();
    testChunkedBodyAndTrailers();
    testPipelinedRequestsRemainParseable();
    testHeaderAndBodyLimits();
    testConvenienceResponses();
    std::cout<<"all HTTP parser tests passed\n";
    return 0;
}
