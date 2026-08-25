#pragma once

#include "db.hpp"
#include "online.hpp"
#include "util/json.hpp"
#include <memory>
typedef enum { GAME_READY, GAME_RUNNING, GAME_OVER } room_status;

#define BOARD_ROW 15
#define BOARD_COL 15
#define CHESS_WHITE 1
#define CHESS_BLACK 2
class room {
  public:
    // 构造函数
    room(uint64_t room_id, user_table *user_table, online_manager *online)
        : _room_id(room_id), _user_table(user_table), _online(online),
          _board(std::vector<std::vector<int>>(
              BOARD_ROW, std::vector<int>(BOARD_COL, 0))) {
        ILOG("房间: %ld 创建成功", room_id);
    }
    // 析构函数
    ~room() { ILOG("房间: %ld 销毁", _room_id); }

    // 获取白棋 用户uid
    uint64_t get_white_uid() { return _white_uid; }
    // 获取黑棋 用户uid
    uint64_t get_black_uid() { return _black_uid; }
    // 获取当前回合
    int get_turn() { return _turn; }
    // 获取房间状态
    room_status get_status() { return _status; }
    // 获取玩家数量
    int get_play_count() { return _play_count; }
    // 获取房间id
    uint64_t get_room_id() { return _room_id; }
    // 设置白棋 用户uid
    void set_white_uid(uint64_t uid) { _white_uid = uid; }
    // 设置黑棋 用户uid
    void set_black_uid(uint64_t uid) { _black_uid = uid; }
    // 添加白棋 用户uid
    void add_white_uid(uint64_t uid) {
        _play_count++;
        _white_uid = uid;
    }
    // 添加黑棋 用户uid
    void add_black_uid(uint64_t uid) {
        _play_count++;
        _black_uid = uid;
    }
    // 设置房间状态
    void set_status(room_status status) { _status = status; }

