#include "sttnet.h"
#include "test_connection.h"

#include "test_assert.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>

namespace {

std::string clientFrame(const uint8_t opcode,const bool fin,const std::string &payload)
{
    STTNET_CHECK(payload.size()<=125);
    const unsigned char mask[4]={0x11,0x22,0x33,0x44};
    std::string frame;
    frame.push_back(static_cast<char>((fin?0x80U:0U)|opcode));
    frame.push_back(static_cast<char>(0x80U|payload.size()));
    frame.append(reinterpret_cast<const char*>(mask),4);
    for(size_t index=0;index<payload.size();++index)
        frame.push_back(static_cast<char>(static_cast<unsigned char>(payload[index])^mask[index%4]));
    return frame;
}

void setInput(stt::network::TcpFDInf &connection,const std::string &wire)
{
    const size_t nextCapacity=std::max<size_t>(wire.size(),256);
    auto nextBuffer=std::make_unique<char[]>(nextCapacity);
    std::memcpy(nextBuffer.get(),wire.data(),wire.size());
    delete[] connection.buffer;
    connection.buffer=nextBuffer.release();
    connection.buffer_capacity=nextCapacity;
    connection.p_buffer_now=wire.size();
}

void testServerFrameEncoding()
{
    stt::network::WebSocketServerFDHandler handler;
    handler.setFD(42,nullptr,true);
    std::string wire;
    handler.setTransportFunctions([&wire](std::string data) {
        wire=std::move(data);
        return static_cast<int>(wire.size());
    });
    STTNET_CHECK(handler.sendMessage("hello","0001"));
    STTNET_CHECK(wire.size()==7);
    STTNET_CHECK(static_cast<unsigned char>(wire[0])==0x81);
    STTNET_CHECK(static_cast<unsigned char>(wire[1])==5);
    STTNET_CHECK(wire.substr(2)=="hello");
    STTNET_CHECK(!handler.sendMessage(std::string(126,'x'),"1001"));
    STTNET_CHECK(!handler.sendMessage("x","invalid"));
    STTNET_CHECK(!handler.sendMessage(std::string("\xC0\xAF",2),"0001"));
    STTNET_CHECK(!handler.sendMessage(std::string{static_cast<char>(0x07),static_cast<char>(0xD0)},"1000"));
}

void testMaskedFrameAndProtocolValidation()
{
    stt::test::TcpConnectionFixture connection;
    stt::network::WebSocketFDInformation information{};
    stt::network::WebSocketServerFDHandler handler;
    setInput(connection,clientFrame(0x1,true,"hello"));
    STTNET_CHECK(handler.getMessage(connection,information,4096,0)==0);
    STTNET_CHECK(information.message=="hello");

    setInput(connection,std::string("\x81\x01x",3));
    STTNET_CHECK(handler.getMessage(connection,information,4096,0)==-1);

    setInput(connection,clientFrame(0x9,false,"bad"));
    STTNET_CHECK(handler.getMessage(connection,information,4096,0)==-1);
}

void testFragmentationAndInterleavedControlFrame()
{
    stt::test::TcpConnectionFixture connection;
    stt::network::WebSocketFDInformation information{};
    stt::network::WebSocketServerFDHandler handler;
    setInput(connection,clientFrame(0x1,false,"hel")+clientFrame(0x0,true,"lo"));
    STTNET_CHECK(handler.getMessage(connection,information,4096,0)==0);
    STTNET_CHECK(information.message=="hello");

    information=stt::network::WebSocketFDInformation{};
    setInput(connection,clientFrame(0x9,true,"p")+clientFrame(0x2,true,"data"));
    STTNET_CHECK(handler.getMessage(connection,information,4096,0)==3);
    STTNET_CHECK(information.message=="p");
    STTNET_CHECK(connection.p_buffer_now>0);
    STTNET_CHECK(handler.getMessage(connection,information,4096,0)==0);
    STTNET_CHECK(information.message=="data");
}

void testInvalidClosePayload()
{
    stt::test::TcpConnectionFixture connection;
    stt::network::WebSocketFDInformation information{};
    stt::network::WebSocketServerFDHandler handler;
    setInput(connection,clientFrame(0x8,true,std::string(1,'x')));
    STTNET_CHECK(handler.getMessage(connection,information,4096,0)==-1);

    const std::string reservedCode{static_cast<char>(0x07),static_cast<char>(0xD0)}; // 2000
    setInput(connection,clientFrame(0x8,true,reservedCode));
    STTNET_CHECK(handler.getMessage(connection,information,4096,0)==-1);
}

void testTextAndCloseReasonMustBeUtf8()
{
    stt::test::TcpConnectionFixture connection;
    stt::network::WebSocketFDInformation information{};
    stt::network::WebSocketServerFDHandler handler;
    setInput(connection,clientFrame(0x1,true,std::string("\xC0\xAF",2)));
    STTNET_CHECK(handler.getMessage(connection,information,4096,0)==-1);

    const std::string invalidReason{static_cast<char>(0x03),static_cast<char>(0xE8),
                                    static_cast<char>(0xED),static_cast<char>(0xA0),static_cast<char>(0x80)};
    setInput(connection,clientFrame(0x8,true,invalidReason));
    STTNET_CHECK(handler.getMessage(connection,information,4096,0)==-1);
}

} // namespace

int main()
{
    testServerFrameEncoding();
    testMaskedFrameAndProtocolValidation();
    testFragmentationAndInterleavedControlFrame();
    testInvalidClosePayload();
    testTextAndCloseReasonMustBeUtf8();
    std::cout<<"all WebSocket tests passed\n";
    return 0;
}
