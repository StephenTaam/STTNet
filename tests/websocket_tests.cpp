#include "sttnet.h"

#include <cassert>
#include <cstring>
#include <iostream>

namespace {

std::string clientFrame(const uint8_t opcode,const bool fin,const std::string &payload)
{
    assert(payload.size()<=125);
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
    delete[] connection.buffer;
    connection.buffer_capacity=std::max<size_t>(wire.size(),256);
    connection.buffer=new char[connection.buffer_capacity];
    std::memcpy(connection.buffer,wire.data(),wire.size());
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
    assert(handler.sendMessage("hello","0001"));
    assert(wire.size()==7);
    assert(static_cast<unsigned char>(wire[0])==0x81);
    assert(static_cast<unsigned char>(wire[1])==5);
    assert(wire.substr(2)=="hello");
    assert(!handler.sendMessage(std::string(126,'x'),"1001"));
    assert(!handler.sendMessage("x","invalid"));
    assert(!handler.sendMessage(std::string("\xC0\xAF",2),"0001"));
    assert(!handler.sendMessage(std::string{static_cast<char>(0x07),static_cast<char>(0xD0)},"1000"));
}

void testMaskedFrameAndProtocolValidation()
{
    stt::network::TcpFDInf connection;
    stt::network::WebSocketFDInformation information{};
    stt::network::WebSocketServerFDHandler handler;
    setInput(connection,clientFrame(0x1,true,"hello"));
    assert(handler.getMessage(connection,information,4096,0)==0);
    assert(information.message=="hello");

    setInput(connection,std::string("\x81\x01x",3));
    assert(handler.getMessage(connection,information,4096,0)==-1);

    setInput(connection,clientFrame(0x9,false,"bad"));
    assert(handler.getMessage(connection,information,4096,0)==-1);
}

void testFragmentationAndInterleavedControlFrame()
{
    stt::network::TcpFDInf connection;
    stt::network::WebSocketFDInformation information{};
    stt::network::WebSocketServerFDHandler handler;
    setInput(connection,clientFrame(0x1,false,"hel")+clientFrame(0x0,true,"lo"));
    assert(handler.getMessage(connection,information,4096,0)==0);
    assert(information.message=="hello");

    information=stt::network::WebSocketFDInformation{};
    setInput(connection,clientFrame(0x9,true,"p")+clientFrame(0x2,true,"data"));
    assert(handler.getMessage(connection,information,4096,0)==3);
    assert(information.message=="p");
    assert(connection.p_buffer_now>0);
    assert(handler.getMessage(connection,information,4096,0)==0);
    assert(information.message=="data");
}

void testInvalidClosePayload()
{
    stt::network::TcpFDInf connection;
    stt::network::WebSocketFDInformation information{};
    stt::network::WebSocketServerFDHandler handler;
    setInput(connection,clientFrame(0x8,true,std::string(1,'x')));
    assert(handler.getMessage(connection,information,4096,0)==-1);

    const std::string reservedCode{static_cast<char>(0x07),static_cast<char>(0xD0)}; // 2000
    setInput(connection,clientFrame(0x8,true,reservedCode));
    assert(handler.getMessage(connection,information,4096,0)==-1);
}

void testTextAndCloseReasonMustBeUtf8()
{
    stt::network::TcpFDInf connection;
    stt::network::WebSocketFDInformation information{};
    stt::network::WebSocketServerFDHandler handler;
    setInput(connection,clientFrame(0x1,true,std::string("\xC0\xAF",2)));
    assert(handler.getMessage(connection,information,4096,0)==-1);

    const std::string invalidReason{static_cast<char>(0x03),static_cast<char>(0xE8),
                                    static_cast<char>(0xED),static_cast<char>(0xA0),static_cast<char>(0x80)};
    setInput(connection,clientFrame(0x8,true,invalidReason));
    assert(handler.getMessage(connection,information,4096,0)==-1);
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
