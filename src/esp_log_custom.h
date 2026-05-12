#ifndef ESP_LOG_CUSTOM_H
#define ESP_LOG_CUSTOM_H

// NOTES:
//
//   USE esp_log_custom.h IN PLACE OF esp_log.h
//
//   1) this must be included in platformio.ini
//       build_flags = -D CORE_DEBUG_LEVEL=3 -D USE_ESP_IDF_LOG
//
//   2) in main.cpp, before setup(), include the following:
//           #include "myesp_log.h"
//
//   3) in setup():
//           // Set global log level to to some level initially
//           esp_log_level_set("*", ESP_LOG_WARN);
//
//      then in subsequent code, you can use lines like the following to change the log level for each function, such as...
//           esp_log_level_set(TAG, ESP_LOG_ERROR);

#include <esp_log.h>

#undef ESP_LOGE
#undef ESP_LOGW
#undef ESP_LOGI
#undef ESP_LOGD
#undef ESP_LOGV

#define ESP_LOGE( tag, format, ... ) ESP_LOG_LEVEL_LOCAL(ESP_LOG_ERROR,   tag, CUSTOM_LOG_FORMAT(E, format) __VA_OPT__(,) __VA_ARGS__)
#define ESP_LOGW( tag, format, ... ) ESP_LOG_LEVEL_LOCAL(ESP_LOG_WARN,    tag, CUSTOM_LOG_FORMAT(W, format) __VA_OPT__(,) __VA_ARGS__)
#define ESP_LOGI( tag, format, ... ) ESP_LOG_LEVEL_LOCAL(ESP_LOG_INFO,    tag, CUSTOM_LOG_FORMAT(I, format) __VA_OPT__(,) __VA_ARGS__)
#define ESP_LOGD( tag, format, ... ) ESP_LOG_LEVEL_LOCAL(ESP_LOG_DEBUG,   tag, CUSTOM_LOG_FORMAT(D, format) __VA_OPT__(,) __VA_ARGS__)
#define ESP_LOGV( tag, format, ... ) ESP_LOG_LEVEL_LOCAL(ESP_LOG_VERBOSE, tag, CUSTOM_LOG_FORMAT(V, format) __VA_OPT__(,) __VA_ARGS__)

#define CUSTOM_LOG_FORMAT(letter, format)				\
  "[%s:%u] %s(): " format ,  \
    __FILE__, __LINE__, __FUNCTION__

#endif // ESP_LOG_CUSTOM_H