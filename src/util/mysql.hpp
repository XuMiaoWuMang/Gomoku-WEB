#pragma once
#include "logger.hpp"
#include <mysql/mysql.h>
#include <string>
class MySQL_Util {
  public:
    // 创建mysql连接
    static MYSQL *MysqlCreate(const std::string &host, int port,
                              const std::string &user,
                              const std::string &password,
                              const std::string &database) {
        // 1. 初始化mysql
        MYSQL *mysql = mysql_init(nullptr);
        if (mysql == nullptr) {
            ELOG("mysql_init failed");
            return nullptr;
        }
        // 2. 连接数据库
        if (mysql_real_connect(mysql, host.c_str(), user.c_str(),
                               password.c_str(), database.c_str(), port,
                               nullptr, 0) == nullptr) {
            ELOG("mysql_real_connect failed");
            mysql_close(mysql);
            return nullptr;
        }
        // 3. 设置字符集
        if (mysql_set_character_set(mysql, "utf8mb4") != 0) {
            ELOG("mysql_set_character_set failed");
            mysql_close(mysql);
            return nullptr;
        }
        // 4. 指定数据库
        // if (mysql_select_db(mysql, database.c_str()) != 0) {
        //     ELOG("mysql_select_db failed");
        //     mysql_close(mysql);
        //     return nullptr;
        // }
        return mysql;
    }
    // 关闭mysql连接
    static void MysqlClose(MYSQL *mysql) {
        if (mysql != nullptr) {
            mysql_close(mysql);
        }
    }
    // 执行mysql查询
    static bool MysqlQuery(MYSQL *mysql, const std::string &query) {
        if (mysql == nullptr) {
            ELOG("mysql is nullptr");
            return false;
        }
        if (mysql_query(mysql, query.c_str()) != 0) {
            ELOG("mysql_query failed, err: %s", mysql_error(mysql));
            return false;
        }
        return true;
    }
};