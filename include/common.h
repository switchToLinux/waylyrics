#ifndef WAYLYRICS_COMMON_H
#define WAYLYRICS_COMMON_H

// 日志级别定义
#define LOG_LEVEL_NONE   0
#define LOG_LEVEL_ERROR  1
#define LOG_LEVEL_WARN   2
#define LOG_LEVEL_DEBUG  3

// 根据编译参数设置日志级别
#if defined(DEBUG_ENABLED)
    #define LOG_LEVEL LOG_LEVEL_DEBUG
#elif defined(WARN_ENABLED)
    #define LOG_LEVEL LOG_LEVEL_WARN
#elif defined(ERROR_ENABLED)
    #define LOG_LEVEL LOG_LEVEL_ERROR
#else
    #define LOG_LEVEL LOG_LEVEL_NONE
#endif

// 通用日志宏
#define LOG_PRINT(level, tag, format, ...) \
    do { \
        if (LOG_LEVEL >= level) \
            fprintf(stderr, "[" tag "] %s:%d(%s): " format "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    } while(0)

// 各级别日志宏
#define DEBUG(format, ...)  LOG_PRINT(LOG_LEVEL_DEBUG, "DEBUG", format, ##__VA_ARGS__)
#define WARN(format, ...)   LOG_PRINT(LOG_LEVEL_WARN,  "WARN",  format, ##__VA_ARGS__)
#define ERROR(format, ...)  LOG_PRINT(LOG_LEVEL_ERROR, "ERROR", format, ##__VA_ARGS__)
#define INFO(format, ...)   LOG_PRINT(LOG_LEVEL_ERROR, "INFO",  format, ##__VA_ARGS__)

#endif // WAYLYRICS_COMMON_H
