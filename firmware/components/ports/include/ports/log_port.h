#pragma once
#include <stdarg.h>
#ifdef __cplusplus
extern "C" { #endif
typedef enum { LOG_ERROR=0, LOG_WARN, LOG_INFO, LOG_DEBUG, LOG_VERBOSE } log_level_t;
void log_write(log_level_t lvl, const char* tag, const char* fmt, ...);
#define LOGE(tag, fmt, ...) log_write(LOG_ERROR,   tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) log_write(LOG_WARN,    tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) log_write(LOG_INFO,    tag, fmt, ##__VA_ARGS__)
#define LOGD(tag, fmt, ...) log_write(LOG_DEBUG,   tag, fmt, ##__VA_ARGS__)
#define LOGV(tag, fmt, ...) log_write(LOG_VERBOSE, tag, fmt, ##__VA_ARGS__)
#ifdef __cplusplus
} #endif
