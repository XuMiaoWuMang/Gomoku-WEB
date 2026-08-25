/* =========================================================
 * Gomoku-WEB 服务端入口
 *
 * 配置优先级（高 → 低）：
 *   命令行参数 > .env（KEY=VALUE，存敏感项）> config.json（JSON）> 内置默认值
 *
 * 用法示例：
 *   ./gomoku_server
 *   ./gomoku_server --server-port=9090 --db-password=xxx
 *   ./gomoku_server --help
 * ========================================================= */
#include "src/server.hpp"
#include "src/util/file.hpp"
#include "src/util/json.hpp"
#include "src/util/logger.hpp"
#include "src/util/str.hpp"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

// 配置项（默认值）
struct Config {
    std::string db_user = "root";       // 数据库用户名
    std::string db_password = "";       // 数据库密码（默认空，建议通过 .env 配置）
    std::string db_name = "test";       // 数据库名
    std::string db_host = "localhost";  // 数据库主机
    int db_port = 3306;                 // 数据库端口
    std::string web_root = "./wwwroot"; // 静态资源目录
    int server_port = 8089;             // HTTP/WebSocket 服务端口
};

// 统一应用一个键值对（key 已归一化为小写、'_' 分隔）
void apply_key(Config &cfg, std::map<std::string, std::string> &src,
               const std::string &key, const std::string &val,
               const std::string &source) {
    if (key == "db_user")          { cfg.db_user = val;     src["db_user"] = source; }
    else if (key == "db_password") { cfg.db_password = val;  src["db_password"] = source; }
    else if (key == "db_name")     { cfg.db_name = val;      src["db_name"] = source; }
    else if (key == "db_host")     { cfg.db_host = val;      src["db_host"] = source; }
    else if (key == "db_port")     { cfg.db_port = std::atoi(val.c_str()); src["db_port"] = source; }
    else if (key == "web_root")    { cfg.web_root = val;     src["web_root"] = source; }
    else if (key == "server_port") { cfg.server_port = std::atoi(val.c_str()); src["server_port"] = source; }
}

// JSON Value 归一化为字符串（数字/布尔也转字符串）
std::string json_val_to_string(const Json::Value &v) {
    if (v.isString()) return v.asString();
    if (v.isInt())    return std::to_string(v.asInt());
    if (v.isUInt())   return std::to_string(v.asUInt());
    if (v.isInt64())  return std::to_string(v.asInt64());
    if (v.isUInt64()) return std::to_string(v.asUInt64());
    if (v.isBool())   return v.asBool() ? "true" : "false";
    return v.asString();
}

// 应用 config.json（JSON 格式）
void apply_json_config(Config &cfg, std::map<std::string, std::string> &src) {
    std::string body;
    if (!File_Util::read("config.json", body)) {
        DLOG("未找到 config.json，跳过该层配置");
        return;
    }
    Json::Value root;
    if (!Json_Util::deserialize(body, root) || !root.isObject()) {
        ELOG("config.json 解析失败，跳过该层配置");
        return;
    }
    static const char *keys[] = {"db_user", "db_password", "db_name", "db_host",
                                 "db_port", "web_root", "server_port"};
    for (const char *k : keys) {
        if (!root.isMember(k) || root[k].isNull()) continue;
        apply_key(cfg, src, k, json_val_to_string(root[k]), "config.json");
    }
    ILOG("已读取 config.json");
}

// 读取 .env：KEY=VALUE，支持 # 注释、空白与引号
bool load_env_file(const std::string &path, std::map<std::string, std::string> &out) {
    std::string body;
    if (!File_Util::read(path, body)) return false;
    std::vector<std::string> lines;
    Str_Util::split(body, "\n", lines);
    for (const auto &raw : lines) {
        std::string s = raw;
        size_t b = s.find_first_not_of(" \t\r");
        if (b == std::string::npos) continue;
        s = s.substr(b);
        if (s.empty() || s[0] == '#') continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string key = s.substr(0, eq);
        std::string val = s.substr(eq + 1);
        size_t ke = key.find_last_not_of(" \t");
        if (ke != std::string::npos) key = key.substr(0, ke + 1);
        size_t vb = val.find_first_not_of(" \t\r");
        size_t ve = val.find_last_not_of(" \t\r");
        if (vb == std::string::npos) {
            val = "";
        } else {
            val = val.substr(vb, ve - vb + 1);
        }
        if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') ||
                                (val.front() == '\'' && val.back() == '\''))) {
            val = val.substr(1, val.size() - 2);
        }
        for (auto &c : key) c = static_cast<char>(::tolower(c));
        out[key] = val;
    }
    return true;
}

