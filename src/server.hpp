#pragma once
#include "matcher.hpp"
#include "online.hpp"
#include "session.hpp"
#include "util/file.hpp"
#include "util/json.hpp"
#include "util/str.hpp"
#include <vector>
class gomoku_server {
  public:
    gomoku_server(std::string user, std::string pass, std::string db,
                  std::string web_root, std::string host, int port)
        : _web_root(web_root), _server(),
          _user_table(user, pass, db, host, port), _session_manager(&_server),
          _online_manager(), _room_manager(&_user_table, &_online_manager),
          _matcher(&_room_manager, &_user_table, &_online_manager) {
        // 设置日志等级
        _server.set_access_channels(websocketpp::log::alevel::none);
        // 初始化asio调度器
        _server.init_asio();
        // 设置地址重用
        _server.set_reuse_addr(true);
        // 设置事件处理函数
        _server.set_open_handler(std::bind(&gomoku_server::ws_open_handler,
                                           this, &_server,
                                           std::placeholders::_1));
        _server.set_close_handler(std::bind(&gomoku_server::ws_close_handler,
                                            this, &_server,
                                            std::placeholders::_1));
        _server.set_http_handler(std::bind(&gomoku_server::ws_http_handler,
                                           this, &_server,
                                           std::placeholders::_1));
        _server.set_message_handler(
            std::bind(&gomoku_server::ws_message_handler, this, &_server,
                      std::placeholders::_1, std::placeholders::_2));
        ILOG("服务器启动成功");
    }
    ~gomoku_server() { ILOG("服务器关闭成功"); }
    void start(int port) {
        // 监听端口
        _server.listen(port);

        // 接受连接
        _server.start_accept();

        // 运行服务器
        _server.run();
    }

