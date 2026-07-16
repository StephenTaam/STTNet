#include "sttnet.h"
#include "test_assert.h"

#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using stt::time::DateTime;
using stt::time::Duration;

static_assert(std::is_same_v<decltype(std::declval<DateTime &>().checkTime()),Duration>);
static_assert(std::is_same_v<decltype(std::declval<DateTime &>().endTiming()),Duration>);
static_assert(std::is_same_v<decltype(std::declval<const DateTime &>().getDt()),Duration>);
static_assert(std::is_same_v<decltype(std::declval<const Duration &>().convertToMsec()),long long>);

void testDurationContract()
{
    const Duration empty;
    STTNET_CHECK(empty.isValid());
    STTNET_CHECK(empty.convertToMsec()==0);

    const Duration ninetySeconds(0,0,1,30,0);
    STTNET_CHECK(ninetySeconds.convertToMsec()==90000);
    STTNET_CHECK(std::abs(ninetySeconds.convertToSec()-90.0)<0.0001);
    STTNET_CHECK(Duration(0,0,1,0,0)==Duration(0,0,0,60,0));

    Duration normalized;
    normalized.recoverForm(90061001);
    STTNET_CHECK(normalized.day==1);
    STTNET_CHECK(normalized.hour==1);
    STTNET_CHECK(normalized.min==1);
    STTNET_CHECK(normalized.sec==1);
    STTNET_CHECK(normalized.msec==1);

    const Duration negative=Duration(0,0,0,1,0)-Duration(0,0,0,2,0);
    STTNET_CHECK(!negative.isValid());
    STTNET_CHECK(negative.convertToMsec()==-1);

    const Duration overflow(std::numeric_limits<long long>::max(),0,0,0,0);
    STTNET_CHECK(overflow.convertToMsec()==-1);
}

void testDateTimeContract()
{
    std::string text="2026-07-15T12:34:56";
    STTNET_CHECK(DateTime::convertFormat(text,"yyyy-mm-ddThh:mi:ss","yyyy/mm/dd hh:mi:ss"));
    STTNET_CHECK(text=="2026/07/15 12:34:56");

    std::string milliseconds="2026-07-15T12:34:56.789";
    STTNET_CHECK(DateTime::convertFormat(milliseconds,"yyyy-mm-ddThh:mi:ss.sss","dd/mm/yyyy hh:mi:ss.sss"));
    STTNET_CHECK(milliseconds=="15/07/2026 12:34:56.789");

    Duration difference;
    DateTime::calculateTime("2026-07-15T12:35:26.250","2026-07-15T12:34:56.000",difference,
                            "yyyy-mm-ddThh:mi:ss.sss","yyyy-mm-ddThh:mi:ss.sss");
    STTNET_CHECK(difference.convertToMsec()==30250);

    DateTime::calculateTime("2026-02-31T12:00:00","2026-02-28T12:00:00",difference);
    STTNET_CHECK(!difference.isValid());

    DateTime::calculateTime("2026-07-15T11:00:00","2026-07-15T12:00:00",difference);
    STTNET_CHECK(!difference.isValid());

    std::string result;
    DateTime::calculateTime("2026-07-15T12:00:00",Duration(0,0,1,30,0),result,"+");
    STTNET_CHECK(result=="2026-07-15T12:01:30");

    DateTime timer;
    STTNET_CHECK(!timer.checkTime().isValid());
    STTNET_CHECK(timer.startTiming());
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    const Duration current=timer.checkTime();
    STTNET_CHECK(current.isValid());
    STTNET_CHECK(current.convertToMsec()>=5);
    const Duration total=timer.endTiming();
    STTNET_CHECK(total.isValid());
    STTNET_CHECK(total.convertToMsec()>=current.convertToMsec());
    STTNET_CHECK(!timer.isStart());
    STTNET_CHECK(timer.getDt()==total);
}

