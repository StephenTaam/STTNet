#include <sttnet.h>

#include <iostream>

int main()
{
    static_assert(stt::version_major==0);
    static_assert(stt::version_minor==7);
    Json::Value value;
    value["version"]=std::string(stt::version);
    const std::string serialized=stt::data::JsonHelper::toString(value);
    std::cout<<stt::version<<'\n';
    return stt::version=="0.7.0"&&!serialized.empty()?0:1;
}