// 应用 .env
void apply_env_config(Config &cfg, std::map<std::string, std::string> &src) {
    std::map<std::string, std::string> env;
    if (!load_env_file(".env", env)) {
        DLOG("未找到 .env，跳过该层配置");
        return;
    }
    static const char *keys[] = {"db_user", "db_password", "db_name", "db_host",
                                 "db_port", "web_root", "server_port"};
    for (const char *k : keys) {
        auto it = env.find(k);
        if (it != env.end()) apply_key(cfg, src, k, it->second, ".env");
    }
    ILOG("已读取 .env");
}

// 命令行参数
struct CmdArgs {
    std::map<std::string, std::string> kv;
    bool help = false;
};

bool parse_cmd_args(int argc, char *argv[], CmdArgs &out) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h") { out.help = true; return true; }
        if (a.size() < 3 || a[0] != '-' || a[1] != '-') {
            ELOG("无法识别的参数: %s", a.c_str());
            return false;
        }
        std::string body = a.substr(2);
        std::string key, val;
        size_t eq = body.find('=');
        if (eq != std::string::npos) {
            key = body.substr(0, eq);
            val = body.substr(eq + 1);
        } else {
            if (i + 1 >= argc) {
                ELOG("参数 %s 缺少值", a.c_str());
                return false;
            }
            key = body;
            val = argv[++i];
        }
        for (auto &c : key) {
            c = static_cast<char>(::tolower(c));
            if (c == '-') c = '_';
        }
        out.kv[key] = val;
    }
    return true;
}

void apply_cmd_args(Config &cfg, std::map<std::string, std::string> &src,
                    const CmdArgs &args) {
    for (const auto &p : args.kv) {
        apply_key(cfg, src, p.first, p.second, "命令行");
    }
}

void print_help() {
    std::cout
        << "Gomoku-WEB 服务端\n"
           "用法: gomoku_server [选项]\n"
           "配置优先级：命令行 > .env > config.json > 默认值\n"
           "  --db-user <name>        数据库用户名   (默认 root)\n"
           "  --db-password <pwd>     数据库密码     (默认空，建议放 .env)\n"
           "  --db-name <name>        数据库名       (默认 Gomoku)\n"
           "  --db-host <host>        数据库主机     (默认 localhost)\n"
           "  --db-port <port>        数据库端口     (默认 3306)\n"
           "  --web-root <dir>        静态资源目录   (默认 ./wwwroot)\n"
           "  --server-port <port>    服务端口       (默认 8089)\n"
           "  -h, --help              显示帮助\n";
}

} // namespace

int main(int argc, char *argv[]) {
    Config cfg;
    std::map<std::string, std::string> src; // 每个配置项的来源

    src["db_user"] = "默认值"; src["db_password"] = "默认值";
    src["db_name"] = "默认值"; src["db_host"] = "默认值";
    src["db_port"] = "默认值"; src["web_root"] = "默认值";
    src["server_port"] = "默认值";

    CmdArgs args;
    if (!parse_cmd_args(argc, argv, args)) {
        print_help();
        return 1;
    }
    if (args.help) {
        print_help();
        return 0;
    }

    // 按优先级叠加：config.json → .env → 命令行
    apply_json_config(cfg, src);
    apply_env_config(cfg, src);
    apply_cmd_args(cfg, src, args);

    // 打印配置来源与启动参数（密码打码，不打印明文）
    ILOG("配置来源: db_user[%s] db_password[%s] db_name[%s] db_host[%s] db_port[%s] web_root[%s] server_port[%s]",
         src["db_user"].c_str(), src["db_password"].c_str(), src["db_name"].c_str(),
         src["db_host"].c_str(), src["db_port"].c_str(), src["web_root"].c_str(),
         src["server_port"].c_str());
    ILOG("启动参数: db_host=%s db_port=%d db_user=%s db_name=%s web_root=%s server_port=%d",
         cfg.db_host.c_str(), cfg.db_port, cfg.db_user.c_str(), cfg.db_name.c_str(),
         cfg.web_root.c_str(), cfg.server_port);
    ILOG("数据库密码: %s", cfg.db_password.empty() ? "(空)" : "******");

    gomoku_server server(cfg.db_user, cfg.db_password, cfg.db_name,
                         cfg.web_root, cfg.db_host, cfg.db_port);
    server.start(cfg.server_port);
    return 0;
}
