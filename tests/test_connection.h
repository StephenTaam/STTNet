#pragma once

#include "sttnet.h"

namespace stt::test {

struct TcpConnectionFixture final : network::TcpFDInf
{
    TcpConnectionFixture()=default;
    TcpConnectionFixture(const TcpConnectionFixture&)=delete;
    TcpConnectionFixture& operator=(const TcpConnectionFixture&)=delete;
    TcpConnectionFixture(TcpConnectionFixture&&)=delete;
    TcpConnectionFixture& operator=(TcpConnectionFixture&&)=delete;

    ~TcpConnectionFixture()
    {
        delete[] buffer;
        buffer=nullptr;
        buffer_capacity=0;
        p_buffer_now=0;
    }
};

} // namespace stt::test