void testStrictNumberConversion()
{
    int integer=0;
    stt::data::NumberStringConvertUtil::toInt("8080",integer,-1);
    STTNET_CHECK(integer==8080);
    stt::data::NumberStringConvertUtil::toInt("8080abc",integer,-1);
    STTNET_CHECK(integer==-1);
    stt::data::NumberStringConvertUtil::toInt("",integer,-1);
    STTNET_CHECK(integer==-1);

    int hexadecimal=0;
    stt::data::NumberStringConvertUtil::str16toInt("7f",hexadecimal,-1);
    STTNET_CHECK(hexadecimal==127);
    stt::data::NumberStringConvertUtil::str16toInt("7fg",hexadecimal,-1);
    STTNET_CHECK(hexadecimal==-1);

    float singlePrecision=0;
    stt::data::NumberStringConvertUtil::toFloat("1.25",singlePrecision,-1.0F);
    STTNET_CHECK(std::abs(singlePrecision-1.25F)<0.0001F);
    stt::data::NumberStringConvertUtil::toFloat("1.25ms",singlePrecision,-1.0F);
    STTNET_CHECK(singlePrecision==-1.0F);

    double decimal=0;
    stt::data::NumberStringConvertUtil::toDouble("2.5",decimal,-1.0);
    STTNET_CHECK(std::abs(decimal-2.5)<0.0001);
    stt::data::NumberStringConvertUtil::toDouble("2.5ms",decimal,-1.0);
    STTNET_CHECK(decimal==-1.0);
    stt::data::NumberStringConvertUtil::toDouble(" 2.5",decimal,-1.0);
    STTNET_CHECK(decimal==-1.0);
    stt::data::NumberStringConvertUtil::toDouble("nan",decimal,-1.0);
    STTNET_CHECK(decimal==-1.0);
    stt::data::NumberStringConvertUtil::toDouble("1e9999",decimal,-1.0);
    STTNET_CHECK(decimal==-1.0);

    bool boolean=false;
    stt::data::NumberStringConvertUtil::toBool("TRUE",boolean);
    STTNET_CHECK(boolean);
    stt::data::NumberStringConvertUtil::toBool("yes",boolean);
    STTNET_CHECK(!boolean);

    std::string hexadecimalText;
    stt::data::NumberStringConvertUtil::strto16(std::string("A\0\xff",3),hexadecimalText);
    STTNET_CHECK(hexadecimalText=="4100ff");
}

void testEncodingAndRandomContract()
{
    const std::string binary("A\0B\xff",4);
    const std::string encoded=stt::data::EncodingUtil::base64_encode(binary);
    STTNET_CHECK(!encoded.empty());
    STTNET_CHECK(stt::data::EncodingUtil::base64_decode(encoded)==binary);
    STTNET_CHECK(stt::data::EncodingUtil::base64_decode("A").empty());
    STTNET_CHECK(stt::data::EncodingUtil::base64_decode("!!!!").empty());
    STTNET_CHECK(stt::data::EncodingUtil::base64_decode("AA=A").empty());

    static const std::string alphabet=
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/";
    for(int length=0;length<=8;++length)
    {
        std::string value;
        stt::data::RandomUtil::getRandomStr_base64(value,length);
        STTNET_CHECK(value.size()==static_cast<std::size_t>(length));
        for(const char ch:value)
            STTNET_CHECK(alphabet.find(ch)!=std::string::npos);
    }

    std::string mask;
    stt::data::EncodingUtil::generateMask_4(mask);
    STTNET_CHECK(mask.size()==4);

    std::string payload="masked";
    const std::string original=payload;
    stt::data::EncodingUtil::maskCalculate(payload,mask);
    STTNET_CHECK(payload!=original);
    stt::data::EncodingUtil::maskCalculate(payload,mask);
    STTNET_CHECK(payload==original);
}

void testBitContract()
{
    std::string bits;
    stt::data::BitUtil::bitOutput('A',bits);
    STTNET_CHECK(bits=="01000001");

    char bit='x';
    stt::data::BitUtil::bitOutput_bit('A',0,bit);
    STTNET_CHECK(bit=='0');

    unsigned long value=123;
    stt::data::BitUtil::bitStrToNumber("1011",value);
    STTNET_CHECK(value==11);
    stt::data::BitUtil::bitStrToNumber("10x1",value);
    STTNET_CHECK(value==0);

    std::string bytes="old";
    stt::data::BitUtil::toBit("01000001",bytes);
    STTNET_CHECK(bytes=="A");
    stt::data::BitUtil::toBit("0100000",bytes);
    STTNET_CHECK(bytes.empty());
}

void testHttpStringContract()
{
    const std::string url="/users?userid=7&id=42&empty=&flag#fragment";
    std::string value;
    stt::data::HttpStringUtil::get_value_str(url,value,"id");
    STTNET_CHECK(value=="42");
    stt::data::HttpStringUtil::get_value_str(url,value,"userid");
    STTNET_CHECK(value=="7");
    stt::data::HttpStringUtil::get_value_str(url,value,"missing");
    STTNET_CHECK(value.empty());

    const std::string headers="content-type:\tapplication/json\r\nX-Request-ID: abc\r\n";
    stt::data::HttpStringUtil::get_value_header(headers,value,"Content-Type");
    STTNET_CHECK(value=="application/json");
    stt::data::HttpStringUtil::get_value_header(headers,value,"x-request-id");
    STTNET_CHECK(value=="abc");
}

