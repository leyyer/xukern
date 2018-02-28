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
#include "list.h"

#define TCP_BACKLOG (32)

#define XU_IO_TCP  1
#define XU_IO_UDP  2

#define IO_REQ_SERVER      1
#define IO_REQ_WRITE       2
#define IO_REQ_UDPSEND     3
#define IO_REQ_CLOSE       4
#define IO_REQ_TCP_CONNECT 5
#define IO_REQ_UOPEN       6
#define IO_REQ_MEMBERSHIP  7
#define IO_REQ_FLAGS       8
#define IO_REQ_POLLFD      9

struct req_host {
	uint16_t  protocol;
	uint16_t  port;
	char      host[0];
};

struct req_write {
	size_t         len;
	const void    *data;
};

struct req_usend {
	union sockaddr_all addr;
	size_t     len;
	const void *data;
};

struct req_uopen {
	int udp6;
};

struct req_membership {
	int mlen;
	int ilen;
	int join;
	char maddr[108];
	char iaddr[108];
};

#define REQ_FLAGS_MCAST_LOOP     1
#define REQ_FLAGS_BROADCAST      2
#define REQ_FLAGS_UDP_TTL        3
#define REQ_FLAGS_UDP_MCAST_TTL  4
#define REQ_FLAGS_TCP_NODELAY    5
#define REQ_FLAGS_TCP_KEEPALIVE  6
struct req_flags {
	int flag;
	int how;
	int reserved;
};

#define REQ_TYPE_SHIFT (24)
#define REQ_TYPE_MASK  (0xffffff)
struct header {
	uint32_t      head; /* 1 byte type + 3 byte len */
	uint32_t      owner;
	uint32_t      fdesc;
};

struct request {
	struct header header;
	union {
		char buffer[256];
		int  reserved;
		struct req_host host;
		struct req_write  write;
		struct req_usend  usend;
		struct req_uopen  uopen;
		struct req_membership membership;
		struct req_flags  flags;
	} u;
};

#define IO_HF_IDLE       0
#define IO_HF_CONNECTING 1
#define IO_HF_CONNECTED  2
#define IO_HF_LISTEN     3
#define IO_HF_CLOSING    4
#define IO_HF_UDP_OPENED 5

struct iohandle {
	union {
		uv_handle_t handle;
		uv_tcp_t    tcp;
		uv_udp_t    udp;
		uv_stream_t stream;
		uv_poll_t   fd;
		uv_tty_t    tty;
	} u;

	struct list_head link;

	uint32_t owner;
	uint32_t handle;
	int      protocol;
	int      flag;
};

struct io_context {
	uv_poll_t recvfd;

	int sendfd;

	struct list_head io;

	uint32_t handle_index;
	struct spinlock lock;
};

static struct io_context *_ioc = NULL;

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

static uint32_t __get_fdesc()
{
	uint32_t h;

	SPIN_LOCK(_ioc);
	h = _ioc->handle_index++;
	if (_ioc->handle_index == 0)
		_ioc->handle_index = 1;
	SPIN_UNLOCK(_ioc);

	return (h);
}

static void __on_close(uv_handle_t *h)
{
	struct iohandle *ih = (struct iohandle *)h;

	xu_error(NULL, "freeing owner [%u]  fd[%u] %p", ih->owner, ih->handle, ih);

	xu_free(ih);
}

static void __report_eorc(uint32_t owner, int eorc, uint32_t fd, int errcode)
{
	struct xu_actor *ctx;
	struct xu_io_event xie;

	memset(&xie, 0, sizeof xie);
	xie.fdesc = fd;
	xie.event = eorc;
	xie.u.errcode = errcode;

	ctx = xu_handle_ref(owner);
	if (ctx) {
		xu_send(ctx, 0, owner, MTYPE_IO, &xie, sizeof xie);
		xu_actor_unref(ctx);
	}
}

