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
	smsg.type = MTYPE_LOG;
	smsg.size = len;
	xu_handle_msgput(logger, &smsg);
}

FILE *xu_log_open(struct xu_actor *ctx, const char *p, const char *def)
{
	const char * logpath = xu_getenv("logpath", NULL, 0);
	uint32_t handle;

	if (logpath == NULL) {
		logpath = ".";
	}
	char tmp[BUFSIZ] = {0};

	if (p && p[0] != '\0') {
		sprintf(tmp, "%s/%s.log", logpath, p);
	} else if (def && def[0] != '\0') {
		sprintf(tmp, "%s/%s.log", logpath, def);
	} else {
		handle = xu_actor_handle(ctx);
		sprintf(tmp, "%s/%08x.log", logpath, handle);
	}
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

static int log_blob(FILE *f, const void * buffer, size_t sz)
{
	size_t i;
	const uint8_t * buf = buffer;
	int eol = 0;

	for (i = 0; i < sz; i++) {
		eol = (i + 1) % 8  == 0;
		fprintf(f, "%02x%c", buf[i], eol ? '\n' : ' ');
	}
	return eol;
}

static void log_io(FILE * f, const struct xu_io_event * message, size_t sz)
{
	int eol = 0;
	size_t size = message->size;
	fprintf(f, "[io] %u %d %d 0x%x :> ", message->fdesc, message->event, size, message->u.errcode);

	if (size > 0) {
		const void *ud = message->data;
		eol = log_blob(f, ud, size);
	}
	if (!eol)
		fprintf(f, "\n");
	fflush(f);
}

void xu_log_output(FILE *f, uint32_t source, int type, const void * buffer, size_t sz)
{
	int eol = 0;
	if (type == MTYPE_IO) {
		log_io(f, buffer, sz);
	} else {
		uint32_t ti = (uint32_t)xu_now();
		fprintf(f, ":%08x %d %u ", source, type, ti);
		log_blob(f, buffer, sz);
		if (!eol)
			fprintf(f,"\n");
		fflush(f);
	}
}

