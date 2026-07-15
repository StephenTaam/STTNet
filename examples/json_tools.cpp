#include <sttnet.h>

#include <iostream>

int main()
{
    using stt::data::JsonHelper;

    // Build a JsonCpp object first. This is the clearest approach for nested data.
    Json::Value user;
    user["id"] = 1001;
    user["name"] = "Stephen";
    user["online"] = true;
    user["roles"].append("developer");
    user["roles"].append("operator");

    // Convert Json::Value to compact JSON text.
    const std::string jsonText = JsonHelper::toString(user);
    std::cout << "serialized: " << jsonText << '\n';

    // Parse JSON text back into Json::Value.
    const Json::Value parsed = JsonHelper::toJsonArray(jsonText);
    std::cout << "name: " << parsed["name"].asString() << '\n';

    // createJson() is convenient for small, flat objects.
    std::cout << "small object: "
              << JsonHelper::createJson("status", "ok", "count", 3)
              << '\n';

    // getValue() extracts a field as text. The return code distinguishes failure,
    // scalar values, and nested JSON values.
    std::string extracted;
    const int type = JsonHelper::getValue(jsonText, extracted, "value", "roles");
    std::cout << "roles type=" << type << ", value=" << extracted << '\n';

    return 0;
}
