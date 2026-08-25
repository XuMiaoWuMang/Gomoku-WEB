#include <iostream>
#include <string>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/http/response.hpp>
#include <websocketpp/server.hpp>

void print(const std::string &msg) { std::cout << msg << std::endl; }

void ws_open_handler(websocketpp::server<websocketpp::config::asio> *server,
                     websocketpp::connection_hdl hdl) {
    std::cout << "websocket连接成功..." << std::endl;
}
void ws_close_handler(websocketpp::server<websocketpp::config::asio> *server,
                      websocketpp::connection_hdl hdl) {
    std::cout << "websocket连接关闭..." << std::endl;
}
void ws_http_handler(websocketpp::server<websocketpp::config::asio> *server,
                     websocketpp::connection_hdl hdl) {
    auto conn = server->get_con_from_hdl(hdl);
    auto req = conn->get_request();
    std::cout << "websocket请求处理..." << std::endl;
    std::cout << "method: " << req.get_method() << std::endl;
    std::cout << "path: " << req.get_uri() << std::endl;
    std::cout << "body: " << req.get_body() << std::endl;
    std::stringstream ss;
    ss << "<!doctype html><html><head>"
       << "<title>hello websocket</title><body>"
       << "<h1>hello websocketpp</h1>"
       << "</body></head></html>";
    conn->set_status(websocketpp::http::status_code::ok);
    conn->set_body(ss.str());
    websocketpp::server<websocketpp::config::asio>::timer_ptr timer =
        conn->set_timer(5000, std::bind(print, "hello websocketpp"));
    timer->cancel();
}
void ws_message_handler(
    websocketpp::server<websocketpp::config::asio> *server,
    websocketpp::connection_hdl hdl,
    websocketpp::server<websocketpp::config::asio>::message_ptr msg) {
    std::cout << "client say: " << msg->get_payload() << std::endl;
    auto conn = server->get_con_from_hdl(hdl);
    conn->send(msg->get_payload(), msg->get_opcode());
}

int main() {
    // 1. 实例化服务器对象
    websocketpp::server<websocketpp::config::asio> server;

    // 2. 初始化asio调度器
    server.init_asio();

    // 3. 设置日志等级
    server.set_access_channels(websocketpp::log::alevel::none);

    // 4. 设置事件处理函数
    server.set_open_handler(
        std::bind(&ws_open_handler, &server, std::placeholders::_1));
    server.set_close_handler(
        std::bind(&ws_close_handler, &server, std::placeholders::_1));
    server.set_http_handler(
        std::bind(&ws_http_handler, &server, std::placeholders::_1));
    server.set_message_handler(std::bind(&ws_message_handler, &server,
                                         std::placeholders::_1,
                                         std::placeholders::_2));

    // 5. 监听端口
    server.listen(8089);

    // 6. 接受连接
    server.start_accept();

    // 7. 运行服务器
    server.run();

    return 0;
}