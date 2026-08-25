#pragma once
#include "util/json.hpp"
#include "util/mysql.hpp"
#include <mutex>
class user_table {
  public:
    user_table(const std::string &user, const std::string &password,
               const std::string &database,
               const std::string &host = "localhost", int port = 3306) {
        _mysql = MySQL_Util::MysqlCreate(host, port, user, password, database);

        if (_mysql == nullptr) {
            ELOG("创建数据库失败");
            return;
        }
        ILOG("创建数据库成功");
    }
    ~user_table() {
        MySQL_Util::MysqlClose(_mysql);
        ILOG("已销毁数据库");
        _mysql = nullptr;
    }
    // 插入用户
    bool insert_user(Json::Value &user) {
// 格式化sql语句
#define INSERT_USER                                                            \
    "insert into user values (null, '%s', SHA2('%s', 256), 1000, 0, 0);"

        std::lock_guard<std::mutex> lock(_mutex);

        // 数据校验
        if (!user.isMember("username") || !user.isMember("password")) {
            DLOG("用户名或密码为空");
            return false;
        }

        // 插入用户
        char sql[256];
        sprintf(sql, INSERT_USER, user["username"].asCString(),
                user["password"].asCString());
        bool ret = MySQL_Util::MysqlQuery(_mysql, sql);
        if (ret == false) {
            ELOG("插入用户[%s]失败", user["username"].asCString());
            return false;
        }
        ILOG("插入用户[%s]成功", user["username"].asCString());
        return true;
    }
    bool login(Json::Value &user) {
// 若用户名不存在，或者密码不匹配，则返回false
#define LOGIN_USER                                                             \
    "select id, username, password, score, total_count, win_count from user "      \
    "where username = "                                                            \
    "'%s' and password = SHA2('%s', 256);"
        // 校验用户名或密码是否为空
        if (!user.isMember("username") || !user.isMember("password")) {
            DLOG("用户名或密码为空");
            return false;
        }
        // 执行登录查询
        char sql[256];
        sprintf(sql, LOGIN_USER, user["username"].asCString(),
                user["password"].asCString());
        MYSQL_RES *res = nullptr;
        {
            std::unique_lock<std::mutex> lock(_mutex);

            bool ret = MySQL_Util::MysqlQuery(_mysql, sql);
            if (ret == false) {
                ELOG("登录用户[%s]失败, err: %s",
                     user["username"].asString().c_str(), mysql_error(_mysql));
                return false;
            }

            // 解析查询结果
            res = mysql_store_result(_mysql);
            if (res == nullptr) {
                ELOG("存储查询结果失败, err: %s", mysql_error(_mysql));
                return false;
            }
        }
        // 若结果为空，则说明用户名或密码错误
        // 若结果为多个，异常情况，返回false
        int row_num = mysql_num_rows(res);
        if (row_num == 0) {
            DLOG("用户名或密码错误");
            mysql_free_result(res);
            return false;
        } else if (row_num > 1) {
            ELOG("用户不是唯一的");
            mysql_free_result(res);
            return false;
        }

        // 解析用户信息
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row == nullptr) {
            ELOG("查询用户信息失败, err: %s", mysql_error(_mysql));
            mysql_free_result(res);
            return false;
        }

        DLOG("查询用户信息成功, id:%s, username:%s, score:%s, total_count:%s, "
             "win_count:%s",
             row[0], row[1], row[3], row[4], row[5]);
        // 解析用户信息
        user["id"] = std::stoul(row[0]);
        user["username"] = row[1];
        // user["password"] = row[2];
        user["password"] = "";
        user["score"] = std::stoul(row[3]);
        user["total_count"] = std::stoi(row[4]);
        user["win_count"] = std::stoi(row[5]);

        mysql_free_result(res);
        ILOG("login验证通过");

