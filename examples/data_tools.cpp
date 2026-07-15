#include <sttnet.h>

#include <cstdint>
#include <iostream>

int main()
{
    using namespace stt::data;

    int port = 0;
    NumberStringConvertUtil::toInt("8080", port);
    std::cout << "port: " << port << '\n';

    int invalid = 0;
    NumberStringConvertUtil::toInt("not-a-number", invalid, -1);
    std::cout << "invalid fallback: " << invalid << '\n';

    double latency = 2.34567;
    PrecisionUtil::getPreciesDouble(latency, 2);
    std::cout << "rounded latency: " << latency << " ms\n";

    std::string token;
    RandomUtil::getRandomStr_base64(token, 24);
    std::cout << "display token: " << token << '\n';

    // This utility swaps all eight bytes in place. It is intended for a 64-bit
    // unsigned long platform such as the Linux targets supported by STTNet.
    unsigned long value = 0x0102030405060708UL;
    NetworkOrderUtil::htonl_ntohl_64(value);
    std::cout << "byte-swapped: 0x" << std::hex << value << std::dec << '\n';

    return 0;
}
