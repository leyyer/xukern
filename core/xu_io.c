#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include "uv.h"
#include "xu_impl.h"
#include "xu_kern.h"
#include "xu_util.h"
#include "xu_io.h"

#define XU_IO_TCP  1
#define XU_IO_UDP  2

#define IO_REQ_SERVER  1
#define IO_REQ_WRITE   2
#define IO_REQ_UDPSEND 3

struct server_req {
	uint16_t  protocol;
	uint16_t  port;
	uint32_t  owner;
	char      host[0];
};

struct write_req {
	uint32_t owner;
	int      fdesc;
	size_t   len;
	const void    *data;
};

struct udp_write_req {
	uint32_t owner;
	int      fdesc;
	union sockaddr_all addr;
	size_t     len;
	const void *data;
};

struct header {
	uint16_t type;
	uint16_t len;
};

struct request {
	struct header head;
	union {
		char buffer[256];
		struct server_req server;
		struct write_req write;
		struct udp_write_req udp;
	} u;
};

#define IO_HF_IDLE       0
#define IO_HF_CONNECTING 1
#define IO_HF_CONNECTED  2
#define IO_HF_LISTEN     3
#define IO_HF_CLOSING   4

struct iohandle {
	union {
		uv_handle_t handle;
		uv_tcp_t    tcp;
		uv_udp_t    udp;
		uv_stream_t stream;
		uv_poll_t   fd;
	} u;
	uint32_t owner;
	int      protocol;
	int      flag;
};

struct io_context {
	uv_poll_t recvfd;

	int sendfd;

	int cap;
	struct iohandle *handles;
	struct spinlock lock;
};

static struct io_context *_ioc = NULL;

static void __on_close(uv_handle_t *h)
{
	struct iohandle *ih = (struct iohandle *)h;

	ih->flag = IO_HF_IDLE;
	ih->protocol = -1;
	ih->owner = 0;
}

static void __report_eorc(uint32_t owner, int eorc, int fd, int errcode)
{
	struct xu_actor *ctx;
	struct xu_io_event xie;

	memset(&xie, 0, sizeof xie);
	xie.fdesc = fd;
	xie.u.errcode = errcode;
	xie.size = eorc << XIE_EVENT_SHIFT;

	ctx = xu_handle_ref(owner);
	if (ctx) {
		xu_send(ctx, 0, owner, MTYPE_IO, &xie, sizeof xie);
		xu_actor_unref(ctx);
	}
}

static void __report_lora(uint32_t owner, int e, int fd, struct sockaddr *sa)
{
	struct xu_io_event xie;
	struct xu_actor *xa;

	memset(&xie, 0, sizeof xie);
	xie.fdesc = fd;
	xie.size = e << XIE_EVENT_SHIFT;
	xie.u.sa.addr = *sa;
	xa = xu_handle_ref(owner);
	if (xa) {
		xu_send(xa, 0, owner, MTYPE_IO, &xie, sizeof xie);
		xu_actor_unref(xa);
	}
}

static struct iohandle *__get_h(struct io_context *ic)
{
	struct iohandle *ioh = NULL;

	for (int i = 0; i < ic->cap; ++i) {
		if (ic->handles[i].flag == IO_HF_IDLE) {
			ioh = &ic->handles[i];
			break;
		}
	}

	if (ioh == NULL) { /* no valid, expanding */
		xu_error(NULL, "expand iohandle cache.");
		ic->handles = xu_realloc(ic->handles, (ic->cap + 32) * sizeof ic->handles[0]);
		ioh = &ic->handles[ic->cap];
		ic->cap += 32;
	}
	return ioh;
}

static struct iohandle *__find_h(struct io_context *ic, uint32_t owner, int fdesc)
{
	struct iohandle *h = NULL, *it;
	int fd;

	for (int i = 0; i < ic->cap; ++i) {
		it = &ic->handles[i];
		if (it->flag != IO_HF_IDLE && it->owner == owner) {
			uv_fileno(&it->u.handle, &fd);
			if (fd == fdesc) {
				h = it;
				break;
			}
		}
	}

	return (h);
}

struct dnsreq {
	uv_getaddrinfo_t req;
	uint32_t owner;
	int      proto;
};

static void __on_alloc(uv_handle_t *handle, size_t size, uv_buf_t *buf)
{
	buf->base = xu_calloc(1, size);
	buf->len  = size;
}

