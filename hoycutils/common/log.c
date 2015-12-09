#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <common/log.h>

static uint8_t curr_level = LOG_DEFAULT_LEVEL;

void log_init(int level)
{
	if(level > LOG_LEVEL_MAX || level < 0)

	curr_level = level;
}

char level_tags[LOG_LEVEL_MAX + 1] = {
	'F', 'E', 'W', 'I', 'D', 'V',
};

void log_printf(int level, const char *tag, const char *fmt, ...)
{
	va_list ap;
	int len;
	char buf[LOG_BUF_SIZE];

	if(level > curr_level || level < 0)
		return;

	len = snprintf(buf, LOG_BUF_SIZE, "%c/[%s] ", level_tags[level], tag);
	va_start(ap, fmt);
	vsnprintf(buf + len, LOG_BUF_SIZE - len, fmt, ap);
	va_end(ap);

	printf("%s", buf);
}