  private:
    void file_handler(websocket_server::connection_ptr conn) {
        // 静态资源请求处理
        std::string uri = conn->get_request().get_uri();
        if (uri == "/") {
            uri = "/index.html";
        }
        std::string path = _web_root + uri;
        std::string body;
        // 读取页面内容
        bool ret = File_Util::read(path, body);
        if (ret == false) {
            body = "<html>"
                   "<head><meta charset=\"UTF-8\"><title>404 Not "
                   "Found</title></head>"
                   "<body><h1>404 Not Found</h1></body>"
                   "</html>";
            conn->set_status(websocketpp::http::status_code::not_found);
            // conn->send(body);
            return;
        }
        // 响应状态码的默认设置为500，所以一定要自己设置
        conn->set_status(websocketpp::http::status_code::ok);
        conn->set_body(body);
    }
    void http_resp(websocket_server::connection_ptr conn,
                   websocketpp::http::status_code::value status_code,
                   bool result, std::string reason) {
        Json::Value resp;
        std::string resp_body;
        resp["result"] = result;
        resp["reason"] = reason;
        Json_Util::serialize(resp, resp_body);
        conn->set_body(resp_body);
        conn->set_status(status_code);
        conn->append_header("Content-Type", "application/json");
        return;
    }
    void reg(websocket_server::connection_ptr conn) {
        // 注册处理
        // 获取username和password
        websocketpp::http::parser::request request = conn->get_request();
        std::string body = request.get_body();
        // 解析body
        Json::Value root;
        bool flag = Json_Util::deserialize(body, root);
        if (flag == false) {
            http_resp(conn, websocketpp::http::status_code::bad_request, false,
                      "未知格式请求体");
            return;
        }

        if (root["username"].isNull() || root["password"].isNull()) {
            http_resp(conn, websocketpp::http::status_code::bad_request, false,
                      "用户名或密码为空");
            return;
        }

        // 注册
        flag = _user_table.insert_user(root);
        if (flag == false) {
            http_resp(conn, websocketpp::http::status_code::bad_request, false,
                      "用户名已存在");
            return;
        }
        // 注册成功
        http_resp(conn, websocketpp::http::status_code::ok, true, "注册成功");
        DLOG("用户[%ld]注册成功", root["id"].asUInt64());
        return;
    }
    void login(websocket_server::connection_ptr conn) {
        // 登录处理
        // 获取 username和password
        websocketpp::http::parser::request request = conn->get_request();
        std::string body = request.get_body();
        // 解析body
        Json::Value root;
        bool flag = Json_Util::deserialize(body, root);
        if (flag == false) {
            http_resp(conn, websocketpp::http::status_code::bad_request, false,
                      "未知格式请求体");
            return;
        }

        // 校验字段完整性
        if (root["username"].isNull() || root["password"].isNull()) {
            http_resp(conn, websocketpp::http::status_code::bad_request, false,
                      "用户名或密码为空");
            return;
        }

        // 登录校验
        flag = _user_table.login(root);
        if (flag == false) {
            // 登录失败
            http_resp(conn, websocketpp::http::status_code::bad_request, false,
                      "用户名或密码错误");
            return;
        }
        uint64_t uid = root["id"].asUInt64();
        // 创建会话
        session_ptr ssp =
            _session_manager.create_session(uid, session_status::IS_LOGIN);
        if (ssp.get() == nullptr) {
            // 创建会话失败
            http_resp(conn,
                      websocketpp::http::status_code::internal_server_error,
                      false, "创建会话失败");
            return;
        }

        _session_manager.set_session_timeout(ssp->session_id(),
                                             SESSION_TIMEOUT);
        DLOG("用户[%ld]登录成功", uid);
        DLOG("会话ID: %lu 的存活时间设置为 %d 毫秒。", ssp->session_id(),
             SESSION_TIMEOUT);
        // 登录成功
        std::string sessionID = "SSID=" + std::to_string(ssp->session_id());

        // 设置会话ID为Cookie
        conn->append_header("Set-Cookie", sessionID);

        http_resp(conn, websocketpp::http::status_code::ok, true, "登录成功");
        // _session_manager.create_session()
        return;
    }
    // 从Cookie中获取指定键值对的值
    bool get_cookie_val(std::string cookie, std::string key,
                        std::string &value) {
        // Cookie以 "; " 分割
        // DLOG("原始Cookie: %s", cookie.c_str());
        std::vector<std::string> cookies_arr;
        std::string sep = "; ";
        // ret: 分割后的字符串数量
        int ret = Str_Util::split(cookie, sep, cookies_arr);
        // DLOG("分割后的Cookie数量: %d", ret);

        // 遍历Cookie数组, 找到指定键值对
        for (auto cookie : cookies_arr) {
            std::vector<std::string> cookie_tmp;
            Str_Util::split(cookie, "=", cookie_tmp);
            if (cookie_tmp.size() < 2) {
                continue;
            }
            if (cookie_tmp.size() == 2) {
                if (cookie_tmp[0] == key) {
                    value = cookie_tmp[1];
                    return true;
                }
            }
        }
        return false;
    }

    void user_info(websocket_server::connection_ptr conn) {
        Json::Value resp;
        // 获取session
        session_ptr ssp = get_session_by_cookie(conn);
        if (ssp.get() == nullptr) {
            return;
        }

        // 获取用户ID
        uint64_t uid = ssp->get_user_id();
        if (uid <= 0) {
            http_resp(conn, websocketpp::http::status_code::bad_request, false,
                      "会话用户ID不合法");
            return;
        }

        // 查询用户信息
        Json::Value user;
        bool ret = _user_table.select_by_id(uid, user);
        if (ret == false) {
            http_resp(conn, websocketpp::http::status_code::bad_request, false,
                      "用户信息查询失败");
            return;
        }
        // 返回用户信息
        std::string user_str;
        ret = Json_Util::serialize(user, user_str);
        if (ret == false) {
            http_resp(conn, websocketpp::http::status_code::bad_request, false,
                      "用户信息序列化失败");
            return;
        }
        conn->set_body(user_str);
        conn->set_status(websocketpp::http::status_code::ok);
        conn->append_header("Content-Type", "application/json");
        DLOG("用户查询成功");

        // 刷新session的过期时间
        _session_manager.set_session_timeout(ssp->session_id(),
                                             SESSION_TIMEOUT);
        return;
    }

