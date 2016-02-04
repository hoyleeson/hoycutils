#ifndef _COMMON_LOG_H_
#define _COMMON_LOG_H_

#define LOG_FATAL 		(0)
#define LOG_ERROR 		(1)
#define LOG_WARNING 	(2)
#define LOG_INFO 		(3)
#define LOG_DEBUG 		(4)
#define LOG_VERBOSE 	(5)
#define LOG_LEVEL_MAX  	LOG_VERBOSE

#define LOG_DEFAULT_LEVEL 	LOG_VERBOSE

#define LOG_TAGS 	"hoycutils"

#define LOG_BUF_SIZE 	(1024)

void log_printf(int level, const char *tag, const char *fmt, ...);

#define LOG_PRINT(l, ...) 	log_printf(l, LOG_TAGS, __VA_ARGS__)

//#define VDEBUG

#ifdef VDEBUG
#define logv(...) 		LOG_PRINT(LOG_VERBOSE, __VA_ARGS__)
#undef DDEBUG
#define DDEBUG
#undef LOGINFO
#define LOGINFO
#else
#define logv(...)
#endif

#ifdef DDEBUG
#define logd(...) 		LOG_PRINT(LOG_DEBUG, __VA_ARGS__)
#undef LOGINFO
#define LOGINFO
#else
#define logd(...)
#endif

#ifdef LOGINFO
#define logi(...) 		LOG_PRINT(LOG_INFO, __VA_ARGS__)
#else
#define logi(...)
#endif

#define logw(...) 		LOG_PRINT(LOG_WARNING, __VA_ARGS__)

#define loge(...) 		LOG_PRINT(LOG_ERROR, __VA_ARGS__)

#define fatal(...) 		do { LOG_PRINT(LOG_FATAL, __VA_ARGS__); exit(1); } while(0)

#define panic(...) 		fatal(__VA_ARGS__);

#endif