static void __on_tcp_read(uv_stream_t *stream, int nread, const uv_buf_t *buf)
{
	struct iohandle *tcp = (struct iohandle *)stream;

	if (nread == 0) {
		goto skip;
	}

	if (nread == UV_EOF) {
		int fd;
		uv_fileno(&tcp->u.handle, &fd);
		xu_error(NULL, "fdesc %d eof.", fd);
		/*
		 * report close event.
		 */
		__report_eorc(tcp->owner, XIE_EVENT_CLOSE, fd, XIE_ERR_EOF);
		uv_read_stop(stream);
		if (tcp->flag != IO_HF_CLOSING) {
			uv_close(&tcp->u.handle, __on_close);
			tcp->flag = IO_HF_CLOSING;
		}
		goto skip;
	}

	if (nread > 0) {
		struct xu_io_event *xie;

		xie = xu_malloc(sizeof *xie + nread);

		uv_fileno(&tcp->u.handle, &xie->fdesc);

		xie->size = (XIE_EVENT_DATA << XIE_EVENT_SHIFT) | (nread & XIE_EVENT_MASK);
		memcpy(xie->data, buf->base, nread);

		struct xu_actor *ctx = xu_handle_ref(tcp->owner);
		if (ctx) {
			xu_send(ctx, 0, tcp->owner, (MTYPE_IO | MTYPE_TAG_DONTCOPY), xie, sizeof xie + nread);
			xu_actor_unref(ctx);
		} else {/* actor dead ? */
			if (tcp->flag != IO_HF_CLOSING) {
				uv_close(&tcp->u.handle, __on_close);
				tcp->flag = IO_HF_CLOSING;
			}
		}
	} /* XXX: nread < 0 case */
skip:
	if (buf->base)
		xu_free(buf->base);
}

static void __on_accept(uv_stream_t *stream, int err)
{
	struct iohandle *server, *ioh;

	if (err == 0) {
		uv_loop_t *loop = uv_default_loop();
		server = (struct iohandle *)stream;
		ioh = __get_h(_ioc);
		uv_tcp_init(loop, &ioh->u.tcp);

		if (uv_accept(stream, &ioh->u.stream) == 0) {
			union sockaddr_all sal;
			int fd, namelen;
			ioh->flag = IO_HF_CONNECTED;
			ioh->protocol = server->protocol;
			ioh->owner = server->owner;
			/* new connection */
			uv_fileno(&ioh->u.handle, &fd);
			namelen = sizeof sal;
			uv_tcp_getpeername(&ioh->u.tcp, (void *)&sal, &namelen);
			__report_lora(server->owner, XIE_EVENT_CONNECTION, fd, &sal.addr);
			uv_read_start(&ioh->u.stream, __on_alloc, __on_tcp_read);
		} else {
			xu_error(NULL, "handle :%0x accept failed.", server->owner);
		}
	}
}

static int __listen_tcp(struct iohandle *ioh, struct addrinfo *ai)
{
	int err;
	struct addrinfo *ni = ai;

	while (ni) {
		err = uv_tcp_bind(&ioh->u.tcp, ai->ai_addr, 0);
		if (!err) {
			return uv_listen(&ioh->u.stream, 32, __on_accept);
		}
		ni = ni->ai_next;
	}
	return err;
}

static void __on_udp_recv(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf,
		const struct sockaddr *addr, unsigned int flags)
{
	struct iohandle *udp = (struct iohandle *)handle;

	if (nread == 0 && addr == NULL) {
		goto skip;
	}

	if (nread > 0) {
		struct xu_io_event *xie;

		xie = xu_malloc(sizeof *xie + nread);

		uv_fileno(&udp->u.handle, &xie->fdesc);

		xie->size = (XIE_EVENT_MESSAGE << XIE_EVENT_SHIFT) | (nread & XIE_EVENT_MASK);
		xie->u.sa.addr = *addr;
		memcpy(xie->data, buf->base, nread);

		struct xu_actor *ctx = xu_handle_ref(udp->owner);
		if (ctx) {
			xu_send(ctx, 0, udp->owner, (MTYPE_IO | MTYPE_TAG_DONTCOPY), xie, sizeof xie + nread);
			xu_actor_unref(ctx);
		} else { /* actor dead ? */
			if (udp->flag != IO_HF_CLOSING) {
				uv_close(&udp->u.handle, __on_close);
				udp->flag = IO_HF_CLOSING;
			}
		}
	}
skip:
	if (buf->base)
		xu_free(buf->base);
}

static int __listen_udp(struct iohandle *ioh, struct addrinfo *ai)
{
	int err;
	struct addrinfo *ni;

	ni = ai;
	while (ni) {
		err = uv_udp_bind(&ioh->u.udp, ai->ai_addr, UV_UDP_REUSEADDR);
		if (!err) {
			return uv_udp_recv_start(&ioh->u.udp, __on_alloc, __on_udp_recv);
		}
		ni = ni->ai_next;
	}
	return err;
}

