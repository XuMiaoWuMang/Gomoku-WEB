#pragma once
#include "online.hpp"

typedef enum { IS_LOGIN, NOT_LOGIN } session_status;

class session {
  public:
    session(uint64_t session_id) : _session_id(session_id) {
        DLOG("SESSION %lu 创建成功。", session_id);
    }
    session(uint64_t session_id, session_status status)
        : _session_id(session_id), _status(status) {
        // 状态为0表示未登录, 1表示已登录
        DLOG("SESSION %lu 创建成功", session_id);
    }

    ~session() { DLOG("SESSION %lu 销毁成功。", _session_id); }
    // 获取会话ID
    uint64_t session_id() { return _session_id; }
    // 设置用户ID
    void set_user_id(uint64_t user_id) { _user_id = user_id; }
    // 获取用户ID
    uint64_t get_user_id() { return _user_id; }
    // 获取会话状态
    session_status get_status() { return _status; }
    // 设置会话状态
    void set_status(session_status status) { _status = status; }
    // 判断是否登录
    bool is_login() { return _status == IS_LOGIN; }
    // 设置定时器
    void set_timer(websocket_server::timer_ptr timer) { _timer = timer; }
    // 获取定时器
    websocket_server::timer_ptr get_timer() { return _timer; }

  private:
    uint64_t _session_id;
    uint64_t _user_id = 0;
    session_status _status = NOT_LOGIN;
    websocket_server::timer_ptr _timer = nullptr;
};

#define SESSION_TIMEOUT 30000
#define SESSION_FOREVER -1
using session_ptr = std::shared_ptr<session>;
class session_manager {
  public:
    session_manager(websocket_server *server) : _server(server) {
        DLOG("SESSION_MANAGER 创建成功。");
    }
    ~session_manager() { DLOG("SESSION_MANAGER 销毁成功。"); }
    // 创建会话
    session_ptr create_session(uint64_t uid) {
        std::lock_guard<std::mutex> lock(_mutex);
        session_ptr ssp = std::make_shared<session>(_next_session_id);
        _sessions.insert({_next_session_id, ssp});
        _next_session_id++;
        ssp->set_user_id(uid);
        return ssp;
    }
    session_ptr create_session(uint64_t uid, session_status status) {
        std::lock_guard<std::mutex> lock(_mutex);

        session_ptr ssp = std::make_shared<session>(_next_session_id, status);
        _sessions.insert({_next_session_id, ssp});
        _next_session_id++;
        ssp->set_user_id(uid);
        return ssp;
    }
    // 添加会话
    void add_session(const session_ptr &session) {
        std::lock_guard<std::mutex> lock(_mutex);
        _sessions.insert({session->session_id(), session});
    }
    // 获取会话
    session_ptr get_session(uint64_t session_id) {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _sessions.find(session_id);
        if (it == _sessions.end()) {
            return nullptr;
        }
        return it->second;
    }
    // 删除会话
    void remove_session(uint64_t session_id) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (find_session(session_id)) {
            _sessions.erase(session_id);
        }
    }
    // 设置定时器时间
    void set_session_timeout(uint64_t session_id, uint64_t ms) {
        session_ptr session = get_session(session_id);
        if (!session) {
            return;
        }
        websocket_server::timer_ptr timer = session->get_timer();
        if (timer == nullptr && ms == SESSION_FOREVER) {
            // 1. session本身没有定时器 且 需要永久存在
            // 无需设置定时器
            return;
        } else if (timer == nullptr && ms != SESSION_FOREVER) {
            // 2. session本身没有定时器 且 需要定时删除
            // 设置定时器
            websocket_server::timer_ptr tmp_timer = _server->set_timer(
                ms,
                std::bind(&session_manager::remove_session, this, session_id));
            session->set_timer(tmp_timer);
            return;
        } else if (timer != nullptr && ms == SESSION_FOREVER) {
            // 3. session本身自带定时器 且 需要永久存在
            // 取消定时器(会导致本身的定时删除任务立即执行)
            timer->cancel();
            // 清空定时器，就不会定时删除
            session->set_timer(websocket_server::timer_ptr());
            // 获取timer_ptr,
            // 设置为立即执行添加，防止因为异步操作，导致删除和添加操作顺序错误
            _server->set_timer(
                0, std::bind(&session_manager::add_session, this, session));
            return;
        } else if (timer != nullptr && ms != SESSION_FOREVER) {
            // 4. session本身自带定时器 且 需要定时删除
            // 取消定时器(会导致本身的定时删除任务立即执行)
            timer->cancel();
            // 设置为立即执行添加，防止因为异步操作，导致删除和添加操作顺序错误
            _server->set_timer(
                0, std::bind(&session_manager::add_session, this, session));
            // 获取timer_ptr,
            websocket_server::timer_ptr tmp_timer = _server->set_timer(
                ms,
                std::bind(&session_manager::remove_session, this, session_id));
            // 将定时删除任务交给session的定时器保存
            session->set_timer(tmp_timer);
            return;
        }
    }

  private:
    bool find_session(uint64_t session_id) {
        auto it = _sessions.find(session_id);
        if (it == _sessions.end()) {
            return false;
        }
        return true;
    }

  private:
    uint64_t _next_session_id = 1;
    std::mutex _mutex;
    std::map<uint64_t, session_ptr> _sessions;
    websocket_server *_server = nullptr;
};
