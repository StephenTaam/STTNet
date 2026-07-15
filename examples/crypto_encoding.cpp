#include <sttnet.h>

#include <array>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    using stt::data::CryptoUtil;
    using stt::data::EncodingUtil;

    const std::string message = "STTNet utility demo";

    // Base64 is an encoding, not encryption. Anyone can decode it.
    const std::string base64 = EncodingUtil::base64_encode(message);
    std::cout << "base64: " << base64 << '\n';
    std::cout << "decoded: " << EncodingUtil::base64_decode(base64) << '\n';

    // SHA-1 is retained for protocol compatibility such as WebSocket handshakes.
    // Do not use SHA-1 for passwords, signatures, or new security designs.
    std::string sha1Hex;
    CryptoUtil::sha11(message, sha1Hex);
    std::cout << "sha1: " << sha1Hex << '\n';

    // AES-256-CBC requires a 32-byte key and a 16-byte IV. Fixed values are used
    // only to keep this demo reproducible; production keys and IVs must be generated
    // and managed securely, and CBC ciphertext also needs authentication.
    const std::array<unsigned char, 32> key = {
        '0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f',
        '0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'
    };
    const std::array<unsigned char, 16> iv = {
        'a','b','c','d','e','f','0','1','2','3','4','5','6','7','8','9'
    };

    // PKCS#7 padding makes the ciphertext a whole number of 16-byte blocks.
    const std::size_t cipherLength = (message.size() / 16 + 1) * 16;
    std::vector<unsigned char> cipher(cipherLength);
    if(!CryptoUtil::encryptSymmetric(
            reinterpret_cast<const unsigned char *>(message.data()),
            message.size(), key.data(), iv.data(), cipher.data()))
        return 1;

    std::vector<unsigned char> plain(cipherLength + 1, 0);
    if(!CryptoUtil::decryptSymmetric(
            cipher.data(), cipherLength, key.data(), iv.data(), plain.data()))
        return 2;

    // The current helper reports success but not the recovered byte count, so this
    // demo uses the known original text length when reconstructing the string.
    std::cout << "decrypted: "
              << std::string(reinterpret_cast<char *>(plain.data()), message.size())
              << '\n';

    return 0;
}
