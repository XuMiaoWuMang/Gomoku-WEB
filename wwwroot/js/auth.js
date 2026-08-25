/* =========================================================
 * Gomoku-WEB · 共享前端脚本 auth.js
 * 职责：
 *   1. 与后端约定的 REST 接口进行安全通信（/reg /login /user_info）
 *   2. 提供统一、XSS 安全的表单验证与消息反馈
 *   3. 提供 WebSocket 会话层：心跳保活 + 指数退避自动重连
 *   4. 前端输入白名单校验（降低注入面；服务端仍须自行加固）
 * 说明：后端 /login、/user_info 当前为空实现，fetch 将得到无实体/非 2xx
 *       响应，此处一律安全降级并给出友好提示，不影响页面正常可用。
 * ========================================================= */
(function (global) {
    'use strict';

    var Auth = global.Auth = {};

    /* ---------- 工具 ---------- */
    Auth.trim = function (v) { return typeof v === 'string' ? v.trim() : ''; };

    // XSS 安全的消息输出：一律用 textContent，杜绝 innerHTML 注入用户数据
    Auth.showMessage = function (el, text, type) {
        if (!el) return;
        el.classList.remove('success', 'error', 'info', 'visible');
        el.textContent = text || '';
        if (type) el.classList.add(type);
        if (text && text.length) el.classList.add('visible');
    };

    Auth.clearMessage = function (el) {
        if (!el) return;
        el.classList.remove('success', 'error', 'info', 'visible');
        el.textContent = '';
    };

    // HTTP 状态对应文案（不直接透传任何服务端原始 HTML）
    function statusText(status) {
        var m = {
            400: '请求格式有误，请检查输入', 401: '登录状态已失效，请重新登录',
            403: '没有操作权限', 404: '接口不存在',
            405: '请求方式不被支持', 500: '服务器内部错误，请稍后再试',
            503: '服务暂不可用，请稍后再试'
        };
        return m[status] || ('请求失败（HTTP ' + status + '）');
    }

    /**
     * 统一的安全 fetch 封装
     *  - 仅接受 JSON 响应，如内容无法解析则按状态码给出通用文案
     *  - 返回 { ok, status, result (bool), reason (string) }
     */
    Auth.fetchJSON = function (url, payload) {
        return fetch(url, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Accept': 'application/json',
                'X-Requested-With': 'XMLHttpRequest'
            },
            body: JSON.stringify(payload || {}),
            credentials: 'same-origin'
        }).then(function (resp) {
            return resp.text().then(function (raw) {
                var body = null;
                var contentType = (resp.headers.get('content-type') || '').toLowerCase();
                if (raw && (contentType.indexOf('json') >= 0)) {
                    try { body = JSON.parse(raw); } catch (e) { body = null; }
                }
                var result = resp.ok && body && body.result === true;
                var reason = '';
                if (body && typeof body.reason === 'string') reason = body.reason;
                if (!resp.ok && !reason) reason = statusText(resp.status);
                if (resp.ok && body && !reason) reason = '请求已完成';
                return { ok: resp.ok, status: resp.status, result: result, reason: reason, body: body };
            });
        }).catch(function () {
            // 网络层失败（连接被拒、断网等）
            return { ok: false, status: 0, result: false, reason: '无法连接服务器，请检查网络后重试' };
        });
    };

    /* ---------- 校验规则（前端白名单，服务端仍需二次校验） ---------- */
    Auth.RULES = {
        username: {
            pattern: /^[A-Za-z0-9_\u4e00-\u9fa5]{3,20}$/,
            test: function (v) {
                v = Auth.trim(v);
                if (!v) return { pass: false, msg: '请输入用户名' };
                if (!this.pattern.test(v)) return { pass: false, msg: '用户名需为 3-20 位，仅含字母、数字、下划线或中文' };
                return { pass: true, msg: '' };
            }
        },
        password: {
            test: function (v) {
                v = typeof v === 'string' ? v : '';
                if (!v) return { pass: false, msg: '请输入密码' };
                if (v.length < 6) return { pass: false, msg: '密码长度不能少于 6 位' };
                if (v.length > 64) return { pass: false, msg: '密码长度不能超过 64 位' };
                // 禁止明显异常的控制字符（防止服务端拼接异常等）
                if (/[\u0000-\u001f\u007f]/.test(v)) return { pass: false, msg: '密码包含非法字符' };
                return { pass: true, msg: '' };
            }
        },
        confirm: {
            test: function (v, pw) {
                v = typeof v === 'string' ? v : '';
                if (!v) return { pass: false, msg: '请再次输入密码' };
                if (v !== pw) return { pass: false, msg: '两次输入的密码不一致' };
                return { pass: true, msg: '' };
            }
        }
    };

    // 密码强度评估：0-4
    Auth.pwdStrength = function (v) {
        if (!v) return 0;
        var s = 0;
        if (v.length >= 6) s++;
        if (v.length >= 10) s++;
        if (/[a-z]/.test(v) && /[A-Z]/.test(v)) s++;
        if (/[0-9]/.test(v) && /[^A-Za-z0-9]/.test(v)) s++;
        return Math.max(1, Math.min(4, s));
    };

    Auth.strengthMeta = ['', 'weak', 'medium', 'strong', 'strong'];
    Auth.strengthColor = { weak: '#f59e0b', medium: '#f97316', strong: '#22c55e' };
    Auth.strengthText = ['', '弱', '中', '强', '很强'];

    /* ---------- 登录态与会话 ---------- */
    var SESSION_KEY = 'gomoku_session';

    Auth.saveSession = function (data) {
        try { sessionStorage.setItem(SESSION_KEY, JSON.stringify(data)); } catch (e) {}
    };
    Auth.getSession = function () {
        try { return JSON.parse(sessionStorage.getItem(SESSION_KEY) || 'null') || null; } catch (e) { return null; }
    };
    Auth.clearSession = function () {
        try { sessionStorage.removeItem(SESSION_KEY); } catch (e) {}
    };

    /**
     * WebSocket 会话管理器：心房跳保活 + 指数退避自动重连
     * opt: { onStatus(status), heartbeatMs, maxRetry }
     * status: connecting | connected | disconnected
     */
    Auth.WsSession = function (opt) {
        opt = opt || {};
        var self = this;
        this.heartbeatMs = opt.heartbeatMs || 20000;
        this.maxRetry = (opt.maxRetry != null) ? opt.maxRetry : Infinity;
        this.onStatus = opt.onStatus || function () {};
        this._attempts = 0;
        this._closedByUser = false;
        this._conn = null;
        this._hbTimer = null;
        this._reconnectTimer = null;
        this._url = opt.url || (function () {
            var proto = location.protocol === 'https:' ? 'wss://' : 'ws://';
            return proto + location.host;
        })();
    };

    Auth.WsSession.prototype.connect = function () {
        var self = this;
        this._closeConn();
        this._closedByUser = false;
        this._setStatus('connecting');

        var ws;
        try { ws = new WebSocket(this._url); }
        catch (e) { this._scheduleReconnect(); return; }
        this._conn = ws;

        ws.onopen = function () {
            self._attempts = 0;
            self._setStatus('connected');
            self._startHeartbeat();
        };
        ws.onmessage = function (e) {
            // 约定：所有消息为 JSON。这里仅用于维持会话心跳应答，不做业务处理。
            // 心跳应答（pong）可据此认为链路存活。
        };
        ws.onerror = function () {
            // 不改变状态，onclose 统一处理
        };
        ws.onclose = function () {
            self._stopHeartbeat();
            self._setStatus('disconnected');
            if (!self._closedByUser) self._scheduleReconnect();
        };
    };

    Auth.WsSession.prototype.ping = function () {
        try { if (this._conn && this._conn.readyState === WebSocket.OPEN) this._conn.send('ping'); } catch (e) {}
    };

    Auth.WsSession.prototype.close = function () {
        this._closedByUser = true;
        this._stopHeartbeat();
        this._clearReconnect();
        this._closeConn();
        this._setStatus('disconnected');
    };

    Auth.WsSession.prototype._closeConn = function () {
        this._stopHeartbeat();
        if (this._conn) {
            try { this._conn.onclose = null; this._conn.close(); } catch (e) {}
            this._conn = null;
        }
    };
    Auth.WsSession.prototype._startHeartbeat = function () {
        var self = this;
        this._stopHeartbeat();
        this._hbTimer = setInterval(function () { self.ping(); }, this.heartbeatMs);
    };
    Auth.WsSession.prototype._stopHeartbeat = function () {
        if (this._hbTimer) { clearInterval(this._hbTimer); this._hbTimer = null; }
    };
    Auth.WsSession.prototype._scheduleReconnect = function () {
        var self = this;
        this._clearReconnect();
        this._attempts++;
        if (this._attempts > this.maxRetry) return;
        var delay = Math.min(30000, 1000 * Math.pow(2, this._attempts - 1));
        this._reconnectTimer = setTimeout(function () { self.connect(); }, delay);
    };
    Auth.WsSession.prototype._clearReconnect = function () {
        if (this._reconnectTimer) { clearTimeout(this._reconnectTimer); this._reconnectTimer = null; }
    };
    Auth.WsSession.prototype._setStatus = function (s) {
        this._status = s;
        try { this.onStatus(s, this); } catch (e) {}
    };

})(window);