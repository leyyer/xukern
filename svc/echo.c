#include <stdio.h>
#include "xu_kern.h"
#include "xu_malloc.h"
#include "xu_util.h"
#include "xu_io.h"

struct echo {
	int      udp;
	int      tcp;
	uint32_t handle;
};

struct echo *echo_new(void)
{
	struct echo *echo;

	echo = xu_malloc(sizeof *echo);
	echo->udp = -1;
	echo->tcp = -1;
	return echo;
}

static int __dispatch(struct xu_actor *ctx, void *ud, int mtype, uint32_t src, void *msg, size_t sz)
{
	struct xu_io_event *e;
	struct echo *echo = ud;
	size_t size;
	int r = 0;

	if (mtype != MTYPE_IO) {
		goto skip;
	}

	e = msg;
	size = e->size;
	xu_error(ctx, "event_type: %d, size: %d", e->event, size);
	switch (e->event) {
		case XIE_EVENT_LISTEN:
			xu_error(ctx, "listen fd: %d", e->fdesc);
			if (echo->udp < 0)
				echo->udp = e->fdesc;
			else 
				echo->tcp = e->fdesc;
			break;
		case XIE_EVENT_MESSAGE:
			xu_error(ctx, "message length: %d", size);
			if (e->fdesc == echo->tcp) {
				int fd = echo->tcp;
				echo->tcp = echo->udp;
				echo->udp = fd;
			}
			xu_io_udp_send(echo->handle, e->fdesc, &e->u.sa, e->data, size);
			r = 1;
			break;
		case XIE_EVENT_DATA:
			xu_error(ctx, "msg data ...");
			xu_io_write(echo->handle, e->fdesc, e->data, size);
			break;
	}
skip:
	xu_error(ctx, "type %d", mtype);
	return r;
}

int echo_init(struct xu_actor *ctx, struct echo *echo, const char *param)
{
	echo->handle = xu_actor_handle(ctx);
	xu_actor_namehandle(echo->handle, "echo");
	xu_actor_callback(ctx, echo, __dispatch);
	xu_io_udp_server(echo->handle, NULL, 60001);
	xu_io_tcp_server(echo->handle, NULL, 60002);
	xu_error(ctx, "echo init");
	return 0;
}

void echo_free(struct echo *ud)
{
	xu_free(ud);
}