    void ws_resp(websocket_server::connection_ptr conn, Json::Value &resp) {
        std::string resp_str;
        Json_Util::serialize(resp, resp_str);
        conn->send(resp_str);
    }
    session_ptr get_session_by_cookie(websocket_server::connection_ptr conn) {
        // 获取cookie
        std::string cookie = conn->get_request_header("Cookie");
        Json::Value resp;
        if (cookie.empty()) {
            resp["optype"] = "hall_ready";
            resp["result"] = false;
            resp["reason"] = "无Cookie信息，请重新访问";
            ws_resp(conn, resp);
            return session_ptr();
        }

        // 获取SSID
        std::string SSID_str;
        bool ret = get_cookie_val(cookie, "SSID", SSID_str);
        if (ret == false) {
            resp["optype"] = "hall_ready";
            resp["result"] = false;
            resp["reason"] = "无SSID信息，请重新登录";
            ws_resp(conn, resp);
            return session_ptr();
        }

        // 是否登录
        session_ptr ssp = _session_manager.get_session(std::stoul(SSID_str));
        if (ssp.get() == nullptr) {
            resp["optype"] = "hall_ready";
            resp["result"] = false;
            resp["reason"] = "会话已过期，请重新登录";
            ws_resp(conn, resp);
            return session_ptr();
        }
        return ssp;
    }
    void ws_open_hall(websocket_server::connection_ptr conn) {
        Json::Value resp;
        // 获取session
        session_ptr ssp = get_session_by_cookie(conn);
        if (ssp.get() == nullptr) {
            return;
        }

        // 是否重复登录
        if (_online_manager.is_in_game_hall(ssp->get_user_id()) ||
            _online_manager.is_in_game_room(ssp->get_user_id())) {
            resp["optype"] = "hall_ready";
            resp["result"] = false;
            resp["reason"] = "用户已登录游戏大厅";
            return ws_resp(conn, resp);
        }

        // 登录游戏大厅
        _online_manager.enter_game_hall(ssp->get_user_id(), conn);
        DLOG("用户登录游戏大厅成功");
        // 返回登录成功
        resp["optype"] = "hall_ready";
        resp["result"] = true;
        resp["reason"] = "登录成功";
        ws_resp(conn, resp);

        DLOG("用户[%ld]登录游戏大厅成功", ssp->get_user_id());
        _session_manager.set_session_timeout(ssp->session_id(),
                                             SESSION_FOREVER);
    }
    void ws_open_room(websocket_server::connection_ptr conn) {
        Json::Value resp;
        // 获取session
        session_ptr ssp = get_session_by_cookie(conn);
        if (ssp.get() == nullptr) {
            ws_resp(conn, resp);
            return;
        }

        // 是否重复登录
        if (_online_manager.is_in_game_hall(ssp->get_user_id()) ||
            _online_manager.is_in_game_room(ssp->get_user_id())) {
            resp["optype"] = "room_ready";
            resp["result"] = false;
            resp["reason"] = "用户已存在连接";
            return ws_resp(conn, resp);
        }
        // 判断房间是否已创建
        room_ptr room = _room_manager.get_room_by_uid(ssp->get_user_id());
        if (room.get() == nullptr) {
            resp["optype"] = "room_ready";
            resp["result"] = false;
            resp["reason"] = "房间不存在";
            return ws_resp(conn, resp);
        }

        // 登录游戏房间
        _online_manager.enter_game_room(ssp->get_user_id(), conn);
        DLOG("用户登录游戏房间成功");
        // 返回响应信息
        resp["optype"] = "room_ready";
        resp["result"] = true;
        resp["reason"] = "房间已就绪";
        resp["room_id"] = room->get_room_id();    // 房间ID
        resp["self_id"] = ssp->get_user_id();     // ⾃⾝ID
        resp["white_id"] = room->get_white_uid(); // ⽩棋ID
        resp["black_id"] = room->get_black_uid(); // ⿊棋ID
        ws_resp(conn, resp);

        DLOG("用户[%ld]登录游戏房间成功", ssp->get_user_id());
        // 如果两位玩家都进入房间，设置房间状态为RUNNING
        if (room->get_play_count() == 2) {
            room->set_status(room_status::GAME_RUNNING);
        }
        _session_manager.set_session_timeout(ssp->session_id(),
                                             SESSION_FOREVER);
    }
    bool ws_open_handler(websocketpp::server<websocketpp::config::asio> *server,
                         websocketpp::connection_hdl hdl) {
        // 建立长连接
        websocket_server::connection_ptr conn = server->get_con_from_hdl(hdl);
        // 获取请求
        websocketpp::http::parser::request request = conn->get_request();
        std::string uri = request.get_uri();
        // DLOG("接受到长连接请求");
        if (uri == "/hall") {
            ws_open_hall(conn);
        } else if (uri == "/room") {
            ws_open_room(conn);
        }
        return true;
    }

