#pragma once
#include <stdio.h>
#include <time.h>

#define INFO 0
#define DBG 1
#define ERR 2
#define DEFAULT_LOG_LEVEL INFO

#define LOG(level, format, ...)                                                \
    do {                                                                       \
        if (DEFAULT_LOG_LEVEL > level)                                         \
            break;                                                             \
        time_t tm = time(NULL);                                                \
        struct tm *tmp = localtime(&tm);                                       \
        char str[128];                                                         \
        strftime(str, 127, "%Y-%m-%d %H:%M:%S", tmp);                          \
        fprintf(stdout, "[%s %s:%d]" format "\n", str, __FILE__, __LINE__,     \
                ##__VA_ARGS__);                                                \
    } while (0)

#define ILOG(format, ...) LOG(INFO, "[INFO]# " format, ##__VA_ARGS__)
#define DLOG(format, ...) LOG(DBG, "[DEBUG]# " format, ##__VA_ARGS__)
#define ELOG(format, ...) LOG(ERR, "[ERROR]# " format, ##__VA_ARGS__)
