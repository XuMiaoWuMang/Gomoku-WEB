// 测试用 Mock 后端：仅用于前端功能/安全测试，镜像真实 C++ 后端的响应契约。
// 真实后端位于 src/server.hpp，本文件不属于生产部署的一部分。
import http from 'node:http';
import { readFile } from 'node:fs/promises';
import { extname, join, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = normalize(fileURLToPath(new URL('./wwwroot', import.meta.url)));
const PORT = process.env.PORT || 8099;

const MIME = {
    '.html': 'text/html; charset=utf-8',
    '.css': 'text/css; charset=utf-8',
    '.js': 'application/javascript; charset=utf-8',
    '.json': 'application/json; charset=utf-8',
    '.png': 'image/png', '.jpg': 'image/jpeg', '.ico': 'image/x-icon',
};

function json(res, status, obj) {
    res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8' });
    res.end(JSON.stringify(obj));
}

async function readBody(req) {
    return new Promise((resolve) => {
        let data = '';
        req.on('data', (c) => { data += c; if (data.length > 1e6) req.destroy(); });
        req.on('end', () => resolve(data));
        req.on('error', () => resolve(''));
    });
}

const server = http.createServer(async (req, res) => {
    const url = new URL(req.url, 'http://x');
    const path = decodeURIComponent(url.pathname);
    const method = req.method;

    // ---- 注册接口：镜像 server.hpp::reg 的行为与返回契约 ----
    if (method === 'POST' && path === '/reg') {
        const body = await readBody(req);
        let root = null;
        try { root = JSON.parse(body); } catch (e) { root = null; }
        if (!root) return json(res, 400, { result: false, reason: '未知格式请求体' });
        if (root.username == null || root.password == null)
            return json(res, 400, { result: false, reason: '用户名或密码为空' });
        // 真实后端 insert_user 依赖 name 字段
        if (root.name == null)
            return json(res, 400, { result: false, reason: '用户名或密码为空' });
        if (root.name === 'admin' || root.name === 'taken')
            return json(res, 200, { result: false, reason: '用户名已存在' });
        return json(res, 200, { result: true, reason: '注册成功' });
    }

    // ---- 登录接口：镜像 login 契约。admin/123456 为成功用例 ----
    if (method === 'POST' && path === '/login') {
        const body = await readBody(req);
        let root = null;
        try { root = JSON.parse(body); } catch (e) { root = null; }
        if (!root || root.username == null || root.password == null)
            return json(res, 400, { result: false, reason: '用户名或密码为空' });
        // XSS 探测：当密码为固定标记时，成功 reason 中塞入恶意脚本，
        // 用于验证前端 textContent 渲染对脚本的免疫。
        if (root.name === 'xss' && root.password === 'xss_mark')
            return json(res, 200, { result: true, reason: '登录成功:<img src=x onerror=alert(1)>' });
        if (root.name === 'admin' && root.password === '123456')
            return json(res, 200, { result: true, reason: '登录成功' });
        return json(res, 200, { result: false, reason: '用户名或密码错误' });
    }

    // ---- 用户信息接口 ----
    if (method === 'GET' && path === '/user_info')
        return json(res, 200, { result: true, reason: 'ok', name: 'test', score: 1000 });

    // ---- 静态资源（模拟 web_root 行为） ----
    let rel = path === '/' ? '/index.html' : path;
    let fp = normalize(join(ROOT, rel));
    if (!fp.startsWith(ROOT)) return json(res, 403, { result: false, reason: 'forbidden' });
    try {
        const data = await readFile(fp);
        res.writeHead(200, { 'Content-Type': MIME[extname(fp)] || 'application/octet-stream' });
        res.end(data);
    } catch (e) {
        res.writeHead(404, { 'Content-Type': 'text/html; charset=utf-8' });
        res.end('<h1>404 Not Found</h1>');
    }
});

server.listen(PORT, () => console.log('mock backend on http://127.0.0.1:' + PORT));