    // 下棋处理函数
    Json::Value play(Json::Value &req) {
    Json::Value resp;
        // 1. 校验房间号是否匹配
        // uint64_t room_id = req["room_id"].asUInt64();
        // if (room_id != _room_id) {
        //     resp["optype"] = "put_chess";
        //     resp["result"] = false;
        //     resp["reason"] = "房间号不匹配";

        //     return resp;
        // }

        // 2. 校验玩家是否在房间内
        uint64_t cur_uid = req["uid"].asUInt64();
        uint64_t other_uid = cur_uid == _white_uid ? _black_uid : _white_uid;
        int row = req["row"].asInt();
        int col = req["col"].asInt();

        // 必要的公共字段统一填写
        resp["room_id"] = _room_id;
        resp["row"] = row;
        resp["col"] = col;
        resp["uid"] = cur_uid;

        if (!_online->is_in_game_room(cur_uid)) {
            resp["result"] = true;
            resp["reason"] = "玩家不在房间内";
            resp["winner"] = other_uid;
            resp["finished"] = true;
            return resp;
        } else if (!_online->is_in_game_room(other_uid)) {
            resp["result"] = true;
            resp["reason"] = "玩家不在房间内";
            resp["winner"] = cur_uid;
            resp["finished"] = true;
            return resp;
        }

        // 3. 校验下棋位置是否有效
        if(_turn % 2 == 1 && cur_uid != _black_uid) {
            resp["optype"] = "put_chess";
            resp["result"] = false;
            resp["reason"] = "当前回合不是黑棋回合";
            return resp;
        }else if(_turn % 2 == 0 && cur_uid != _white_uid) {
            resp["optype"] = "put_chess";
            resp["result"] = false;
            resp["reason"] = "当前回合不是白棋回合";
            return resp;
        }

        if (row < 0 || row >= BOARD_ROW || col < 0 || col >= BOARD_COL ||
            _board[row][col] != 0 ) {
            resp["optype"] = "put_chess";
            resp["result"] = false;
            resp["reason"] = "下棋位置无效";
            return resp;
        }
        int chess_color = cur_uid == _white_uid ? CHESS_WHITE : CHESS_BLACK;
        _board[row][col] = chess_color;

        // 4. 判断是否结束游戏
        uint64_t cur_winner;
        bool ret = check_win(row, col, chess_color, cur_winner);
        if (ret) {
            resp["result"] = true;
            if (cur_winner == 0) {
                resp["reason"] = "平局";
            } else {
                resp["reason"] = "五星连珠, 精彩对局！";
            }
            resp["winner"] = cur_winner;
            resp["finished"] = true;
            return resp;
        } else {
            resp["result"] = true;
            resp["reason"] = "下棋成功";
            resp["winner"] = 0;
            resp["finished"] = false;
        }
        return resp;
    }
    // 聊天处理函数
    Json::Value chat(Json::Value &req) {
        Json::Value resp;
        // 1. 校验房间号是否匹配
        // uint64_t room_id = req["room_id"].asUInt64();
        // if (room_id != _room_id) {
        //     resp["optype"] = "chat";
        //     resp["result"] = false;
        //     resp["reason"] = "房间号不匹配";
        //     return resp;
        // }

        // 2. 检测是否包含敏感词
        std::string msg = req["message"].asString();
        if (msg.find("垃圾") != std::string::npos) {
            resp["optype"] = "chat";
            resp["result"] = false;
            resp["reason"] = "包含敏感词，不能发送";
            return resp;
        }

        // 3. 广播消息
        resp["optype"] = "chat";
        resp["result"] = true;
        resp["message"] = req["message"].asString();
        return resp;
    }
    // 退出房间处理函数
    bool exit(uint64_t uid) {
        // DLOG("用户%lu退出房间", uid);
        Json::Value resp;
        // 1. 玩家退出，若在游戏中，则判另一名玩家获胜，若不在游戏中，则退出房间
        // DLOG("当前房间状态: %d", _status);
        if (_status == room_status::GAME_RUNNING) {
            uint64_t other_uid = _white_uid == uid ? _black_uid : _white_uid;
            _user_table->win(other_uid);
            _user_table->lose(uid);
            _status = room_status::GAME_OVER;
            resp["optype"] = "put_chess";
            resp["result"] = true;
            resp["reason"] = "玩家退出游戏";
            resp["winner"] = other_uid;
            resp["room_id"] = _room_id;
            resp["uid"] = uid;
            resp["row"] = -1;
            resp["col"] = -1;
            resp["finished"] = true;
            DLOG("玩家退出游戏...");
            broadcast(resp);
        }

        // 2. 退出游戏房间
        _online->exit_game_room(uid);
        _play_count--;

        return true;
    }
    // 检查是否结束游戏
    uint64_t check_win(int row, int col, int chess_color, uint64_t &winner) {
        if (check_five(row, col, 1, 0, chess_color) ||
            check_five(row, col, 0, 1, chess_color) ||
            check_five(row, col, 1, 1, chess_color) ||
            check_five(row, col, -1, 1, chess_color)) {
            winner = chess_color == CHESS_WHITE ? _white_uid : _black_uid;
            return true;
        }
        if (check_board()) {
            winner = 0;
            return true;
        }
        return false;
    }
    // 调度器
    void dispatch(Json::Value &req) {
        Json::Value resp;
        
        resp["optype"] = req["optype"].asString();

        // 1. 校验房间号是否匹配
        uint64_t room_id = req["room_id"].asUInt64();
        if (room_id != _room_id) {
            resp["optype"] = req["optype"].asString();
            resp["result"] = false;
            resp["reason"] = "房间号不匹配";
            broadcast(resp);
            DLOG("房间号不匹配");
            return;
        }
        // 根据不同的请求，调用不同的处理函数
        else if (req["optype"].asString() == "put_chess") {
            resp = play(req);
            resp["optype"] = req["optype"].asString();
            // result 为 true 时，说明操作合法且成功，广播消息
            if (resp["result"].asBool()) {
                broadcast(resp);
                if (resp["result"] == true)
                {
                    _turn++;
                }
            }

            // finished 为 true 时，说明游戏结束，结算积分积分
            if (resp["finished"].asBool()) {
                _status = room_status::GAME_OVER;
                uint64_t winner = resp["winner"].asUInt64();
                // winner为0时，说明平局
                if (winner != 0) {
                    uint64_t loser =
                        winner == _white_uid ? _black_uid : _white_uid;
                    _user_table->win(winner);
                    _user_table->lose(loser);
                }
            }
        } else if (req["optype"].asString() == "chat") {
            Json::Value resp = chat(req);
            resp["optype"] = req["optype"].asString();
            if (resp["result"].asBool()) {
                broadcast(resp);
            }
        } else {
            resp["optype"] = req["optype"].asString();
            resp["result"] = false;
            resp["reason"] = "未知请求类型";
            broadcast(resp);
        }
        // DLOG("收到一个%s请求", req["optype"].asCString());
    }

    // 广播消息
    void broadcast(Json::Value &resp) {
        std::string msg;
        if (!Json_Util::serialize(resp, msg)) {
            return;
        }
        websocket_server::connection_ptr white_conn =
            _online->get_game_room_conn(_white_uid);
        websocket_server::connection_ptr black_conn =
            _online->get_game_room_conn(_black_uid);
        if (white_conn != nullptr) {
            // DLOG("广播消息给白方玩家: %s", msg.c_str());
            white_conn->send(msg);
        }
        if (black_conn != nullptr) {
            // DLOG("广播消息给黑方玩家: %s", msg.c_str());
            black_conn->send(msg);
        }
        DLOG("广播消息: %s", msg.c_str());
    }

