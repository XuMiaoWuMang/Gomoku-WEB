// #include "../src/db.hpp"
// #include "../src/util/json.hpp"
// #include "../src/online.hpp"
// #include "../src/room.hpp"
// #include "../src/session.hpp"
// #include "../src/matcher.hpp"
#include "../src/server.hpp"
#define HOST "localhost"
#define PORT 3306
#define USER "root"
#define PASSWORD "change_me" // 本地联调自行填写，真实密码请放 .env（勿提交）
#define DATABASE "Gomoku"

void sql_test(){
    user_table user(USER, PASSWORD, DATABASE, HOST, PORT);

    Json::Value userInfo;
    userInfo["name"] = "xumiao1";
    // userInfo["password"] = "123456";
    user.insert_user(userInfo);

    Json::Value tmp;
    user.select_by_name(userInfo["name"].asString(), tmp);

    std::string userStr;
    Json_Util::serialize(tmp, userStr);
    std::cout << userStr << std::endl;

    user.win(userInfo["id"].asInt64());

    user.lose(userInfo["id"].asInt64());
}
void online_test(){
    online_manager online;
    online.enter_game_hall(1, nullptr);
    std::cout << "#用户进入游戏大厅" << std::endl;
    std::cout << "用户是否在大厅: " << online.is_in_game_hall(1) << std::endl;
    std::cout << "用户是否在房间: " << online.is_in_game_room(1) << std::endl;

    online.enter_game_room(1, nullptr);
    std::cout << "#用户进入游戏房间" << std::endl;
    std::cout << "用户是否在大厅: " << online.is_in_game_hall(1) << std::endl;
    std::cout << "用户是否在房间: " << online.is_in_game_room(1) << std::endl;

    online.exit_game_hall(1);
    std::cout << "#用户退出游戏大厅" << std::endl;
    std::cout << "用户是否在大厅: " << online.is_in_game_hall(1) << std::endl;
    std::cout << "用户是否在房间: " << online.is_in_game_room(1) << std::endl;

    online.exit_game_room(1);
    std::cout << "#用户退出游戏房间" << std::endl;
    std::cout << "用户是否在大厅: " << online.is_in_game_hall(1) << std::endl;
    std::cout << "用户是否在房间: " << online.is_in_game_room(1) << std::endl;
}
void room_test(){
    user_table user(USER, PASSWORD, DATABASE, HOST, PORT);
    online_manager online;


    room_manager rm_manager(&user, &online);
    room_ptr rm = rm_manager.create_room(1, 2);
    std::cout << "房间id: " << rm->get_room_id() << std::endl;
    
}
void session_test(){
    websocketpp::server<websocketpp::config::asio> server;
    // 2. 初始化asio调度器
    server.init_asio();

    // 3. 设置日志等级
    server.set_access_channels(websocketpp::log::alevel::none);

    // 5. 监听端口
    server.listen(8089);

    // 6. 接受连接
    server.start_accept();

    // 7. 运行服务器
    server.run();

    session_manager sm(&server);
    session_ptr session = sm.create_session(1);
    std::cout << "sessionid: " << session->session_id() << std::endl;
}
void matcher_test(){
    user_table user(USER, PASSWORD, DATABASE, HOST, PORT);
    online_manager online;
    room_manager rm_manager(&user, &online);
    rm_manager.create_room(1, 2);
    matcher matcher(&rm_manager, &user, &online);
}
void server_test(){
    gomoku_server server(USER, PASSWORD, DATABASE, "./wwwroot", HOST, 8089);
    server.start(8089);
}
int main() {
    // sql_test();
    // online_test();
    // session_test();
    // matcher_test();
    server_test();
    return 0;
} 