    void ws_close_hall(websocket_server::connection_ptr conn) {
        websocketpp::http::parser::request request = conn->get_request();
        Json::Value resp;
        // 获取session
        session_ptr ssp = get_session_by_cookie(conn);
        if (ssp.get() == nullptr) {
            resp["optype"] = "unknow";
            resp["result"] = false;
            resp["reason"] = "会话已过期，请重新登录";
            ws_resp(conn, resp);
            return;
        }
        // 退出游戏大厅
        _online_manager.exit_game_hall(ssp->get_user_id());
        // 设置会话时间
        _session_manager.set_session_timeout(ssp->session_id(),
                                             SESSION_TIMEOUT);

        DLOG("用户[%ld]退出游戏大厅", ssp->get_user_id());
    }
    void ws_close_room(websocket_server::connection_ptr conn) {
        Json::Value resp;
        // 获取session
        session_ptr ssp = get_session_by_cookie(conn);
        if (ssp.get() == nullptr) {
            resp["optype"] = "room_ready";
            resp["result"] = false;
            resp["reason"] = "会话已过期，请重新登录";
            ws_resp(conn, resp);
            return;
        }

        // 退出游戏房间
        _room_manager.user_exit_room(ssp->get_user_id());

        // 退出在线管理器
        _online_manager.exit_game_room(ssp->get_user_id());

        // 设置会话时间
        _session_manager.set_session_timeout(ssp->session_id(),
                                             SESSION_TIMEOUT);

        DLOG("用户[%ld]退出游戏房间", ssp->get_user_id());
    }
    bool
    ws_close_handler(websocketpp::server<websocketpp::config::asio> *server,
                     websocketpp::connection_hdl hdl) {
        // 断开长连接
        websocket_server::connection_ptr conn = server->get_con_from_hdl(hdl);
        // 获取请求
        websocketpp::http::parser::request request = conn->get_request();
        std::string uri = request.get_uri();
        DLOG("接受到断开长连接请求");
        if (uri == "/hall") {
            ws_close_hall(conn);
        } else if (uri == "/room") {
            ws_close_room(conn);
        }
        return true;
    }
    bool ws_http_handler(websocketpp::server<websocketpp::config::asio> *server,
                         websocketpp::connection_hdl hdl) {

        // DLOG("HTTP 请求...");
        websocket_server::connection_ptr conn = server->get_con_from_hdl(hdl);

        // 解析请求
        websocketpp::http::parser::request request = conn->get_request();
        std::string uri = request.get_uri();
        std::string path = _web_root + uri;
        // DLOG("Path: %s", path.c_str());

        std::string method = request.get_method();
        if (method == "POST" && uri == "/reg") {
            reg(conn);
            return true;
        } else if (method == "POST" && uri == "/login") {
            login(conn);
            return true;
        } else if (method == "GET" && uri == "/user_info") {
            user_info(conn);
            return true;
        } else {
            file_handler(conn);
            return true;
        }

        return true;
    }
    void ws_game_hall(
        websocket_server::connection_ptr conn,
        websocketpp::server<websocketpp::config::asio>::message_ptr msg) {
        Json::Value resp;
        // 获取session
        session_ptr ssp = get_session_by_cookie(conn);
        if (ssp.get() == nullptr) {
            return;
        }

        // 根据optype字段分类处理
        Json::Value req_json;
        bool ret = Json_Util::deserialize(msg->get_payload(), req_json);
        if (ret == false) {
            resp["result"] = false;
            resp["reason"] = "请求参数错误";
            ws_resp(conn, resp);
            return;
        }

        if (!req_json["optype"].isNull() &&
            req_json["optype"].asString() == "match_start") {
            // 开始匹配
            DLOG("用户开始匹配");
            _matcher.add(ssp->get_user_id());
            resp["optype"] = "match_start";
            resp["result"] = true;
            ws_resp(conn, resp);
            return;
        } else if (!req_json["optype"].isNull() &&
                   req_json["optype"].asString() == "match_stop") {
            // 取消匹配
            DLOG("用户取消匹配");
            _matcher.del(ssp->get_user_id());
            resp["optype"] = "match_stop";
            resp["result"] = true;
            ws_resp(conn, resp);
            return;
        }
        resp["optype"] = "unknow";
        resp["result"] = false;
        resp["reason"] = "未知操作类型";
        ws_resp(conn, resp);
        return;
    }
    void ws_game_room(
        websocket_server::connection_ptr conn,
        websocketpp::server<websocketpp::config::asio>::message_ptr msg) {
        Json::Value req;
        // 获取session
        session_ptr ssp = get_session_by_cookie(conn);
        if (ssp.get() == nullptr) {
            return;
        } 
        
        // 判断房间是否已创建
        Json::Value resp;
        room_ptr room = _room_manager.get_room_by_uid(ssp->get_user_id());
        if (room.get() == nullptr) {
            resp["optype"] = "unknow";
            resp["result"] = false;
            resp["reason"] = "房间不存在";
            return ws_resp(conn, resp);
        }

        bool ret = Json_Util::deserialize(msg->get_payload(), req);
        if(ret == false) {
            resp["optype"] = "unknow";
            resp["result"] = false;
            resp["reason"] = "请求参数错误";
            return ws_resp(conn, resp);
        }

        if(!req.isMember("optype")) {
            resp["optype"] = "unknow";
            resp["result"] = false;
            resp["reason"] = "未知操作类型";
            return ws_resp(conn, resp);
        }

        // 分类处理
        room->dispatch(req);

        return;
    }
    bool ws_message_handler(
        websocketpp::server<websocketpp::config::asio> *server,
        websocketpp::connection_hdl hdl,
        websocketpp::server<websocketpp::config::asio>::message_ptr msg) {
        websocket_server::connection_ptr conn = server->get_con_from_hdl(hdl);

        // 获取消息
        std::string msg_str = msg->get_payload();
        // DLOG("接受到消息: %s", msg_str.c_str());

        websocketpp::http::parser::request request = conn->get_request();
        std::string uri = request.get_uri();
        if (uri == "/hall") {
            ws_game_hall(conn, msg);
        } else if (uri == "/room") {
            ws_game_room(conn, msg);
        }
        return true;
    }

  private:
    std::string _web_root;
    websocket_server _server;
    session_manager _session_manager;
    room_manager _room_manager;
    matcher _matcher;
    online_manager _online_manager;
    user_table _user_table;
};