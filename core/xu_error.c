#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "xu_impl.h"
#include "xu_kern.h"
#include "xu_util.h"

#define LOG_MESSAGE_SIZE 256

void xu_error(struct xu_actor * context, const char *msg, ...)
{
	static uint32_t logger = 0;
	if (logger == 0) {
		logger = xu_actor_findname("logger");
	}
	if (logger == 0) {
		return;
	}

	char tmp[LOG_MESSAGE_SIZE];
	char *data = NULL;

	va_list ap;

	va_start(ap,msg);
	int len = vsnprintf(tmp, LOG_MESSAGE_SIZE, msg, ap);
	va_end(ap);
	if (len >=0 && len < LOG_MESSAGE_SIZE) {
		data = xu_strdup(tmp);
	} else {
		int max_size = LOG_MESSAGE_SIZE;
		for (;;) {
			max_size *= 2;
			data = xu_malloc(max_size);
			va_start(ap,msg);
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
		perror("vsnprintf error :");
		return;
	}

	struct xu_msg smsg;
	if (context == NULL) {
		smsg.source = 0;
	} else {
		smsg.source = xu_actor_handle(context);
	}
	smsg.data = data;
	smsg.sz = len | ((size_t)MTYPE_LOG << MESSAGE_TYPE_SHIFT);
	xu_handle_msgput(logger, &smsg);
}

