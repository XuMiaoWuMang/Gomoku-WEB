#pragma once
#include <string>
#include "logger.hpp"
#include <vector>
#include <fstream>
#include <sys/stat.h>
class File_Util {
public:
    // 文件读取，返回值为是否读取成功，读取到的字符串会放在body中
  static bool read(const std::string &filename, std::string &body) {
      // 防崩溃加固：仅允许读取普通文件，拒绝目录等非常规路径，
      // 避免 tellg 返回异常大值导致 resize 抛出 length_error 使进程终止。
      struct stat st;
      if (stat(filename.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
          ELOG("open file %s failed", filename.c_str());
          return false;
      }

      // 打开文件
      std::ifstream file(filename);
      if (!file.is_open()) {
          ELOG("open file %s failed", filename.c_str());
          return false;
      }

      // 读取文件大小
      size_t fsize = 0;
      file.seekg(0, std::ios::end);
      fsize = file.tellg();
      file.seekg(0, std::ios::beg);

      // 读取文件内容
      body.resize(fsize);
      file.read(&body[0], fsize);
      if (!file.good()) {
          ELOG("read file %s failed", filename.c_str());
          file.close();
          return false;
      }

      // 关闭文件
      file.close();

      return true;
  }
};
