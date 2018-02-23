#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "atomic.h"
#include "xu_malloc.h"
#include "xu_util.h"

#define LOG_MSG_SIZE (256)

static logger_t __printer  = NULL;
static void *   __log_data = NULL;

static void __do_print(const char *msg, int len)
{
	if (__printer) {
		__printer(__log_data, msg, len);
		__printer(__log_data, "\n", 1);
	} else {
		fprintf(stderr, "%s\n", msg);
		fflush(stderr);
	}
}

void xu_set_logger(void *ud, logger_t f)
{
	logger_t saved = __printer;
	void *ds = __log_data;

	ATOM_CAS_POINTER(&__printer, saved, f);
	ATOM_CAS_POINTER(&__log_data, ds, ud);
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

	__do_print(data, len);

	if (data != tmp) {
		xu_free(data);
	}
	return (len);
}

int xu_log_blob(void *buf, int len)
{
	char tmp[LOG_MSG_SIZE] = {0};
	char item[8] = {0};
	const char *p = buf;
	int i = 0;

	while (i < len) {
		sprintf(item, "%02x ", p[i]);
		strcat(tmp, item);
		++i;
		if ((i % 16) == 0) {
			__do_print(tmp, strlen(tmp));
			tmp[0] = '\0';
		}
	}

	__do_print(tmp, strlen(tmp));

	return (i);
}

