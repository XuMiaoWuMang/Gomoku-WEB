#pragma once
#include <string>
#include "logger.hpp"
#include <vector>
class Str_Util {
public:
    // 字符串分割，返回值为分割后的字符串数量，分割后的字符串会放在ret中
  static int split(const std::string &str, std::string sep,
                   std::vector<std::string> &ret) {
      size_t pos = 0, index = 0;

      size_t n = 0; // 记录返回的字符串的数量
      while (pos < str.size()) {
          // 下一个分隔符的下标
          pos = str.find(sep, index);

          // 如果 没有找到分隔符，直接返回
          if (pos == std::string::npos) {
              if (index == str.size()) {
                  return n;
              }
              ret.push_back(str.substr(index));
              n++;
              return n;
          }
          // 如果下一个分隔符的位置与起始位置一致，则跳过
          if (pos - index == 0) {
              // 跳过重复的分隔符
              index += sep.size();
              continue;
          }
          ret.push_back(str.substr(index, pos - index));
          // 更新起始位置
          index = pos + sep.size();
          n++;
      }
      return n;
  }
};