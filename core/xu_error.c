#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "xu_impl.h"
#include "xu_kern.h"
#include "xu_io.h"
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

FILE *xu_log_open(struct xu_actor *ctx, uint32_t handle)
{
	const char * logpath = xu_getenv("logpath", NULL, 0);

	if (logpath == NULL) {
		return NULL;
	}
	size_t sz = strlen(logpath);
	char tmp[sz + 16];
	sprintf(tmp, "%s/%08x.log", logpath, handle);
	FILE *f = fopen(tmp, "ab");
	if (f) {
		uint32_t starttime = xu_starttime();
		uint64_t current = xu_now();
		time_t ti = starttime + current / 100;
		xu_error(ctx, "Open log file %s", tmp);
		fprintf(f, "open time: %u %s", (uint32_t)current, ctime(&ti));
		fflush(f);
	} else {
		xu_error(ctx, "Open log %s failed.", tmp);
	}

	return f;
}

void xu_log_close(struct xu_actor * ctx, FILE *f, uint32_t handle)
{
	xu_error(ctx, "Close log file :%08x", handle);
	fprintf(f, "close time: %u\n", (uint32_t)xu_now());
	fclose(f);
}

static void log_blob(FILE *f, const void * buffer, size_t sz)
{
	size_t i;
	const uint8_t * buf = buffer;
	for (i=0;i!=sz;i++) {
		fprintf(f, "%02x", buf[i]);
	}
}

static void log_io(FILE * f, const struct xu_io_event * message, size_t sz)
{
	size_t size = message->size & XIE_EVENT_MASK;
	fprintf(f, "[io] %d %d %d %d", (message->size) >> XIE_EVENT_SHIFT, message->fdesc, size, message->u.errcode);

	if (sz > 0) {
		const void *ud = message->data;
		log_blob(f, ud, sz);
	}
	fprintf(f, "\n");
	fflush(f);
}

void xu_log_output(FILE *f, uint32_t source, int type, const void * buffer, size_t sz)
{
	if (type == MTYPE_IO) {
		log_io(f, buffer, sz);
	} else {
		uint32_t ti = (uint32_t)xu_now();
		fprintf(f, ":%08x %d %u ", source, type, ti);
		log_blob(f, buffer, sz);
		fprintf(f,"\n");
		fflush(f);
	}
}

