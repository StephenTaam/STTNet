#include <sttnet.h>

#include <iostream>

int main()
{
    using stt::data::JsonHelper;

    // Json::Value is the clearest option for nested data.
    Json::Value user(Json::objectValue);
    user["id"]=1001;
    user["name"]="Stephen";
    user["online"]=true;
    user["roles"].append("developer");
    user["roles"].append("operator");

    const std::string jsonText=JsonHelper::toString(user);
    std::cout << "serialized: " << jsonText << '\n';

    const Json::Value parsed=JsonHelper::toJsonArray(jsonText);
    if(!parsed.isObject())
        return 1;
    std::cout << "name: " << parsed["name"].asString() << '\n';

    // createJson() preserves booleans as JSON true/false rather than integers.
    const std::string small=JsonHelper::createJson("status","ok","online",true,"count",3);
    std::cout << "small object: " << small << '\n';

    // jsonAdd() merges object members or appends array elements. Mixing an object
    // and an array is invalid and returns an empty string.
    std::cout << "merged object: "
              << JsonHelper::jsonAdd("{\"a\":1}","{\"b\":2}") << '\n';
    std::cout << "appended array: "
              << JsonHelper::jsonAdd("[1]","[2,3]") << '\n';

    std::string extracted;
    const int type=JsonHelper::getValue(jsonText,extracted,"value","roles");
    std::cout << "roles type=" << type << ", value=" << extracted << '\n';
    return 0;
}
