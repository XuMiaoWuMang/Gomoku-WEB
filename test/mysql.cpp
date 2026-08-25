#include "../src/util/mysql.hpp"
#include <mysql/mysql.h>


int main() {
    MYSQL *mysql =
        MySQL_Util::MysqlCreate("localhost", 3306, "root", "change_me", "Gomoku"); // 真实密码请放 .env（勿提交）
    if (mysql == nullptr) {
        return 1;
    }

    // 执行语句
    if (!MySQL_Util::MysqlQuery(mysql, "insert into user values (NULL, 'xumiao', 18, 99, 98, 97);")) {
        return 1;
    }

    MySQL_Util::MysqlClose(mysql);
    
        return 0;
}
