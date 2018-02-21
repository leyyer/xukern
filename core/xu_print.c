#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "atomic.h"
#include "xu_malloc.h"
#include "xu_util.h"

#define LOG_MSG_SIZE (256)

static void fallback(const char *msg, int len)
{
	fprintf(stderr, "%s\n", msg);
	fflush(stderr);
}

static logger_t __printer = fallback;

logger_t xu_set_logger(logger_t f)
{
	logger_t saved = __printer;
	ATOM_CAS_POINTER(&__printer, saved, f);
	return saved;
}

int xu_println(const char *msg, ...) 
{
	char tmp[LOG_MSG_SIZE];
	char *data = NULL;
	va_list ap;
	int len;

	va_start(ap, msg);
	len = vsnprintf(tmp, sizeof tmp, msg, ap);
	va_end(ap);
	if (len >= 0 && len < sizeof tmp) {
		data = tmp;
	} else {
		int max_size = LOG_MSG_SIZE;
		for (;;) {
			max_size <<= 1; /* double */
			data = xu_malloc(max_size);
			va_start(ap, msg);
			len = vsnprintf(data, max_size, msg, ap);
			va_end(ap);
			if (len < max_size) {
				break;
			}
			xu_free(data);
		}
	}
	if (len < 0) {
		xu_free(data);
		perror("vsnprintf error: ");
		return -1;
	}

	if (__printer)
		__printer(msg, len);

	if (data != tmp) {
		xu_free(data);
	}
	return (len);
}

