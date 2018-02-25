#include "uv.h"
#include "xu_impl.h"

struct xu_udp {
	uv_udp_t handle;
	void *udata;
	void (*recv)(xu_udp_t, const void *buf, ssize_t nread, const struct sockaddr *addr);
};

struct udp_send_req {
	uv_udp_send_t req;
	xu_udp_t udp;
	void (*cb)(xu_udp_t, int status);
};

xu_udp_t xu_udp_open(xuctx_t ctx)
{
	xu_udp_t udp;

	udp = xu_calloc(1, sizeof *udp);

	if (uv_udp_init(xu_ctx_loop(ctx), &udp->handle)) {
		xu_free(udp);
		return NULL;
	}
	uv_handle_set_data((uv_handle_t *)&udp->handle, ctx);
	return udp;
}

xu_udp_t xu_udp_open_with_fd(xuctx_t ctx, int fd)
{
	xu_udp_t udp;

	udp = xu_calloc(1, sizeof *udp);

	if (uv_udp_init(xu_ctx_loop(ctx), &udp->handle)) {
		xu_free(udp);
		return NULL;
	}

	if (uv_udp_open(&udp->handle, fd) != 0) {
		xu_free(udp);
		return NULL;
	}

	uv_handle_set_data((uv_handle_t *)&udp->handle, ctx);
	return udp;
}

static void __free(uv_handle_t *h)
{
	xu_udp_t udp = (xu_udp_t)h;
	xu_println("udp freeing %p", h);
	xu_free(udp);
}

void xu_udp_close(xu_udp_t udp)
{
	if (uv_is_active((uv_handle_t *)&udp->handle) || !uv_is_closing((uv_handle_t *)&udp->handle))
		uv_close((uv_handle_t *)&udp->handle, __free);
}

int xu_udp_bind(xu_udp_t udp, const char *addr, int port)
{
	struct sockaddr_in saddr;

	if (addr == NULL) {
		addr = "0.0.0.0";
	}
	if (uv_ip4_addr(addr, port, &saddr))
		return -1;
	return uv_udp_bind(&udp->handle, (struct sockaddr *)&saddr, UV_UDP_REUSEADDR);
}

int xu_udp_bind6(xu_udp_t udp, const char *addr, int port)
{
	struct sockaddr_in6 saddr;

	if (addr == NULL) {
		addr = "::0";
	}
	if (uv_ip6_addr(addr, port, &saddr))
		return -1;
	return uv_udp_bind(&udp->handle, (struct sockaddr *)&saddr, UV_UDP_REUSEADDR);
}

void xu_udp_set_data(xu_udp_t udp, void *data)
{
	void *saved = udp->udata;
	ATOM_CAS_POINTER(&udp->udata, saved, data);
}

void *xu_udp_get_data(xu_udp_t udp)
{
	return udp->udata;
}

static void __on_send(uv_udp_send_t *uus, int status)
{
	struct udp_send_req *usr;

	usr = (struct udp_send_req *)uus;
	if (usr->cb)
		usr->cb(usr->udp, status);
	xu_free(usr);
}

static int do_send(xu_udp_t udp, int family, struct xu_buf buf[], int nbuf, const char *addr, int port, void (*cb)(xu_udp_t, int status))
{
	uv_buf_t bufs[nbuf];
	char saddr[sizeof (struct sockaddr_in6)];
	struct udp_send_req *uus;

	int err = -1;
	switch (family) {
		case AF_INET:
			err = uv_ip4_addr(addr, port, (struct sockaddr_in *)&saddr);
			break;
		case AF_INET6:
			err = uv_ip6_addr(addr, port, (struct sockaddr_in6 *)&saddr);
			break;
	}

	if (err != 0)
		return err;

	for (int i = 0; i < nbuf; ++i) {
		bufs[i] = uv_buf_init(buf[i].base, buf[i].len);
	}
	uus = xu_calloc(1, sizeof *uus);
	uus->cb = cb;
	uus->udp = udp;
	err = uv_udp_send(&uus->req, &udp->handle,
			bufs, nbuf, (struct sockaddr *)&saddr, __on_send);
	if (err)
		xu_free(uus);

	return err;
}

int xu_udp_address(xu_udp_t udp, struct sockaddr *addr, int *size)
{
	return uv_udp_getsockname(&udp->handle, addr, size);
}

int xu_udp_send(xu_udp_t udp, struct xu_buf buf[], int nbuf, const char *addr, int port, void (*cb)(xu_udp_t, int status))
{
	return do_send(udp, AF_INET, buf, nbuf, addr,  port, cb);
}

int xu_udp_send6(xu_udp_t udp, struct xu_buf buf[], int nbuf, const char *addr, int port, void (*cb)(xu_udp_t, int status))
{
	return do_send(udp, AF_INET6, buf, nbuf, addr,  port, cb);
}

int xu_udp_add_membership(xu_udp_t udp, const char *multicast_addr, const char *interface_addr)
{
	return uv_udp_set_membership(&udp->handle, multicast_addr, interface_addr, UV_JOIN_GROUP);
}

int xu_udp_drop_membership(xu_udp_t udp, const char *multicast_addr, const char *interface_addr)
{
	return uv_udp_set_membership(&udp->handle, multicast_addr, interface_addr, UV_LEAVE_GROUP);
}

int xu_udp_set_multicast_loopback(xu_udp_t udp, int on)
{
	return uv_udp_set_multicast_loop(&udp->handle, on);
}

int xu_udp_set_multicast_ttl(xu_udp_t udp, int ttl)
{
	return uv_udp_set_multicast_ttl(&udp->handle, ttl);
}

int xu_udp_set_multicast_interface(xu_udp_t udp, const char *interface_addr)
{
	return uv_udp_set_multicast_interface(&udp->handle, interface_addr);
}

int xu_udp_set_ttl(xu_udp_t udp, int ttl)
{
	return uv_udp_set_ttl(&udp->handle, ttl);
}

int xu_udp_set_broadcast(xu_udp_t udp, int on)
{
	return uv_udp_set_broadcast(&udp->handle, on);
}

static void __on_recv(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf,
		const struct sockaddr *addr, unsigned int flags)
{
	xu_udp_t udp = (xu_udp_t)handle;

	if (nread == 0 && addr == NULL) {
		goto skip;
	}

	if (udp->recv)
		udp->recv(udp, buf->base, nread, addr);
skip:
	if (buf->base)
		xu_free(buf->base);
}

static void __on_alloc(uv_handle_t *handle, size_t size, uv_buf_t *buf)
{
	buf->base = xu_calloc(1, size);
	buf->len  = size;
}

int xu_udp_recv_start(xu_udp_t udp, void (*recv)(xu_udp_t, const void *buf, ssize_t nread, const struct sockaddr *addr))
{
	int err;

	udp->recv = recv;
	err = uv_udp_recv_start(&udp->handle, __on_alloc, __on_recv);
	if (err == UV_EALREADY)
		err = 0;
	return err;
}

int xu_udp_recv_stop(xu_udp_t udp)
{
	return uv_udp_recv_stop(&udp->handle);
}

int xu_udp_recv_buffer_size(xu_udp_t udp, int *value)
{
	return uv_recv_buffer_size((uv_handle_t *)&udp->handle, value);
}

int xu_udp_send_buffer_size(xu_udp_t udp, int *value)
{
	return uv_send_buffer_size((uv_handle_t *)&udp->handle, value);
}