void testFileCopyContract()
{
    const std::string source="/tmp/sttnet_contract_source.bin";
    const std::string target="/tmp/sttnet_contract_target.bin";
    const std::string payload("first\0line\nsecond\xff",18);
    {
        std::ofstream stream(source,std::ios::binary|std::ios::trunc);
        stream.write(payload.data(),static_cast<std::streamsize>(payload.size()));
    }
    STTNET_CHECK(stt::file::FileTool::copy(source,target));
    std::ifstream stream(target,std::ios::binary);
    const std::string copied((std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>());
    STTNET_CHECK(copied==payload);
    STTNET_CHECK(!stt::file::FileTool::copy("/tmp/sttnet_missing_source",target));
    STTNET_CHECK(stt::file::FileTool::get_file_size("/tmp/sttnet_missing_source")==
                 std::numeric_limits<std::size_t>::max());
    ::unlink(source.c_str());
    ::unlink(target.c_str());
}

void testDataUtilityBoundaries()
{
    double rounded=2.34567;
    stt::data::PrecisionUtil::getPreciesDouble(rounded,2);
    STTNET_CHECK(std::abs(rounded-2.35)<0.0001);

    unsigned long value=0x0102030405060708UL;
    const unsigned long original=value;
    stt::data::NetworkOrderUtil::htonl_ntohl_64(value);
    STTNET_CHECK(value==0x0807060504030201UL);
    stt::data::NetworkOrderUtil::htonl_ntohl_64(value);
    STTNET_CHECK(value==original);

    for(int i=0;i<100;++i)
    {
        const long generated=stt::data::RandomUtil::getRandomNumber(8,3);
        STTNET_CHECK(generated>=3&&generated<=8);
    }
}

void testLogDrainContract()
{
    const std::string path="/tmp/sttnet_contract_async.log";
    ::unlink(path.c_str());
    {
        stt::file::LogFile log(64);
        STTNET_CHECK(log.openFile(path,ISO8086B," "));
        log.writeLog("first record");
        log.writeLog("second record");
    }
    std::ifstream input(path);
    const std::string content((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    STTNET_CHECK(content.find("first record")!=std::string::npos);
    STTNET_CHECK(content.find("second record")!=std::string::npos);
    ::unlink(path.c_str());
}

void testCryptoLengthContract()
{
    const std::string plain="AES-CBC binary payload";
    const std::array<unsigned char,32> key={
        '0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f',
        '0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
    const std::array<unsigned char,16> iv={
        'a','b','c','d','e','f','0','1','2','3','4','5','6','7','8','9'};
    std::vector<unsigned char> cipher(plain.size()+EVP_MAX_BLOCK_LENGTH);
    std::size_t cipherLength=0;
    STTNET_CHECK(stt::data::CryptoUtil::encryptSymmetric(
        reinterpret_cast<const unsigned char*>(plain.data()),plain.size(),key.data(),iv.data(),
        cipher.data(),cipherLength));
    STTNET_CHECK(cipherLength>plain.size());
    STTNET_CHECK(cipherLength%16==0);

    std::vector<unsigned char> recovered(cipherLength);
    std::size_t recoveredLength=0;
    STTNET_CHECK(stt::data::CryptoUtil::decryptSymmetric(
        cipher.data(),cipherLength,key.data(),iv.data(),recovered.data(),recoveredLength));
    STTNET_CHECK(recoveredLength==plain.size());
    STTNET_CHECK(std::string(reinterpret_cast<char*>(recovered.data()),recoveredLength)==plain);
}

void testJsonHelperContract()
{
    const std::string object=stt::data::JsonHelper::createJson("ok",true,"count",3);
    const Json::Value parsed=stt::data::JsonHelper::toJsonArray(object);
    STTNET_CHECK(parsed.isObject());
    STTNET_CHECK(parsed["ok"].isBool());
    STTNET_CHECK(parsed["ok"].asBool());
    STTNET_CHECK(parsed["count"].asInt()==3);

    const std::string merged=stt::data::JsonHelper::jsonAdd("{\"a\":1}","{\"b\":2}");
    const Json::Value mergedObject=stt::data::JsonHelper::toJsonArray(merged);
    STTNET_CHECK(mergedObject["a"].asInt()==1);
    STTNET_CHECK(mergedObject["b"].asInt()==2);

    const std::string appended=stt::data::JsonHelper::jsonAdd("[1]","[2,3]");
    const Json::Value array=stt::data::JsonHelper::toJsonArray(appended);
    STTNET_CHECK(array.isArray());
    STTNET_CHECK(array.size()==3);
    STTNET_CHECK(stt::data::JsonHelper::jsonAdd("{}","[]").empty());
}

} // namespace

int main()
{
    testDurationContract();
    testDateTimeContract();
    testStrictNumberConversion();
    testEncodingAndRandomContract();
    testBitContract();
    testHttpStringContract();
    testFileCopyContract();
    testDataUtilityBoundaries();
    testLogDrainContract();
    testCryptoLengthContract();
    testJsonHelperContract();
    return 0;
}
