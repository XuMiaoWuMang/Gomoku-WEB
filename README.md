# Gomoku-WEB

基于websocket实现的多人五子棋在线对战网页

## 环境依赖

1. boost
2. cmake
3. git
4. mysql
5. Websocketpp

## 构建与运行
为确保数据持久化模块的正常运行，建议在项目根目录下执行以下命令：

```bash
mysql -u<username> -p < db.sql
```

此举是为了创建数据库表结构，如果日志报错 `mysql_real_connect failed` ，说明MySQL数据库未建表，其中的 `<username>` 为MySQL用户名，请自行替换。

```bash
mkdir -p build && cd build
cmake ..
make
# 回到项目根目录运行（.env / config.json 从当前工作目录读取）
cd ..
./build/gomoku_server
```

## 配置

配置优先级：**命令行 > .env > config.json > 默认值**

- `.env`（KEY=VALUE，存放敏感项如数据库密码；已被 .gitignore 忽略，参考 `.env.example`）
- `config.json`（JSON 结构化配置；已被 .gitignore 忽略，参考 `config.example.json`）

可用配置项：`db_user`、`db_password`、`db_name`、`db_host`、`db_port`、`web_root`、`server_port`

命令行示例：

```bash
./build/gomoku_server --server-port=9090 --db-password=xxx --web-root=./wwwroot
```