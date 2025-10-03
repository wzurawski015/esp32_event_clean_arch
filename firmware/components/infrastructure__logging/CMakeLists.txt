#include "ports/log_port.h"
#include "esp_log.h"
void log_write(log_level_t lvl, const char* tag, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    switch (lvl) {
    case LOG_ERROR:   esp_log_writev(ESP_LOG_ERROR,   tag, fmt, ap); break;
    case LOG_WARN:    esp_log_writev(ESP_LOG_WARN,    tag, fmt, ap); break;
    case LOG_INFO:    esp_log_writev(ESP_LOG_INFO,    tag, fmt, ap); break;
    case LOG_DEBUG:   esp_log_writev(ESP_LOG_DEBUG,   tag, fmt, ap); break;
    default:          esp_log_writev(ESP_LOG_VERBOSE, tag, fmt, ap); break;
    }
    va_end(ap);
}
