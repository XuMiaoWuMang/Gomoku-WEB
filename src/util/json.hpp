#pragma once
#include <iostream>
#include <jsoncpp/json/json.h>
#include <memory>
#include <sstream>
#include <string>
#include "logger.hpp"

class Json_Util {
public:
  static bool serialize(const Json::Value &json, std::string &str) {
      // 1. 实例化工厂对象
      Json::StreamWriterBuilder builder;

      // 2. 实例化WriterBuilder对象
      std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());

      // 3. 序列化对象
      std::stringstream retStr;
      int ret = writer->write(json, &retStr);
      if (ret != 0) {
        //   ELOG("序列化失败");
          str = "";
          return false;
      }
    //   DLOG("序列化成功");

      str = retStr.str();
      return true;
  }
  static bool deserialize(const std::string &str, Json::Value &json) {
      // 1. 实例化工厂对象
      Json::CharReaderBuilder builder;

      // 2. 实例化ReaderBuilder对象
      std::unique_ptr<Json::CharReader> reader(builder.newCharReader());

      // 3. 反序列化对象
      std::string errmsg;
      bool ret =
          reader->parse(str.c_str(), str.c_str() + str.size(), &json, &errmsg);
      if (!ret) {
        //   ELOG("反序列化失败: %s", errmsg.c_str());
          return false;
      }
    //   DLOG("反序列化成功");
      return true;
  }
};