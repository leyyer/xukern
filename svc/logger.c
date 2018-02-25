#include <stdio.h>
#include "xu_kern.h"
#include "xu_malloc.h"
#include "xu_util.h"

struct logger {
	FILE *sfp;
	char *name;
	int close;
};

struct logger *logger_new(void)
{
	struct logger *logger;

	logger = xu_malloc(sizeof *logger);
	logger->sfp = NULL;
	logger->close = 0;
	logger->name = NULL;

	return logger;
}

static int __dispatch(struct xu_actor *ctx, void *ud, int type, uint32_t src, void *msg, size_t sz)
{
	struct logger *log = ud;
//	static int n = 1;

	switch (type) {
		case  MTYPE_LOG:
			fprintf(log->sfp, "[:%08x] ", src);
			fwrite(msg, sz, 1, log->sfp);
			fprintf(log->sfp, "\n");
			fflush(log->sfp);
			break;
#if 0
		case MTYPE_TIMEOUT:
			xu_error(ctx, "timeout %p", msg);
			xu_timeout(xu_actor_handle(ctx), 10, ++n);
			break;
#endif
	}

	return 0;
}

int logger_init(struct xu_actor *ctx, struct logger *log, const char *param)
{
	if (param && param[0] != '\0') {
		log->sfp = fopen(param, "w");
		if (log->sfp == NULL) {
			return 0;
		}
		log->name = xu_strdup(param);
		log->close = 1;
	} else {
		log->sfp = stdout;
	}

	if (log->sfp) {
		xu_actor_namehandle(xu_actor_handle(ctx), "logger");
		xu_actor_callback(ctx, log, __dispatch);
//		xu_timeout(xu_actor_handle(ctx), 200, 1);
		return 0;
	} 
	return -1;
}

void logger_free(struct logger *ud)
{
	if (ud->close) {
		fclose(ud->sfp);
	}
	xu_free(ud->name);
	xu_free(ud);
}