static void __close_handle(struct iohandle *io, int reason)
{
	struct xu_actor *ctx;
	if (io->flag != IO_HF_CLOSING) {
		uv_close(&io->u.handle, __on_close);

		io->flag = IO_HF_CLOSING;

		list_del(&io->link);

		ctx = xu_handle_ref(io->owner);
		if (ctx) {
			__report_eorc(io->owner, XIE_EVENT_CLOSE, io->handle, reason);
		}
	}
}

static void __report_lora(uint32_t owner, int e, uint32_t fd, struct sockaddr *sa)
{
	struct xu_io_event xie;
	struct xu_actor *xa;

	xa = xu_handle_ref(owner);
	if (xa) {
		memset(&xie, 0, sizeof xie);
		xie.fdesc = fd;
		xie.event = e;
		xie.u.sa.in = *sa;
		xu_send(xa, 0, owner, MTYPE_IO, &xie, sizeof xie);
		xu_actor_unref(xa);
	}
}

static void __report_drain(uint32_t owner, uint32_t fd, int code)
{
	struct xu_io_event xie;
	struct xu_actor *xa;

	if ((xa = xu_handle_ref(owner)) != NULL) {
		memset(&xie, 0, sizeof xie);
		xie.fdesc = fd;
		xie.event = XIE_EVENT_DRAIN;
		xie.u.errcode = code;
		xu_send(xa, 0, owner, MTYPE_IO, &xie, sizeof xie);
	}
}

static struct iohandle *alloc_iohandle(struct io_context *ic)
{
	struct iohandle *ioh = NULL;

	ioh = xu_calloc(1, sizeof *ioh);

	INIT_LIST_HEAD(&ioh->link);
	ioh->flag = IO_HF_IDLE;

	list_add(&ioh->link, &ic->io);

	return ioh;
}

static struct iohandle *__find_io(struct io_context *ic, uint32_t owner, uint32_t fdesc)
{
	struct iohandle *h = NULL, *it;

	list_for_each_entry(it, &ic->io, link) {
		if (it->flag != IO_HF_IDLE && it->owner == owner && it->handle == fdesc) {
			h = it;
			break;
		}
	}
	return (h);
}

