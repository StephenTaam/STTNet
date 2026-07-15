#include <sttnet.h>

#include <cstdint>
#include <iostream>

int main()
{
    using namespace stt::data;

    // Numeric conversion succeeds only when the entire input is valid.
    int port=0;
    NumberStringConvertUtil::toInt("8080",port,-1);
    std::cout << "port: " << port << '\n';
    NumberStringConvertUtil::toInt("8080abc",port,-1);
    std::cout << "strict fallback: " << port << '\n';

    double latency=2.34567;
    PrecisionUtil::getPreciesDouble(latency,2);
    std::cout << "rounded latency: " << latency << " ms\n";

    // This creates exact-length pseudorandom text from the Base64 alphabet.
    // It is not a cryptographic token and is not necessarily valid Base64 data.
    std::string displayId;
    RandomUtil::getRandomStr_base64(displayId,24);
    std::cout << "display id: " << displayId << '\n';

    // NetworkOrderUtil swaps all eight bytes in place. It is intended for the
    // 64-bit unsigned long ABI used by STTNet's Linux targets.
    unsigned long value=0x0102030405060708UL;
    NetworkOrderUtil::htonl_ntohl_64(value);
    std::cout << "byte-swapped: 0x" << std::hex << value << std::dec << '\n';
    return 0;
}