static void __on_dns(uv_getaddrinfo_t *rq, int err, struct addrinfo *ai)
{
	struct dnsreq *dr = container_of(rq, struct dnsreq, req);

	if (ai == NULL) {
		__report_eorc(dr->owner,  XIE_EVENT_ERROR, -1, XIE_ERR_LISTEN);
		xu_free(dr);
		return;
	}

	struct iohandle *ioh = __get_h(_ioc);
	if (err == 0) {
		uv_loop_t *loop = uv_default_loop();
		switch (dr->proto) {
			case XU_IO_TCP:
				uv_tcp_init(loop, &ioh->u.tcp);
				err = __listen_tcp(ioh, ai);
				break;
			case XU_IO_UDP:
				uv_udp_init(loop, &ioh->u.udp);
				err = __listen_udp(ioh, ai);
				break;
			default:
				err = -1;
				break;
		}
	}
	/* 
	 * XXX: report 'listen' or `error' event to `owern'
	 */
	if (err == 0) {
		int fd;
		ioh->flag = IO_HF_LISTEN;
		ioh->protocol = dr->proto;
		ioh->owner = dr->owner;
		uv_fileno(&ioh->u.handle, &fd);
		__report_lora(dr->owner, XIE_EVENT_LISTEN, fd, ai->ai_addr);
	} else { /* report error */
		printf("err = %d\n", err);
		__report_eorc(dr->owner,  XIE_EVENT_ERROR, -1, XIE_ERR_LISTEN);
		if (ioh->flag != IO_HF_CLOSING) {
			uv_close(&ioh->u.handle, __on_close);
			ioh->flag = IO_HF_CLOSING;
		}
	}
	uv_freeaddrinfo(ai);
	xu_free(dr);
}

static void __handle_req_server(struct io_context *ic, struct request *req)
{
	struct addrinfo hints;
	const char *node;
	char service[32];
	struct server_req *sr = &req->u.server;
	int hlen;
	struct dnsreq *dr;
	uv_loop_t *loop = uv_default_loop();

	memset(&hints, 0, sizeof hints);
	sprintf(service, "%d", sr->port);
	hlen = strlen(sr->host);
	if (hlen > 0) {
		if (sr->host[0] == '[' && sr->host[hlen-1] == ']') {
			hints.ai_family = AF_INET6;
		} else {
			hints.ai_family = AF_INET;
		}
		node = sr->host;
	} else {
		hints.ai_family = AF_INET;
		node = NULL;
	}

	if (sr->protocol == XU_IO_TCP) {
		hints.ai_socktype = SOCK_STREAM;
	} else if (sr->protocol == XU_IO_UDP) {
		hints.ai_socktype = SOCK_DGRAM;
	}
	hints.ai_flags = AI_PASSIVE;

	dr = xu_calloc(1, sizeof *dr);
	dr->proto = sr->protocol;
	dr->owner = sr->owner;

	if (uv_getaddrinfo(loop, &dr->req, __on_dns, node, service, &hints)) {
		__report_eorc(sr->owner, XIE_EVENT_ERROR, -1, XIE_ERR_NOTSUPP);
		xu_free(dr);
	}
}

static void __on_write(uv_write_t *req, int err)
{
	if (err) { /* XXX: report error. */
	}
	xu_free(req);
}

static void __handle_req_write(struct io_context *ic, struct request *req)
{
	struct write_req *wr = &req->u.write;
	struct iohandle *h = __find_h(ic, wr->owner, wr->fdesc);
	uv_write_t *uwr;

	if (h && h->flag == IO_HF_CONNECTED) {
		uv_buf_t buf;
		uwr = xu_malloc(sizeof *uwr);
		uwr->data = h;
		buf.base = (void *)wr->data;
		buf.len = wr->len;
		if (uv_write(uwr, &h->u.stream, &buf, 1, __on_write)) {
			/*
			 * XXX: report error.
			 */
			xu_free(uwr);
		}
	}
	xu_free((void *)wr->data);
}

static void __on_send(uv_udp_send_t *uwr, int status)
{
	xu_free(uwr);
}

static void __handle_req_udp(struct io_context *ic, struct request *req)
{
	struct udp_write_req *wr = &req->u.udp;
	struct iohandle *h = __find_h(ic, wr->owner, wr->fdesc);
	uv_udp_send_t *uwr;

	if (h) {
		uv_buf_t buf;
		uwr = xu_malloc(sizeof *uwr);
		uwr->data = h;
		buf.base = (void *)wr->data;
		buf.len = wr->len;
		if (uv_udp_send(uwr, &h->u.udp, &buf, 1, &wr->addr.addr, __on_send)) {
			/*
			 * XXX: report error.
			 */
			xu_free(uwr);
		}
	}
	xu_free((void *)wr->data);
}

