cmake_minimum_required(VERSION 3.10)
project(gomoku_web LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- 依赖查找 ----
find_package(Boost REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)
find_path(WEBSOCKETPP_INCLUDE_DIR NAMES websocketpp/server.hpp websocketpp/config/asio_no_tls.hpp)
find_path(MYSQL_INCLUDE_DIR NAMES mysql/mysql.h)
find_library(MYSQLCLIENT_LIBRARY NAMES mysqlclient)
find_library(JSONCPP_LIBRARY NAMES jsoncpp)

if(NOT WEBSOCKETPP_INCLUDE_DIR)
    message(FATAL_ERROR "未找到 websocketpp 头文件，请安装 websocketpp；若安装在非标准目录，可用 -DWEBSOCKETPP_INCLUDE_DIR=/path/to/websocketpp 指定")
endif()
if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQLCLIENT_LIBRARY)
    message(FATAL_ERROR "未找到 MySQL 开发库（libmysqlclient）")
endif()
if(NOT JSONCPP_LIBRARY)
    message(FATAL_ERROR "未找到 jsoncpp 库")
endif()

include_directories(${WEBSOCKETPP_INCLUDE_DIR} ${MYSQL_INCLUDE_DIR})

# ---- 服务端入口 ----
add_executable(gomoku_server main.cpp)
target_link_libraries(gomoku_server PRIVATE
    ${Boost_LIBRARIES}
    Threads::Threads
    ${MYSQLCLIENT_LIBRARY}
    ${JSONCPP_LIBRARY}
)