struct dnsreq {
	uv_getaddrinfo_t req;
	int      reqtype;
	uint32_t owner;
	uint32_t handle;
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
		xu_error(NULL, "fdesc %d eof handle %u.", fd, tcp->handle);
		/*
		 * report close event.
		 */
		uv_read_stop(stream);
		__close_handle(tcp, XIE_ERR_EOF);
		goto skip;
	}

	if (nread > 0) {
		struct xu_io_event *xie;

		xie = xu_malloc(sizeof *xie + nread);

		xie->fdesc = tcp->handle;

		xie->event = XIE_EVENT_DATA;
		xie->size = nread;
		memcpy(xie->data, buf->base, nread);

		struct xu_actor *ctx = xu_handle_ref(tcp->owner);
		if (ctx) {
			xu_send(ctx, 0, tcp->owner, (MTYPE_IO | MTYPE_TAG_DONTCOPY), xie, sizeof xie + nread);
			xu_actor_unref(ctx);
		} else {/* actor dead ? */
			__close_handle(tcp, XIE_ERR_RECV_DATA);
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
		ioh = alloc_iohandle(_ioc);
		uv_tcp_init(loop, &ioh->u.tcp);

		if (uv_accept(stream, &ioh->u.stream) == 0) {
			union sockaddr_all sal;
			int namelen;
			ioh->flag = IO_HF_CONNECTED;
			ioh->protocol = server->protocol;
			ioh->owner = server->owner;
			/* new connection */
			ioh->handle = __get_fdesc();
			namelen = sizeof sal;
			uv_tcp_getpeername(&ioh->u.tcp, (void *)&sal, &namelen);
			__report_lora(server->owner, XIE_EVENT_CONNECTION, ioh->handle, &sal.in);
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
//		printf("ai_family: %s\n", (ai->ai_family == AF_INET ? "inet" : (ai->ai_family == AF_INET6 ? "inet6" : "unknown")));
//		printf("ai_socktype: %s\n", (ai->ai_socktype == SOCK_STREAM ? "tcp" : (ai->ai_socktype == SOCK_DGRAM ? "udp" : "unknown")));
		err = uv_tcp_bind(&ioh->u.tcp, ai->ai_addr, 0);
		if (!err) {
			err =  uv_listen(&ioh->u.stream, TCP_BACKLOG, __on_accept);
			if (err == 0) {
				break;
			}
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

		xie->fdesc = udp->handle;
		xie->event = XIE_EVENT_MESSAGE;
		xie->size = nread;
		xie->u.sa.in = *addr;
		memcpy(xie->data, buf->base, nread);

		struct xu_actor *ctx = xu_handle_ref(udp->owner);
		if (ctx) {
			xu_send(ctx, 0, udp->owner, (MTYPE_IO | MTYPE_TAG_DONTCOPY), xie, sizeof xie + nread);
			xu_actor_unref(ctx);
		} else { /* actor dead ? */
			__close_handle(udp, XIE_ERR_RECV_DATA);
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

static void __on_dns_server(struct dnsreq *dr, int err, struct addrinfo *ai)
{
	struct iohandle *ioh = alloc_iohandle(_ioc);

	ioh->owner = dr->owner;
	ioh->handle = dr->handle;
	ioh->protocol = dr->proto;
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
				__report_eorc(dr->owner,  XIE_EVENT_ERROR, -1, XIE_ERR_LISTEN);
				list_del(&ioh->link);
				xu_free(ioh);
				return;
		}
	}
	/* 
	 * report 'listen' or `error' event to `owern'
	 */
	if (err == 0) {
		ioh->flag = IO_HF_LISTEN;
		__report_lora(dr->owner, XIE_EVENT_LISTEN, ioh->handle, ai->ai_addr);
	} else { /* report error */
		struct xu_actor *xa = xu_handle_ref(dr->owner);
		xu_error(xa, "dns error: %s", uv_strerror(err));
		__report_eorc(dr->owner,  XIE_EVENT_ERROR, -1, XIE_ERR_LISTEN);
		__close_handle(ioh, XIE_ERR_LISTEN);
		if (xa) {
			xu_actor_unref(xa);
		}
	}
}

static void on_tcp_con(uv_connect_t *uc, int err)
{
	struct iohandle *tcp = uc->data;
	union sockaddr_all sa;
	int namelen;

	if (err) {
		__close_handle(tcp, XIE_ERR_CONNECT);
	} else {
		tcp->flag = IO_HF_CONNECTED;
		memset(&sa, 0, sizeof sa);
		namelen = sizeof sa;
		uv_tcp_getpeername(&tcp->u.tcp, &sa.in, &namelen);
		__report_lora(tcp->owner, XIE_EVENT_CONNECT, tcp->handle, &sa.in);
		uv_read_start(&tcp->u.stream, __on_alloc, __on_tcp_read);
	}
	xu_free(uc);
}

static void __on_dns_tcp_connect(struct dnsreq *dr, int err, struct addrinfo *ai)
{
	struct addrinfo *ni;
	struct iohandle *tcp;
	uv_connect_t *req;

	if (err != 0) {
		__report_eorc(dr->owner, XIE_EVENT_ERROR, dr->handle, XIE_ERR_LOOKUP);
		return;
	}
	tcp = alloc_iohandle(_ioc);
	req = xu_calloc(1, sizeof *req);
	req->data = tcp;
	ni = ai;
	uv_tcp_init(uv_default_loop(), &tcp->u.tcp);
	tcp->flag = IO_HF_CONNECTING;

	while (ni) {
		err = uv_tcp_connect(req, &tcp->u.tcp, ni->ai_addr, on_tcp_con);
		if (err == 0)
			break;
		ni = ni->ai_next;
	}

	if (err) {
		__report_eorc(dr->owner, XIE_EVENT_ERROR, dr->handle, XIE_ERR_CONNECT);
		__close_handle(tcp, XIE_ERR_CONNECT);
		xu_free(req);
	}
}

static void __on_dns(uv_getaddrinfo_t *rq, int err, struct addrinfo *ai)
{
	struct dnsreq *dr = container_of(rq, struct dnsreq, req);

	if (ai == NULL) {
		__report_eorc(dr->owner,  XIE_EVENT_ERROR, -1, XIE_ERR_LISTEN);
		goto skip;
	}
	switch (dr->reqtype) {
		case IO_REQ_SERVER:
			__on_dns_server(dr, err, ai);
			break;
		case IO_REQ_TCP_CONNECT:
			__on_dns_tcp_connect(dr, err, ai);
			break;
	}
skip:
	uv_freeaddrinfo(ai);
	xu_free(dr);
}

static void __handle_req_uopen(struct io_context *ic, struct request *req)
{
	struct iohandle *udp;
	struct req_uopen *ru = &req->u.uopen;

	udp = alloc_iohandle(ic);

	if (uv_udp_init_ex(uv_default_loop(), &udp->u.udp, ru->udp6 ? AF_INET6 : AF_INET)) {
		/* XXX: report error */
	}

	uv_udp_recv_start(&udp->u.udp, __on_alloc, __on_udp_recv);

	udp->owner = req->header.owner;
	udp->handle = req->header.fdesc;
	udp->flag = IO_HF_UDP_OPENED;
}

static void __handle_req_host(struct io_context *ic, struct request *req)
{
	struct addrinfo hints;
	const char *node;
	char service[32];
	struct req_host *sr = &req->u.host;
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
	dr->reqtype = req->header.head >> REQ_TYPE_SHIFT;
	dr->proto = sr->protocol;
	dr->owner = req->header.owner;
	dr->handle = req->header.fdesc;

	if (uv_getaddrinfo(loop, &dr->req, __on_dns, node, service, &hints)) {
		__report_eorc(req->header.owner, XIE_EVENT_ERROR, -1, XIE_ERR_NOTSUPP);
		xu_free(dr);
	}
}

static void __on_write(uv_write_t *req, int err)
{
	struct iohandle *h = req->data;

	__report_drain(h->owner, h->handle, err);
	xu_free(req);
}

static void __handle_req_write(struct io_context *ic, struct request *req)
{
	struct req_write *wr = &req->u.write;
	struct iohandle *h = __find_io(ic, req->header.owner, req->header.fdesc);
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
	struct iohandle *udp = uwr->data;

	//printf("udp_send recv %d\n", status);
	__report_drain(udp->owner, udp->handle, status);
	xu_free(uwr);
}

static void __handle_req_usend(struct io_context *ic, struct request *req)
{
	struct req_usend *wr = &req->u.usend;
	struct iohandle *h = __find_io(ic, req->header.owner, req->header.fdesc);
	uv_udp_send_t *uwr;

	//printf("usend_req: %p, owner: %u, fdesc: %u\n", h, req->header.owner, req->header.fdesc);
	if (h) {
		uv_buf_t buf;
		uwr = xu_malloc(sizeof *uwr);
		uwr->data = h;
		buf.base = (void *)wr->data;
		buf.len = wr->len;
		if (uv_udp_send(uwr, &h->u.udp, &buf, 1, &wr->addr.in, __on_send)) {
			/*
			 * XXX: report error.
			 */
			xu_free(uwr);
		}
	}
	xu_free((void *)wr->data);
}

static void __handle_req_close(struct io_context *ic, struct request *req)
{
	struct iohandle *h = __find_io(ic, req->header.owner, req->header.fdesc);

	if (h) {
		__close_handle(h, 0);
	}
}

static void __handle_req_membership(struct io_context *ic, struct request *req)
{
	struct iohandle *h = __find_io(ic, req->header.owner, req->header.fdesc);
	struct req_membership *rm = &req->u.membership;

	if (h) {
		if (uv_udp_set_membership(&h->u.udp, rm->maddr, rm->iaddr, rm->join ? UV_JOIN_GROUP : UV_LEAVE_GROUP)) { /* XXX: report error */
		} 
	}
}

static void __handle_req_flags(struct io_context *ic, struct request *req)
{
	struct iohandle *h = __find_io(ic, req->header.owner, req->header.fdesc);

	if (!h) { /* XXX: report error ? */
		return;
	}
	struct req_flags *rf = &req->u.flags;
	switch (rf->flag) {
		case REQ_FLAGS_MCAST_LOOP:
			uv_udp_set_multicast_loop(&h->u.udp, rf->how);
			break;
		case REQ_FLAGS_BROADCAST:
			uv_udp_set_broadcast(&h->u.udp, rf->how);
			break;
		case REQ_FLAGS_UDP_TTL:
			uv_udp_set_ttl(&h->u.udp, rf->how);
			break;
		case REQ_FLAGS_UDP_MCAST_TTL:
			uv_udp_set_multicast_ttl(&h->u.udp, rf->how);
			break;
		case REQ_FLAGS_TCP_NODELAY:
			uv_tcp_nodelay(&h->u.tcp, rf->how);
			break;
		case REQ_FLAGS_TCP_KEEPALIVE:
			uv_tcp_keepalive(&h->u.tcp, rf->how, rf->reserved);
			break;
	}
}

static void __on_poll(uv_poll_t *handle, int status, int event)
{
	struct iohandle *io = (struct iohandle *)handle;
	int nread;
	int fd;

	if (status != 0) { /* XXX: report error. */
		return;
	}

	if (event & UV_READABLE) {
		uv_fileno(&io->u.handle, &fd);
		struct xu_io_event *xie = xu_malloc(sizeof *xie + BUFSIZ);
		nread = __read(fd, xie->data, BUFSIZ);
		if (nread > 0) {
			xie->fdesc = io->handle;
			xie->event = XIE_EVENT_DATA;
			xie->size = nread;
			struct xu_actor *ctx = xu_handle_ref(io->owner);
			if (ctx) {
				xu_send(ctx, 0, io->owner, (MTYPE_IO | MTYPE_TAG_DONTCOPY), xie, sizeof xie + nread);
				xu_actor_unref(ctx);
			} else {/* actor dead ? */
				xu_free(xie);
				__close_handle(io, XIE_ERR_RECV_DATA);
			}
		} else { /* XXX: nread < 0 case */
			xu_free(xie);
		}
	}
}

static void __handle_req_pollfd(struct io_context *ic, struct request *req)
{
	struct iohandle *io;
	
	io = alloc_iohandle(ic);
	uv_poll_init(uv_default_loop(), &io->u.fd, req->u.reserved);
	uv_poll_start(&io->u.fd, UV_READABLE, __on_poll);
}

static void __handle_req(struct io_context *ic, struct request *req)
{
	struct header *hr = &req->header;
	int type = hr->head >> REQ_TYPE_SHIFT;
	switch (type) {
		case IO_REQ_SERVER:
		case IO_REQ_TCP_CONNECT:
			__handle_req_host(ic, req);
			break;
		case IO_REQ_WRITE:
			__handle_req_write(ic, req);
			break;
		case IO_REQ_UDPSEND:
			__handle_req_usend(ic, req);
			break;
		case IO_REQ_CLOSE:
			__handle_req_close(ic, req);
			break;
		case IO_REQ_UOPEN:
			__handle_req_uopen(ic, req);
			break;
		case IO_REQ_MEMBERSHIP:
			__handle_req_membership(ic, req);
			break;
		case IO_REQ_FLAGS:
			__handle_req_flags(ic, req);
			break;
		case IO_REQ_POLLFD:
			__handle_req_pollfd(ic, req);
			break;
	}
}

static inline int __send_req(struct request *req, int qtype, uint32_t o, uint32_t h, int reqlen)
{
	int r;
	struct header *hr;

	hr = &req->header;
	hr->head = ((qtype & 0xff) << REQ_TYPE_SHIFT) | (reqlen & REQ_TYPE_MASK);
	hr->owner    = o;
	hr->fdesc    = h;
	reqlen += sizeof *hr; /* add header */
	r =  __write(_ioc->sendfd, req, reqlen);
//	xu_error(NULL, "%s: %d, r = %d, type = %d", __func__, reqlen, r,  qtype);
	assert(r == reqlen);
	return (r - sizeof *hr);
}

static void __on_req(uv_poll_t *uvp, int status, int events)
{
	struct io_context *ic;
	struct request req;
	int r, fd, size;

	if (uv_fileno((uv_handle_t *)uvp, &fd) != 0) {
		xu_error(NULL, "can't get file handle");
		return;
	}
	if (events & UV_READABLE) {
		struct header *hr = &req.header;
		ic = container_of(uvp, struct io_context, recvfd);
		memset(&req, 0, sizeof req);
		r = __read(fd, hr, sizeof *hr);
		if (r < 0)
			return;
		assert(r == sizeof *hr);
		size = hr->head & REQ_TYPE_MASK;
		//xu_error(NULL, "r = %d, req_len = %d, type = %d, sizeof req.u = %d", r, size, type, sizeof req.u);
		assert(size <= sizeof req.u);
		if (size > 0) {
			r = __read(fd, &req.u, size);
			//xu_error(NULL, "r = %d, req.head.len = %d", r, size);
			assert(r == size);
		}
		__handle_req(ic, &req);
	}
}

void xu_io_init(void)
{
	int pfd[2];
	uv_loop_t *loop =uv_default_loop();

	_ioc = xu_calloc(1, sizeof *_ioc);

	SPIN_INIT(_ioc);

	if (pipe(pfd) < 0) {
		fprintf(stderr, "pipe failed.\n");
		fflush(stderr);
		abort();
	}
	_ioc->sendfd = pfd[1];

	_ioc->handle_index = 1;

	INIT_LIST_HEAD(&_ioc->io);

	uv_poll_init(loop, &_ioc->recvfd, pfd[0]);
	uv_poll_start(&_ioc->recvfd, UV_READABLE, __on_req);
}

static uint32_t __io_host(uint32_t h, int e, const char *addr, int port, int proto)
{
	struct request req;
	struct req_host *sr;
	uint16_t reqlen = sizeof *sr;
	uint32_t fdesc;

	memset(&req, 0, sizeof req);
	sr = &req.u.host;
	if (addr) {
		size_t len = strlen(addr);
		size_t vlen = sizeof req.u - sizeof *sr;
		if (len > vlen - 1) {
			struct xu_actor *xa = xu_handle_ref(h);
			xu_error(xa, "address %s too long.", addr);
			if (xa)
				xu_actor_unref(xa);
			return -1;
		}
		len = xu_strlcpy(sr->host, addr, vlen);
		reqlen += len;
	}
	sr->protocol = proto;
	sr->port  = port;
	fdesc = __get_fdesc();
	__send_req(&req, e, h, fdesc, reqlen);
	return fdesc;
}

uint32_t xu_io_tcp_connect(uint32_t handle, const char *addr, int port)
{
	return __io_host(handle, IO_REQ_TCP_CONNECT, addr, port, XU_IO_TCP);
}

uint32_t xu_io_tcp_server(uint32_t h, const char *addr, int port)
{
	return __io_host(h, IO_REQ_SERVER, addr, port, XU_IO_TCP);
}

uint32_t xu_io_udp_server(uint32_t h, const char *addr, int port)
{
	return __io_host(h, IO_REQ_SERVER, addr, port,  XU_IO_UDP);
}

int xu_io_write(uint32_t handle, uint32_t fdesc, const void *data, int len)
{
	struct request req;
	struct req_write *wr;

	if (len <= 0) {
		return -1;
	}

	wr = &req.u.write;
	wr->len   = len;
	wr->data  = data;
#if 0
	{
		struct xu_actor *ctx = xu_handle_ref(handle);
		xu_error(ctx, "write: %d bytes", len);
		xu_actor_unref(ctx);
	}
#endif
	return __send_req(&req, IO_REQ_WRITE, handle, fdesc, sizeof *wr) != sizeof *wr;
}

int xu_io_udp_send(uint32_t handle, uint32_t fdesc, union sockaddr_all *addr, const void *data, int len)
{
	struct request   req;
	struct req_usend *ur;

	ur = &req.u.usend;
	ur->addr = *addr;
	ur->len = len;
	ur->data = data;

	return __send_req(&req, IO_REQ_UDPSEND, handle, fdesc, sizeof *ur) != sizeof *ur;
}

uint32_t xu_io_udp_open(uint32_t handle, int udp6)
{
	struct request req;
	struct req_uopen *ru;
	uint32_t fdesc;

	ru = &req.u.uopen;
	ru->udp6 = udp6;
	fdesc = __get_fdesc();
	__send_req(&req, IO_REQ_UOPEN, handle, fdesc, sizeof *ru);

	return fdesc;
}

uint32_t xu_io_fd_open(uint32_t handle, int fd)
{
	struct request req;
	uint32_t h;

	h = __get_fdesc();
	req.u.reserved = fd;
	__send_req(&req, IO_REQ_POLLFD, handle, h, sizeof req.u.reserved);

	return h;
}

int xu_io_udp_membership(uint32_t handle, uint32_t fdesc, const char *mcast, const char *iaddr, int join)
{
	struct request req;
	struct req_membership *rm = &req.u.membership;

	rm->mlen = xu_strlcpy(rm->maddr, mcast, sizeof rm->maddr);
	rm->ilen = xu_strlcpy(rm->iaddr, iaddr, sizeof rm->iaddr);
	rm->join = join;

	return __send_req(&req, IO_REQ_MEMBERSHIP, handle, fdesc, sizeof *rm) != sizeof *rm;
}

int xu_io_udp_set_multicast_loop(uint32_t handle, uint32_t fdesc, int on)
{
	struct request req;
	struct req_flags *rf = &req.u.flags;

	rf->flag = REQ_FLAGS_MCAST_LOOP;
	rf->how = on;

	return __send_req(&req, IO_REQ_FLAGS, handle, fdesc, sizeof *rf) != sizeof *rf;
}

int xu_io_udp_set_broadcast(uint32_t handle, uint32_t fdesc, int on)
{
	struct request req;
	struct req_flags *rf = &req.u.flags;

	rf->flag = REQ_FLAGS_BROADCAST;
	rf->how = on;

	return __send_req(&req, IO_REQ_FLAGS, handle, fdesc, sizeof *rf) != sizeof *rf;
}

int xu_io_udp_set_ttl(uint32_t handle, uint32_t fdesc, int on)
{
	struct request req;
	struct req_flags *rf = &req.u.flags;

	rf->flag = REQ_FLAGS_UDP_TTL;
	rf->how = on;

	return __send_req(&req, IO_REQ_FLAGS, handle, fdesc, sizeof *rf) != sizeof *rf;
}

int xu_io_udp_set_multicast_ttl(uint32_t handle, uint32_t fdesc, int on)
{
	struct request req;
	struct req_flags *rf = &req.u.flags;

	rf->flag = REQ_FLAGS_UDP_MCAST_TTL;
	rf->how = on;

	return __send_req(&req, IO_REQ_FLAGS, handle, fdesc, sizeof *rf) != sizeof *rf;
}

int xu_io_tcp_nodelay(uint32_t handle, uint32_t fdesc, int on)
{
	struct request req;
	struct req_flags *rf = &req.u.flags;

	rf->flag = REQ_FLAGS_TCP_NODELAY;
	rf->how = on;

	return __send_req(&req, IO_REQ_FLAGS, handle, fdesc, sizeof *rf) != sizeof *rf;
}

int xu_io_tcp_keepalive(uint32_t handle, uint32_t fdesc, int enable, int delay)
{
	struct request req;
	struct req_flags *rf = &req.u.flags;

	rf->flag = REQ_FLAGS_TCP_KEEPALIVE;
	rf->how = enable;
	rf->reserved = delay;

	return __send_req(&req, IO_REQ_FLAGS, handle, fdesc, sizeof *rf) != sizeof *rf;
}

int xu_io_close(uint32_t handle, uint32_t fdesc)
{
	struct request req;

	__send_req(&req, IO_REQ_CLOSE, handle, fdesc, 0);

	return 0;
}

