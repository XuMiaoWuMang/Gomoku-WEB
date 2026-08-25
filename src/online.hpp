#pragma once
#include "util/logger.hpp"
#include <mutex>
#include <unordered_map>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#define websocket_server websocketpp::server<websocketpp::config::asio>
class online_manager {
  public:
    online_manager(){
        ILOG("在线管理器启动成功");
    }
    ~online_manager(){
        ILOG("在线管理器关闭成功");
    }
    // 进入游戏大厅
    void enter_game_hall(uint64_t uid, websocket_server::connection_ptr conn) {
        std::lock_guard<std::mutex> lock(_mutex);
        _online_in_hall.insert(std::make_pair(uid, conn));
    }
    // 进入游戏房间
    void enter_game_room(uint64_t uid, websocket_server::connection_ptr conn) {
        std::lock_guard<std::mutex> lock(_mutex);
        _online_in_room.insert(std::make_pair(uid, conn));
    }

    // 是否在游戏大厅
    bool is_in_game_hall(uint64_t uid) {
        std::lock_guard<std::mutex> lock(_mutex);
        return _online_in_hall.find(uid) != _online_in_hall.end();
    }
    // 是否在游戏房间
    bool is_in_game_room(uint64_t uid) {
        std::lock_guard<std::mutex> lock(_mutex);
        return _online_in_room.find(uid) != _online_in_room.end();
    }
    // 退出游戏大厅
    void exit_game_hall(uint64_t uid) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_online_in_hall.find(uid) != _online_in_hall.end())
            _online_in_hall.erase(uid);
    }
    // 退出游戏房间
    void exit_game_room(uint64_t uid) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_online_in_room.find(uid) != _online_in_room.end())
            _online_in_room.erase(uid);
    }

    // 获取用户的大厅连接
    websocket_server::connection_ptr get_game_hall_conn(uint64_t uid) {
        std::lock_guard<std::mutex> lock(_mutex);
        return _online_in_hall.find(uid) != _online_in_hall.end()
                   ? _online_in_hall.at(uid)
                   : nullptr;
    }
    // 获取用户的游戏房间连接
    websocket_server::connection_ptr get_game_room_conn(uint64_t uid) {
        std::lock_guard<std::mutex> lock(_mutex);
        return _online_in_room.find(uid) != _online_in_room.end()
                   ? _online_in_room.at(uid)
                   : nullptr;
    }

  private:
    std::mutex _mutex;
    std::unordered_map<uint64_t, websocket_server::connection_ptr>
        _online_in_hall;
    std::unordered_map<uint64_t, websocket_server::connection_ptr>
        _online_in_room;
};
