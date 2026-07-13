#include "sttnet.h"

#include <cassert>
#include <cstring>
#include <iostream>

namespace {

void setInput(stt::network::TcpFDInf &connection,const std::string &input,const size_t capacity=4096)
{
    delete[] connection.buffer;
    connection.buffer_capacity=std::max(capacity,input.size());
    connection.buffer=new char[connection.buffer_capacity];
    std::memcpy(connection.buffer,input.data(),input.size());
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
    stt::network::TcpFDInf connection;
    stt::network::HttpRequestInformation request;
    setInput(connection,"POST /submit?q=1 HTTP/1.1\r\nhOsT: example.test\r\ncOnTeNt-LeNgTh: 4\r\n\r\ndata");
    assert(parse(connection,request)==1);
    assert(request.type=="POST");
    assert(request.loc=="/submit");
    assert(request.para=="?q=1");
    assert(request.body=="data");
    assert(connection.p_buffer_now==0);
}

void testRequestSmugglingInputsAreRejected()
{
    stt::network::TcpFDInf connection;
    stt::network::HttpRequestInformation request;
    setInput(connection,"POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 4\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
    assert(parse(connection,request)==-1);

    setInput(connection,"POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 4\r\nContent-Length: 4\r\n\r\ndata");
    assert(parse(connection,request)==-1);

    setInput(connection,"GET / HTTP/1.1\r\nUser-Agent: test\r\n\r\n");
    assert(parse(connection,request)==-1);

    setInput(connection,"POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: gzip, chunked\r\n\r\n0\r\n\r\n");
    assert(parse(connection,request)==-1);
}

void testChunkedBodyAndTrailers()
{
    stt::network::TcpFDInf connection;
    stt::network::HttpRequestInformation request;
    setInput(connection,
             "POST /chunk HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n"
             "4;extension=yes\r\nWiki\r\n5\r\npedia\r\n0\r\nX-Trace: ok\r\n\r\n");
    assert(parse(connection,request)==1);
    assert(request.body.empty());
    assert(request.body_chunked=="Wikipedia");
}

void testPipelinedRequestsRemainParseable()
{
    stt::network::TcpFDInf connection;
    stt::network::HttpRequestInformation first;
    stt::network::HttpRequestInformation second;
    const std::string secondWire="GET /two HTTP/1.1\r\nHost: x\r\n\r\n";
    setInput(connection,"GET /one HTTP/1.1\r\nHost: x\r\n\r\n"+secondWire);
    assert(parse(connection,first)==1);
    assert(first.loc=="/one");
    assert(connection.p_buffer_now==secondWire.size());
    assert(parse(connection,second)==1);
    assert(second.loc=="/two");
    assert(connection.p_buffer_now==0);
}

void testHeaderAndBodyLimits()
{
    stt::network::TcpFDInf connection;
    stt::network::HttpRequestInformation request;
    setInput(connection,"GET / HTTP/1.1\r\nHost: x\r\nX-Large: 12345678901234567890\r\n\r\n");
    assert(parse(connection,request,4096,32)==-1);

    setInput(connection,"POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 4096\r\n\r\n");
    assert(parse(connection,request,4096,1024)==-1);
}

} // namespace

int main()
{
    testContentLengthAndCaseInsensitiveNames();
    testRequestSmugglingInputsAreRejected();
    testChunkedBodyAndTrailers();
    testPipelinedRequestsRemainParseable();
    testHeaderAndBodyLimits();
    std::cout<<"all HTTP parser tests passed\n";
    return 0;
}
