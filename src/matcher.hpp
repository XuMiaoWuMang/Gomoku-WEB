#pragma once
#include "db.hpp"
#include "online.hpp"
#include "room.hpp"
#include <condition_variable>
#include <list>
#include <mutex>
#include <string>
#include <thread>
template <class T> class matcher_queue {
  public:
    int size() {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.size();
    }
    bool empty() {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.empty();
    }
    void push(const T &t) {
        DLOG("添加用户[%ld]到匹配队列", t);
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push_back(t);
        DLOG("匹配队列中用户[");
        for (auto &item : _queue) {
            DLOG("%ld，", item);
        }
        DLOG("]");
        _cond.notify_all();
    }
    void wait() {
        std::unique_lock<std::mutex> lock(_mutex);
        _cond.wait(lock);
    }
    bool pop(T &t) {
        std::unique_lock<std::mutex> lock(_mutex);

        if (_queue.empty() == true) {
            DLOG("匹配队列为空");
            return false;
        }
        DLOG("锁。。。");
        t = _queue.front();
        DLOG("从匹配队列弹出用户[%ld]", t);
        _queue.pop_front();
        DLOG("匹配队列中用户[");
        for (auto &item : _queue) {
            DLOG("%ld，", item);
        }
        DLOG("]");
        return true;
    }
    void remove(const T &t) {
        DLOG("移除用户[%ld]从匹配队列", t);
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.remove(t);
        DLOG("匹配队列中用户[");
        for (auto &item : _queue) {
            DLOG("%ld，", item);
        }
        DLOG("]");
    }

  private:
    std::list<T> _queue;
    std::mutex _mutex;
    std::condition_variable _cond;
};
class matcher {
  public:
    matcher(room_manager *room_manager, user_table *user_table,
            online_manager *online_manager)
        : _room_manager(room_manager), _user_table(user_table),
          _online_manager(online_manager) {
        _thread_bronze = std::thread(&matcher::match_bronze_entry, this);
        _thread_silver = std::thread(&matcher::match_silver_entry, this);
        _thread_gold = std::thread(&matcher::match_gold_entry, this);
        ILOG("匹配器启动成功");
    }
    ~matcher() {
        _thread_bronze.join();
        _thread_silver.join();
        _thread_gold.join();
        ILOG("匹配器关闭成功");
    }
    void add(uint64_t uid) {
        // 1. 获取用户信息
        Json::Value user;
        if (_user_table->select_by_id(uid, user) == false) {
            ELOG("用户[%ld]不存在", uid);
            return;
        }
        uint64_t score = user["score"].asUInt64();
        // 2 加到匹配队列
        if (score < 2000) {
            _queue_bronze.push(uid);
        } else if (score < 4000) {
            _queue_silver.push(uid);
        } else {
            _queue_gold.push(uid);
        }
    }
    void del(uint64_t uid) {
        // 1. 获取用户信息
        Json::Value user;
        if (_user_table->select_by_id(uid, user) == false) {
            ELOG("用户[%ld]不存在", uid);
            return;
        }
        uint64_t score = user["score"].asUInt64();
        // 2 加到匹配队列
        if (score < 2000) {
            _queue_bronze.remove(uid);
        } else if (score < 4000) {
            _queue_silver.remove(uid);
        } else {
            _queue_gold.remove(uid);
        }
    }

  private:
    // 线程入口函数
    void handle_match(matcher_queue<uint64_t> &queue) {
        while (true) {
            // 1. 等待匹配的玩家数量大于等于2人
            while (queue.size() < 2) {
                // 由于是notify_all，一般只唤醒一个线程，所以循环等待，若之后线程扩充，则无需修改代码
                queue.wait();
            }
            DLOG("匹配人数充足，开始匹配");
            // 2. 从队列中取出2个玩家
            uint64_t uid1, uid2;
            bool ret = queue.pop(uid1);
            if (ret == false) {
                DLOG("匹配队列为空，无法匹配");
                continue;
            }
            DLOG("匹配到玩家1[%ld]", uid1);
            ret = queue.pop(uid2);
            if (ret == false) {
                DLOG("匹配队列为空，无法匹配");
                // 由于uid1已经确认pop成功，所以需要重新入队列
                queue.push(uid1);
                continue;
            }
            DLOG("匹配到玩家2[%ld]", uid2);
            // 3. 检查玩家连接状态
            websocket_server::connection_ptr conn1 =
                _online_manager->get_game_hall_conn(uid1);
            websocket_server::connection_ptr conn2 =
                _online_manager->get_game_hall_conn(uid2);
            if (conn1 == nullptr) {
                // 若uid1连接异常，将uid2入队列
                queue.push(uid2);
                DLOG("玩家1[%ld]连接异常，将玩家2[%ld]入队列", uid1, uid2);
                continue;
            }
            if (conn2 == nullptr) {
                // 若uid2连接异常，将uid1入队列
                queue.push(uid1);
                DLOG("玩家2[%ld]连接异常，将玩家1[%ld]入队列", uid2, uid1);
                continue;
            }
            // 4. 创建房间
            auto room = _room_manager->create_room(uid1, uid2);
            if (room.get() == nullptr) {
                // 若创建房间失败，将uid1和uid2重新入队列
                queue.push(uid1);
                queue.push(uid2);
                continue;
            }
            DLOG("创建房间成功, room_id: %ld", room->get_room_id());
            // 5. 向用户发送匹配成功消息
            Json::Value msg;
            msg["type"] = "match_success";
            msg["result"] = true;
            std::string message;
            Json_Util::serialize(msg, message);
            conn1->send(message);
            conn2->send(message);
        }
    }
    // 铜级匹配线程入口函数
    void match_bronze_entry() { handle_match(_queue_bronze); }
    // 银级匹配线程入口函数
    void match_silver_entry() { handle_match(_queue_silver); }
    // 金级匹配线程入口函数
    void match_gold_entry() { handle_match(_queue_gold); }

  private:
    matcher_queue<uint64_t> _queue_bronze; // 铜级匹配队列
    matcher_queue<uint64_t> _queue_silver; // 银级匹配队列
    matcher_queue<uint64_t> _queue_gold;   // 金级匹配队列
    std::thread _thread_bronze;            // 铜级匹配线程
    std::thread _thread_silver;            // 银级匹配线程
    std::thread _thread_gold;              // 金级匹配线程
    room_manager *_room_manager;           // 房间管理器指针
    user_table *_user_table;               // 用户表指针
    online_manager *_online_manager;       // 在线管理器指针
};