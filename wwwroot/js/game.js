/* =========================================================
 * Gomoku-WEB · 游戏前端脚本 game.js
 * 职责：大厅页(/hall) 与对局页(/room) 的 WebSocket 通信与 UI 逻辑。
 * 契约对齐 src/server.hpp、src/room.hpp、src/matcher.hpp：
 *   - /hall : hall_ready / match_start / match_stop / match_success(type 字段)
 *   - /room : room_ready / put_chess / chat
 * 兼容后端怪癖（以后端代码为准）：
 *   - match_success 使用 type 字段且 result 为字符串，前端按 type 判断
 *   - /user_info 返回原始用户对象(无 result 字段)，此处单独解析
 *   - 聊天同时携带 message 与 msg（后端过滤 msg、广播 message）
 * ========================================================= */
(function (global) {
    'use strict';

    /* ---------- 通用工具 ---------- */
    function $(id) { return document.getElementById(id); }
    function nowTime() {
        var d = new Date();
        function p(n) { return (n < 10 ? '0' : '') + n; }
        return p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds());
    }
    function wsUrl(path) {
        var proto = location.protocol === 'https:' ? 'wss://' : 'ws://';
        return proto + location.host + path;
    }
    function parseMsg(raw) {
        try { return JSON.parse(raw); } catch (e) { return null; }
    }
    function isSessionError(reason) {
        if (!reason) return false;
        return reason.indexOf('会话') >= 0 || reason.indexOf('Cookie') >= 0 ||
               reason.indexOf('登录') >= 0 || reason.indexOf('重新访问') >= 0;
    }
    function showMsg(el, text, type) {
        if (!el) return;
        el.classList.remove('success', 'error', 'info', 'visible');
        el.textContent = text || '';
        if (type) el.classList.add(type);
        if (text) el.classList.add('visible');
    }
    function redirectLogin() {
        if (global.Auth) { try { Auth.clearSession(); } catch (e) {} }
        location.href = 'login.html';
    }

    /* ---------- 大厅页 ---------- */
    function initHall() {
        var badge = $('connBadge'), connText = $('connText');
        var nameEl = $('userName'), scoreEl = $('userScore');
        var msgArea = $('hallMsg');
        var btnStart = $('btnStart'), btnStop = $('btnStop');
        var matchStatus = $('matchStatus'), btnLogout = $('btnLogout');
        var ws = null, matching = false, openTries = 0;

        function setConn(state, text) {
            badge.classList.remove('connected', 'connecting', 'disconnected');
            badge.classList.add(state);
            connText.textContent = text;
        }
        function setMatching(on) {
            matching = on;
            if (btnStart) btnStart.disabled = on;
            if (btnStop) btnStop.disabled = !on;
            if (matchStatus) matchStatus.textContent = on ? '正在匹配对手…' : '';
        }

        // /user_info 直接返回用户对象（无 result 字段），失败则回退本地 sessionStorage
        function loadUser() {
            fetch('/user_info', { credentials: 'same-origin' })
                .then(function (r) { return r.text(); })
                .then(function (raw) {
                    var body = null;
                    try { body = JSON.parse(raw); } catch (e) {}
                    if (body && body.username) {
                        if (nameEl) nameEl.textContent = body.username;
                        if (scoreEl) scoreEl.textContent = '积分 ' + body.score;
                        return;
                    }
                    fallbackUser();
                })
                .catch(fallbackUser);
        }
        function fallbackUser() {
            var s = null;
            if (global.Auth) { try { s = Auth.getSession(); } catch (e) {} }
            s = s || {};
            if (s.name && nameEl) nameEl.textContent = s.name;
            if (scoreEl) scoreEl.textContent = '';
        }

        function connect() {
            ws = new WebSocket(wsUrl('/hall'));
            setConn('connecting', '正在连接大厅…');
            ws.onopen = function () { setConn('connected', '大厅已连接'); };
            ws.onmessage = function (e) {
                var msg = parseMsg(e.data);
                if (!msg) return;
                if (msg.optype === 'hall_ready') {
                    if (msg.result === true) {
                        openTries = 0;
                        setConn('connected', '大厅已连接');
                    } else {
                        setConn('disconnected', '连接被拒绝');
                        if (isSessionError(msg.reason)) {
                            showMsg(msgArea, msg.reason || '进入大厅失败', 'error');
                            setTimeout(redirectLogin, 1200);
                        } else if (msg.reason && msg.reason.indexOf('已登录') >= 0 && openTries < 3) {
                            // 刷新页面时旧连接可能尚未被服务端清理，稍后重连
                            openTries++;
                            showMsg(msgArea, '连接冲突，正在重连…', 'info');
                            setTimeout(function () { try { ws.close(); } catch (e2) {} connect(); }, 600);
                        } else {
                            showMsg(msgArea, msg.reason || '进入大厅失败', 'error');
                        }
                    }
                } else if (msg.optype === 'match_start') {
                    if (msg.result === true) {
                        setMatching(true);
                        showMsg(msgArea, '已进入匹配队列，请稍候…', 'info');
                    } else {
                        showMsg(msgArea, msg.reason || '开始匹配失败', 'error');
                    }
                } else if (msg.optype === 'match_stop') {
                    if (msg.result === true) {
                        setMatching(false);
                        showMsg(msgArea, '已取消匹配', 'info');
                    } else {
                        showMsg(msgArea, msg.reason || '取消失败', 'error');
                    }
                } else if (msg.type === 'match_success') {
                    setMatching(true);
                    showMsg(msgArea, '匹配成功，正在进入房间…', 'success');
                    if (btnStart) btnStart.disabled = true;
                    if (btnStop) btnStop.disabled = true;
                    // 先断开大厅，等服务端处理完退出大厅后再进房
                    try {
                        ws.onclose = function () { location.href = 'room.html'; };
                        ws.close();
                    } catch (e3) {
                        location.href = 'room.html';
                    }
                } else if (msg.optype === 'unknow') {
                    showMsg(msgArea, msg.reason || '未知操作', 'error');
                }
            };
            ws.onclose = function () {
                setConn('disconnected', '连接已断开');
            };
        }

        if (btnStart) btnStart.addEventListener('click', function () {
            if (!ws || ws.readyState !== WebSocket.OPEN) {
                showMsg(msgArea, '大厅连接未就绪', 'error');
                return;
            }
            ws.send(JSON.stringify({ optype: 'match_start' }));
        });
        if (btnStop) btnStop.addEventListener('click', function () {
            if (!ws || ws.readyState !== WebSocket.OPEN) return;
            ws.send(JSON.stringify({ optype: 'match_stop' }));
        });
        if (btnLogout) btnLogout.addEventListener('click', function () {
            if (ws) { try { ws.close(); } catch (e) {} }
            redirectLogin();
        });
        window.addEventListener('beforeunload', function () {
            if (ws) { try { ws.close(); } catch (e) {} }
        });

        loadUser();
        connect();
    }

    /* ---------- 对局页 ---------- */
    function initRoom() {
        var badge = $('connBadge'), connText = $('connText');
        var roomInfoEl = $('roomInfo'), turnText = $('turnText');
        var msgArea = $('roomMsg');
        var boardEl = $('board'), chatList = $('chatList');
        var chatInput = $('chatInput'), btnChat = $('btnChat');
        var btnExit = $('btnExit'), btnBackHall = $('btnBackHall'), btnReconnect = $('btnReconnect');
        var overlayEl = $('overlay'), overlayTitle = $('overlayTitle'), overlaySub = $('overlaySub');

        var ws = null, leaving = false;
        var roomId = 0, selfId = 0, whiteId = 0, blackId = 0, myColor = null;
        var turn = 'black'; // 后端 _turn=1 起，黑棋先手
        var finished = false, boardEnabled = false;
        var board = [];
        var chatPending = false, chatPendingText = '';
        var moveTimer = null;          // 落子响应超时计时器
        var backTarget = 'hall.html';  // 结算/错误遮罩的返回目标

        function colorName(c) { return c === 'white' ? '白棋' : '黑棋'; }
        function colorOf(uid) { return uid === whiteId ? 'white' : 'black'; }

        function setConn(state, text) {
            badge.classList.remove('connected', 'connecting', 'disconnected');
            badge.classList.add(state);
            connText.textContent = text;
        }

        function updateTurnText() {
            if (finished) { turnText.textContent = '对局结束'; return; }
            if (!myColor) { turnText.textContent = '正在进入房间…'; return; }
            if (!boardEnabled) { turnText.textContent = '等待对手加入…'; return; }
            if (turn === myColor) {
                turnText.textContent = '轮到你落子（' + colorName(myColor) + '）';
            } else {
                turnText.textContent = '等待对方落子（对方执' + colorName(turn) + '）';
            }
        }
        function setTurn(t) { turn = t; updateTurnText(); }

        function buildBoard() {
            board = [];
            var frag = document.createDocumentFragment();
            for (var r = 0; r < 15; r++) {
                board[r] = [];
                for (var c = 0; c < 15; c++) {
                    board[r][c] = 0;
                    var cell = document.createElement('div');
                    cell.className = 'board-cell';
                    cell.setAttribute('role', 'gridcell');
                    cell.setAttribute('data-row', r);
                    cell.setAttribute('data-col', c);
                    if ((r === 3 && c === 3) || (r === 3 && c === 11) ||
                        (r === 11 && c === 3) || (r === 11 && c === 11) ||
                        (r === 7 && c === 7)) {
                        cell.classList.add('star');
                    }
                    frag.appendChild(cell);
                }
            }
            boardEl.innerHTML = '';
            boardEl.appendChild(frag);
        }

        function placeStone(row, col, color) {
            board[row][col] = color === 'white' ? 1 : 2;
            var prev = boardEl.querySelector('.last-move');
            if (prev) prev.classList.remove('last-move');
            var cell = boardEl.querySelector('[data-row="' + row + '"][data-col="' + col + '"]');
            if (!cell) return;
            var stone = document.createElement('span');
            stone.className = 'stone ' + color;
            cell.appendChild(stone);
            cell.classList.add('last-move');
        }

        function enableBoard() {
            if (finished || !myColor) return;
            boardEnabled = true;
            if (turn === myColor) {
                showMsg(msgArea, '对局开始，' + colorName(myColor) + '先手，轮到你落子', 'info');
            } else {
                showMsg(msgArea, '对局开始，等待对方落子', 'info');
            }
            updateTurnText();
        }

        function finishGame(msg) {
            finished = true;
            boardEnabled = false;
            if (moveTimer) { clearTimeout(moveTimer); moveTimer = null; }
            if (btnReconnect) btnReconnect.style.display = 'none';
            var winner = msg.winner;
            var title = '', sub = '';
            if (winner === 0) {
                title = '平局';
                sub = msg.reason || '棋盘已满，未分胜负';
            } else if (winner === selfId) {
                title = '你赢了';
                sub = msg.reason || '五星连珠，精彩对局！';
            } else {
                title = '你输了';
                sub = msg.reason || '再接再厉';
            }
            overlayTitle.textContent = title;
            overlaySub.textContent = sub;
            overlayEl.classList.add('show');
            updateTurnText();
        }

        // 重进房间时重置本地对局状态（服务端无状态同步接口，棋盘按空盘重建）
        function resetRoomState() {
            finished = false;
            boardEnabled = false;
            turn = 'black';
            buildBoard();
            updateTurnText();
        }

        // 致命错误：显示遮罩并给出返回目标，不再静默跳转
        function showFatalError(title, sub) {
            if (btnReconnect) btnReconnect.style.display = 'none';
            overlayTitle.textContent = title;
            overlaySub.textContent = sub;
            overlayEl.classList.add('show');
        }

        // 非主动断开：提示 + 重连/返回选项，不再只报错
        function showDisconnected() {
            setConn('disconnected', '连接已断开');
            if (leaving) { location.href = 'hall.html'; return; }
            if (finished) return;
            overlayTitle.textContent = '连接已断开';
            overlaySub.textContent = '与服务器的连接中断，请重新连接或返回大厅。';
            if (btnReconnect) btnReconnect.style.display = '';
            overlayEl.classList.add('show');
        }

        // 用户主动重连
        function reconnect() {
            overlayEl.classList.remove('show');
            if (ws) { try { ws.onclose = null; ws.close(); } catch (e) {} }
            connect();
        }

        function appendChat(text, who) {
            var item = document.createElement('div');
            item.className = 'chat-item' + (who === 'me' ? ' me' : '');
            item.appendChild(document.createTextNode(text));
            var t = document.createElement('span');
            t.className = 't';
            t.textContent = nowTime();
            item.appendChild(t);
            chatList.appendChild(item);
            chatList.scrollTop = chatList.scrollHeight;
        }

        function sendChat() {
            var text = (chatInput.value || '').trim();
            if (!text) return;
            if (!roomId || !ws || ws.readyState !== WebSocket.OPEN) {
                showMsg(msgArea, '房间连接未就绪', 'error');
                return;
            }
            chatPending = true;
            chatPendingText = text;
            chatInput.value = '';
            // message 用于广播展示，msg 用于服务端敏感词过滤
            ws.send(JSON.stringify({ optype: 'chat', room_id: roomId, message: text, msg: text }));
        }

        function onPutChess(msg) {
            if (moveTimer) { clearTimeout(moveTimer); moveTimer = null; }
            if (msg.result === true) {
                var row = msg.row, col = msg.col;
                var color = colorOf(msg.uid);
                if (row >= 0 && row < 15 && col >= 0 && col < 15) {
                    placeStone(row, col, color);
                }
                if (msg.finished === true) {
                    finishGame(msg);
                } else {
                    setTurn(turn === 'black' ? 'white' : 'black');
                }
            } else {
                // 仅发送方会收到失败响应；按约定只统计成功落子推进回合
                showMsg(msgArea, '落子被拒绝：' + (msg.reason || '未知原因'), 'error');
            }
        }

        function onMessage(msg) {
            if (!msg) return;
            if (msg.optype === 'room_ready') {
                if (msg.result === true) {
                    resetRoomState();
                    roomId = msg.room_id;
                    selfId = msg.self_id;
                    whiteId = msg.white_id;
                    blackId = msg.black_id;
                    myColor = selfId === whiteId ? 'white' : 'black';
                    roomInfoEl.textContent = '房间 ' + roomId + ' · 你执' + colorName(myColor);
                    showMsg(msgArea, '已进入房间，对局即将开始…', 'info');
                    // 立即启用棋盘（已按需求移除 3 秒等待）
                    enableBoard();
                    updateTurnText();
                } else {
                    // 修复：不再自断连接、不再静默跳转，改为明确提示 + 用户操作
                    var failReason = msg.reason || '进入房间失败';
                    if (isSessionError(failReason)) {
                        backTarget = 'login.html';
                        showFatalError('无法进入房间', failReason);
                    } else {
                        backTarget = 'hall.html';
                        if (failReason.indexOf('已存在连接') >= 0) {
                            showFatalError('无法进入房间', failReason + '，请返回大厅后重新进入');
                        } else {
                            showFatalError('无法进入房间', failReason);
                        }
                    }
                }
            } else if (msg.optype === 'put_chess') {
                onPutChess(msg);
            } else if (msg.optype === 'chat') {
                if (msg.result === true) {
                    if (chatPending && msg.message === chatPendingText) {
                        appendChat(msg.message, 'me');
                        chatPending = false;
                        chatPendingText = '';
                    } else {
                        appendChat(msg.message, 'other');
                    }
                } else {
                    chatPending = false;
                    chatPendingText = '';
                    showMsg(msgArea, msg.reason || '消息发送失败', 'error');
                }
            } else if (msg.optype === 'unknow') {
                showMsg(msgArea, msg.reason || '未知操作', 'error');
            }
        }

        function connect() {
            ws = new WebSocket(wsUrl('/room'));
            setConn('connecting', '正在连接房间…');
            ws.onopen = function () { setConn('connected', '房间已连接'); };
            ws.onmessage = function (e) { onMessage(parseMsg(e.data)); };
            ws.onclose = showDisconnected;
        }

        boardEl.addEventListener('click', function (ev) {
            var cell = ev.target && ev.target.closest ? ev.target.closest('.board-cell') : null;
            if (!cell || finished) return; // 对局结束由结算遮罩提示
            if (!myColor || !boardEnabled) {
                showMsg(msgArea, '棋盘尚未就绪，请等待房间连接完成', 'error');
                return;
            }
            if (turn !== myColor) {
                showMsg(msgArea, '还没轮到你落子', 'error');
                return;
            }
            var row = parseInt(cell.getAttribute('data-row'), 10);
            var col = parseInt(cell.getAttribute('data-col'), 10);
            if (board[row][col] !== 0) {
                showMsg(msgArea, '该位置已有棋子', 'error');
                return;
            }
            if (!ws || ws.readyState !== WebSocket.OPEN) {
                showMsg(msgArea, '连接已断开，无法落子', 'error');
                return;
            }
            if (moveTimer) clearTimeout(moveTimer);
            moveTimer = setTimeout(function () {
                moveTimer = null;
                showMsg(msgArea, '落子未收到服务端响应，请检查连接', 'error');
            }, 6000);
            ws.send(JSON.stringify({ optype: 'put_chess', room_id: roomId, uid: selfId, row: row, col: col }));
        });

        btnChat.addEventListener('click', sendChat);
        chatInput.addEventListener('keydown', function (e) {
            if (e.key === 'Enter') { e.preventDefault(); sendChat(); }
        });

        btnExit.addEventListener('click', function () {
            if (finished) {
                leaving = true;
                try { if (ws) ws.close(); } catch (e) {}
                location.href = 'hall.html';
                return;
            }
            if (!window.confirm('确定离开房间吗？对局中离开将判对方获胜。')) return;
            leaving = true;
            try { if (ws) ws.close(); } catch (e) {}
            location.href = 'hall.html';
        });
        if (btnBackHall) btnBackHall.addEventListener('click', function () {
            location.href = backTarget || 'hall.html';
        });
        if (btnReconnect) btnReconnect.addEventListener('click', reconnect);

        window.addEventListener('beforeunload', function () {
            if (ws) { try { ws.close(); } catch (e) {} }
        });

        buildBoard();
        updateTurnText();
        connect();
    }

    global.GomokuGame = { initHall: initHall, initRoom: initRoom };
})(window);