static void __handle_req(struct io_context *ic, struct request *req)
{
	switch (req->head.type) {
		case IO_REQ_SERVER:
			__handle_req_server(ic, req);
			break;
		case IO_REQ_WRITE:
			__handle_req_write(ic, req);
			break;
		case IO_REQ_UDPSEND:
			__handle_req_udp(ic, req);
			break;
	}
}

static inline int __read(int fd, void *buf, int len)
{
	int r;

	do {
		r = read(fd, buf, len);
	} while ((r == -1 ) && (errno == EINTR || errno == EAGAIN));

	return (r);
}

static inline int __write(int fd, void *buf, int len)
{
	int r;

	do {
		r = write(fd, buf, len);
	} while ((r == -1) && (errno == EINTR || errno == EAGAIN));
	return (r);
}

static inline int __send_req(int qtype, struct request *req, int reqlen)
{
	int r;

	req->head.type = qtype;
	req->head.len = reqlen;
	reqlen += 4; /* add header */
	r =  __write(_ioc->sendfd, req, reqlen);
	assert(r == reqlen);
	return (r - 4);
}

static void __on_req(uv_poll_t *uvp, int status, int events)
{
	struct io_context *ic;
	struct request req;
	int r, fd;

	if (uv_fileno((uv_handle_t *)uvp, &fd) != 0) {
		xu_error(NULL, "can't get file handle");
		return;
	}
	if (events & UV_READABLE) {
		ic = container_of(uvp, struct io_context, recvfd);
		memset(&req, 0, sizeof req);
		r = __read(fd, &req.head, sizeof req.head);
		if (r < 0)
			return;
		assert(r == sizeof req.head);
		assert(req.head.len <= sizeof req.u);
		r = __read(fd, &req.u, req.head.len);
		assert(r == req.head.len);
		__handle_req(ic, &req);
	}
}

void xu_io_init(void)
{
	int pfd[2];
	uv_loop_t *loop =uv_default_loop();

	_ioc = xu_calloc(1, sizeof *_ioc);

	_ioc->cap = 32;
	_ioc->handles = xu_calloc(_ioc->cap, sizeof _ioc->handles[0]);

	SPIN_INIT(_ioc);

	if (pipe(pfd) < 0) {
		fprintf(stderr, "pipe failed.\n");
		fflush(stderr);
		abort();
	}
	_ioc->sendfd = pfd[1];
	uv_poll_init(loop, &_ioc->recvfd, pfd[0]);
	uv_poll_start(&_ioc->recvfd, UV_READABLE, __on_req);
}

static int __io_server(uint32_t h, const char *addr, int port, int proto)
{
	struct request req;
	struct server_req *sr;
	uint16_t reqlen = sizeof *sr;

	memset(&req, 0, sizeof req);
	sr = &req.u.server;
	if (addr) {
		size_t len = strlen(addr);
		size_t vlen = sizeof req.u - sizeof *sr;
		if (len > vlen - 1) {
			xu_error(NULL, "address %s too long.", addr);
			return -1;
		}
		len = xu_strlcpy(sr->host, addr, vlen);
		reqlen += len;
	}
	sr->protocol = proto;
	sr->port  = port;
	sr->owner = h;
	return __send_req(IO_REQ_SERVER, &req, reqlen) != reqlen;
}

int xu_io_tcp_server(uint32_t h, const char *addr, int port)
{
	return __io_server(h, addr, port, XU_IO_TCP);
}

int xu_io_udp_server(uint32_t h, const char *addr, int port)
{
	return __io_server(h, addr, port,  XU_IO_UDP);
}

int xu_io_write(uint32_t handle, int fdesc, const void *data, int len)
{
	struct request req;
	struct write_req *wr;

	wr = &req.u.write;
	wr->owner = handle;
	wr->fdesc = fdesc;
	wr->len   = len;
	wr->data  = data;
	return __send_req(IO_REQ_WRITE, &req, sizeof *wr) != sizeof *wr;
}

int xu_io_udp_send(uint32_t handle, int fdesc, union sockaddr_all *addr, const void *data, int len)
{
	struct request req;
	struct udp_write_req *ur;

	ur = &req.u.udp;
	ur->owner = handle;
	ur->fdesc = fdesc;
	ur->addr = *addr;
	ur->len = len;
	ur->data = data;

	return __send_req(IO_REQ_UDPSEND, &req, sizeof *ur) != sizeof *ur;
}

int xu_io_step()
{
	return uv_run(uv_default_loop(), UV_RUN_ONCE);
}