  private:
    // 检查五星连珠
    bool check_five(int row, int col, int row_offset, int col_offset,
                    int chess_color) {
        int count = 1;
        int next_row = row + row_offset;
        int next_col = col + col_offset;
        while (next_row >= 0 && next_row < BOARD_ROW && next_col >= 0 &&
               next_col < BOARD_COL &&
               _board[next_row][next_col] == chess_color) {
            count++;
            next_row += row_offset;
            next_col += col_offset;
        }
        next_row = row - row_offset;
        next_col = col - col_offset;
        while (next_row >= 0 && next_row < BOARD_ROW && next_col >= 0 &&
               next_col < BOARD_COL &&
               _board[next_row][next_col] == chess_color) {
            count++;
            next_row -= row_offset;
            next_col -= col_offset;
        }

        return count >= 5;
    }
    // 检查棋盘是否满
    bool check_board() const {
        if (_turn >= BOARD_ROW * BOARD_COL) {
            return true;
        }
        return false;
    }

  private:
    int _turn = 1;                        // 当前回合
    uint64_t _room_id = 0;                // 房间id
    uint64_t _white_uid = 0;              // 白方用户uid
    uint64_t _black_uid = 0;              // 黑方用户uid
    int _play_count = 0;                  // 玩家人数
    room_status _status = GAME_READY;     // 房间状态
    user_table *_user_table = nullptr;    // 结算用户积分句柄
    std::vector<std::vector<int>> _board; // 棋盘
    online_manager *_online = nullptr;    // 在线管理器句柄
};

using room_ptr = std::shared_ptr<room>;

class room_manager {
  public:
    // 构造函数
    room_manager(user_table *user_table, online_manager *online)
        : _user_table(user_table), _online(online), _next_room_id(1) {
            ILOG("房间管理器启动成功");
        }
    // 析构函数
    ~room_manager() {
        ILOG("房间管理器关闭成功");
    }

    // 创建房间
    room_ptr create_room(uint64_t white_uid, uint64_t black_uid) {
        // 1. 校验用户是否在大厅
        if (!_online->is_in_game_hall(white_uid)) {
            ELOG("用户%lu不在大厅", white_uid);
            return room_ptr();
        } else if (!_online->is_in_game_hall(black_uid)) {
            ELOG("用户%lu不在大厅", black_uid);
            return room_ptr();
        }

        // 2. 创建房间
        std::lock_guard<std::mutex> lock(_mutex);
        std::shared_ptr<room> rm_ptr(
            new room(_next_room_id, _user_table, _online));
        rm_ptr->add_white_uid(white_uid);
        rm_ptr->add_black_uid(black_uid);
        rm_ptr->set_status(room_status::GAME_READY);

        // 3. 保存房间信息
        _rooms.insert(std::make_pair(_next_room_id, rm_ptr));
        _room_map.insert(std::make_pair(white_uid, _next_room_id));
        _room_map.insert(std::make_pair(black_uid, _next_room_id));
        _next_room_id++;

        return rm_ptr;
    }
    // 通过房间id获取房间
    room_ptr get_room_by_rid(uint64_t room_id) {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _rooms.find(room_id);
        if (it == _rooms.end()) {
            return room_ptr();
        }
        return it->second;
    }
    // 通过用户uid获取房间
    room_ptr get_room_by_uid(uint64_t uid) {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _room_map.find(uid);
        if (it == _room_map.end()) {
            return room_ptr();
        }
        auto room = _rooms.find(it->second);
        if (room == _rooms.end()) {
            return room_ptr();
        }
        return room->second;
    }
    // 用户退出房间
    bool user_exit_room(uint64_t uid) {
        // DLOG("用户%lu退出房间", uid);
        // 1. 通过uid获取房间
        room_ptr room = get_room_by_uid(uid);
        if (room.get() == nullptr) {
            ELOG("用户%lu不在房间", uid);
            return false;
        }

        // 2. 从房间映射中移除
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _room_map.erase(uid);
        }

        // 3. 从房间管理器中移除用户
        room->exit(uid);
        _online->exit_game_room(uid);

        // 4. 销毁房间
        if (room->get_play_count() == 0) {
            destroy_room(room->get_room_id());
        }
        return true;
    }
    // 销毁房间
    bool destroy_room(uint64_t room_id) {
        // 1. 通过rid获取房间信息
        room_ptr room = get_room_by_rid(room_id);
        if (room == nullptr) {
            ELOG("房间%lu不存在", room_id);
            return false;
        }

        // 2. 通过uid获取所有用户信息
        uint64_t white_uid = room->get_white_uid();
        uint64_t black_uid = room->get_black_uid();

        // 3. 从房间映射中移除
        std::lock_guard<std::mutex> lock(_mutex);
        _room_map.erase(white_uid);
        _room_map.erase(black_uid);

        // 4. 从房间管理器中移除用户
        _online->exit_game_room(white_uid);
        _online->exit_game_room(black_uid);

        // 5. 销毁房间
        _rooms.erase(room_id);

        return true;
    }

  private:
    uint64_t _next_room_id = 0;                       // 下一个房间id
    std::mutex _mutex;                                // 互斥锁
    user_table *_user_table = nullptr;                // 结算用户积分句柄
    online_manager *_online = nullptr;                // 在线管理器句柄
    std::unordered_map<uint64_t, uint64_t> _room_map; // uid和rid映射表
    std::unordered_map<uint64_t, room_ptr> _rooms;    // 房间id映射表
};
