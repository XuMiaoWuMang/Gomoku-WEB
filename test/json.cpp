#include <iostream>
#include <jsoncpp/json/json.h>
#include <memory>
#include <sstream>
#include <string>
#include "../src/util/json.hpp"

int main()
{
    Json::Value object;

    std::string ret;
    Json_Util::serialize(object, ret);
    
    Json::Value json;
    Json_Util::deserialize(ret, json);

    return 0;
}