        return true;
    }
    // 根据用户名查询用户
    bool select_by_name(const std::string &username, Json::Value &user) {
        char sql[256];
#define SELECT_BY_NAME                                                         \
    "select username, score, total_count, win_count from user where username = "   \
    "'%s';"
        // 校验用户名是否为空
        if (username.empty()) {
            DLOG("用户名为空");
            return false;
        }
        // 格式化查询语句
        sprintf(sql, SELECT_BY_NAME, username.c_str());
        MYSQL_RES *res = nullptr;
        {
            std::unique_lock<std::mutex> lock(_mutex);

            bool ret = MySQL_Util::MysqlQuery(_mysql, sql);
            if (ret == false) {
                ELOG("查询用户[%s]失败, err: %s", username.c_str(),
                     mysql_error(_mysql));
                return false;
            }

            // 解析查询结果
            res = mysql_store_result(_mysql);
            if (res == nullptr) {
                ELOG("存储查询结果失败, err: %s", mysql_error(_mysql));
                return false;
            }
        }
        int row_num = mysql_num_rows(res);
        if (row_num == 0) {
            DLOG("用户[%s]不存在", username.c_str());
            mysql_free_result(res);
            return false;
        } else if (row_num > 1) {
            ELOG("用户不唯一");
            mysql_free_result(res);
            return false;
        }
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row == nullptr) {
            ELOG("查询用户[%s]信息失败, err: %s", username.c_str(),
                 mysql_error(_mysql));
            mysql_free_result(res);
            return false;
        }
        DLOG("查询用户信息成功, id:%s, username:%s, score:%s, total_count:%s, "
             "win_count:%s",
             row[0], row[1], row[2], row[3], row[4]);

        // 解析用户信息
        user["username"] = row[0];
        user["score"] = (Json::UInt64)std::stoul(row[1]);
        user["total_count"] = (Json::UInt64)std::stoi(row[2]);
        user["win_count"] = (Json::UInt64)std::stoi(row[3]);

        mysql_free_result(res);
        ILOG("name查询成功");
        return true;
    }
    // 根据id查询用户
    bool select_by_id(int64_t id, Json::Value &user) {
        char sql[256];
#define SELECT_BY_ID                                                           \
    "select username, score, total_count, win_count from user where id = %ld;"
        // 格式化查询语句
        if (id <= 0) {
            DLOG("用户ID不合法");
            return false;
        }
        sprintf(sql, SELECT_BY_ID, id);
        MYSQL_RES *res = nullptr;
        {
            std::unique_lock<std::mutex> lock(_mutex);

            bool ret = MySQL_Util::MysqlQuery(_mysql, sql);
            if (ret == false) {
                ELOG("查询用户失败, err: %s", mysql_error(_mysql));
                return false;
            }

            // 解析查询结果
            res = mysql_store_result(_mysql);
            if (res == nullptr) {
                ELOG("存储查询结果失败, err: %s", mysql_error(_mysql));
                return false;
            }
        }
        int row_num = mysql_num_rows(res);
        if (row_num == 0) {
            DLOG("用户[%ld]不存在", id);
            mysql_free_result(res);
            return false;
        } else if (row_num > 1) {
            ELOG("用户[%ld]不是唯一的", id);
            mysql_free_result(res);
            return false;
        }
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row == nullptr) {
            ELOG("查询存储结果失败, err: %s", mysql_error(_mysql));
            mysql_free_result(res);
            return false;
        }
        // DLOG("查询用户信息成功, id:%s, username:%s, score:%s, total_count:%s, "
        //      "win_count:%s",
        //      row[0], row[1], row[2], row[3], row[4]);

        // 解析用户信息
        user["username"] = row[0];
        user["score"] = (Json::UInt64)std::stoul(row[1]);
        user["total_count"] = (Json::UInt64)std::stoi(row[2]);
        user["win_count"] = (Json::UInt64)std::stoi(row[3]);

        mysql_free_result(res);
        ILOG("id查询成功");
        return true;
    }
    // 胜利结算: 增加胜利场次、增加总场次、增加积分
    bool win(int64_t id) {
        char sql[256];
#define WIN_USER                                                               \
    "update user set win_count = win_count + 1, score = score + 100, "         \
    "total_count = total_count + 1 "                                           \
    "where id = %ld;"
        // 格式化查询语句
        if (id <= 0) {
            DLOG("用户ID不合法");
            return false;
        }
        sprintf(sql, WIN_USER, id);
        bool ret = MySQL_Util::MysqlQuery(_mysql, sql);
        if (ret == false) {
            ELOG("更新胜者数据失败, err: %s", mysql_error(_mysql));
            return false;
        }
        DLOG("更新胜者数据成功");
        return true;
    }
    // 失败结算: 减少积分、增加总场次
    bool lose(int64_t id){
    char sql[256];
#define LOSE_USER                                                              \
    "update user set score = score - 100, total_count = total_count + 1 "      \
    "where id = %ld;"
    // 格式化查询语句
    if (id <= 0) {
        DLOG("用户ID不合法");
        return false;
    }
    sprintf(sql, LOSE_USER, id);
    bool ret = MySQL_Util::MysqlQuery(_mysql, sql);
    if (ret == false) {
        ELOG("更新败者数据失败, err: %s", mysql_error(_mysql));
        return false;
    }
    DLOG("更新败者数据成功");
    return true;
}

  private:
    MYSQL *_mysql = nullptr;
    std::mutex _mutex;
};