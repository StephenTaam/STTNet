#include <sttnet.h>

#include <array>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    using stt::data::CryptoUtil;
    using stt::data::EncodingUtil;

    const std::string message="STTNet utility demo";

    // Base64 is reversible encoding, not encryption. Invalid encoded input is
    // rejected with an empty result; empty input also produces an empty result.
    const std::string base64=EncodingUtil::base64_encode(message);
    std::cout << "base64: " << base64 << '\n';
    std::cout << "decoded: " << EncodingUtil::base64_decode(base64) << '\n';

    // SHA-1 remains useful for protocol compatibility such as RFC 6455.
    // Do not use SHA-1 for password storage, signatures, or new security designs.
    std::string sha1Hex;
    CryptoUtil::sha11(message,sha1Hex);
    std::cout << "sha1: " << sha1Hex << '\n';

    // AES-256-CBC requires a 32-byte key and a 16-byte IV. Fixed values keep
    // this demo reproducible only. Production IVs must be unpredictable and CBC
    // ciphertext must be authenticated separately (or replaced by an AEAD mode).
    const std::array<unsigned char,32> key={
        '0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f',
        '0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
    const std::array<unsigned char,16> iv={
        'a','b','c','d','e','f','0','1','2','3','4','5','6','7','8','9'};

    // Reserve room for PKCS#7 padding and read the actual output length returned
    // by the overload. Never reconstruct binary output by guessing its length.
    std::vector<unsigned char> cipher(message.size()+EVP_MAX_BLOCK_LENGTH);
    std::size_t cipherLength=0;
    if(!CryptoUtil::encryptSymmetric(
           reinterpret_cast<const unsigned char*>(message.data()),message.size(),
           key.data(),iv.data(),cipher.data(),cipherLength))
        return 1;

    std::vector<unsigned char> plain(cipherLength);
    std::size_t plainLength=0;
    if(!CryptoUtil::decryptSymmetric(cipher.data(),cipherLength,key.data(),iv.data(),
                                     plain.data(),plainLength))
        return 2;

    std::cout << "cipher bytes: " << cipherLength << '\n';
    std::cout << "decrypted: "
              << std::string(reinterpret_cast<char*>(plain.data()),plainLength)
              << '\n';
    return 0;
}
