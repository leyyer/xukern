#include "uv.h"
#include "xu_impl.h"

struct xu_tcp {
	uv_tcp_t handle;
	xuctx_t  ctx;
	void *udata;
	void (*on_con)(struct xu_tcp *, struct xu_tcp *, int status);
	void (*recv)(struct xu_tcp *, const void *, int);
};

xu_tcp_t xu_tcp_open(xuctx_t ctx)
{
	xu_tcp_t tcp = xu_calloc(1, sizeof *tcp);

	if (uv_tcp_init(xu_ctx_loop(ctx), &tcp->handle) != 0) {
		goto failed;
	}

	tcp->ctx = ctx;

	return tcp;
failed:
	xu_free(tcp);
	return NULL;
}

xu_tcp_t xu_tcp_open_with_fd(xuctx_t ctx, int fd)
{
	xu_tcp_t tcp = xu_calloc(1, sizeof *tcp);

	if (uv_tcp_init(xu_ctx_loop(ctx), &tcp->handle) != 0) {
		goto failed;
	}

	if (uv_tcp_open(&tcp->handle, fd) != 0) {
		goto failed;
	}

	tcp->ctx = ctx;

	return tcp;
failed:
	xu_free(tcp);
	return NULL;
}

static void __free(uv_handle_t *h)
{
	xu_tcp_t tcp = (xu_tcp_t)h;
	xu_println("tcp freeing %p", h);
	xu_free(tcp);
}

void xu_tcp_close(xu_tcp_t tcp)
{
	if (uv_is_active((uv_handle_t *)&tcp->handle) || !uv_is_closing((uv_handle_t *)&tcp->handle))
		uv_close((uv_handle_t *)&tcp->handle, __free);
}

void xu_tcp_set_data(xu_tcp_t tcp, void *data)
{
	tcp->udata = data;
}

void *xu_tcp_get_data(xu_tcp_t tcp)
{
	return tcp->udata;
}

int xu_tcp_recv_buffer_size(xu_tcp_t tcp, int *value)
{
	return uv_recv_buffer_size((uv_handle_t *)&tcp->handle, value);
}

int xu_tcp_send_buffer_size(xu_tcp_t tcp, int *value)
{
	return uv_send_buffer_size((uv_handle_t *)&tcp->handle, value);
}

int xu_tcp_nodelay(xu_tcp_t tcp, int enable)
{
	return uv_tcp_nodelay(&tcp->handle, enable);
}

int xu_tcp_keepalive(xu_tcp_t tcp, int enable, int delay_in_seconds)
{
	return uv_tcp_keepalive(&tcp->handle, enable, delay_in_seconds);
}

int xu_tcp_bind(xu_tcp_t tcp, const char *address, int port)
{
	int err;
	struct sockaddr_in addr;

	if (address == NULL)
		address = "0.0.0.0";

	err = uv_ip4_addr(address,  port, &addr);
	if (err == 0) {
		err = uv_tcp_bind(&tcp->handle, (struct sockaddr *)&addr, 0);
	}
	return (err);
}

int xu_tcp_bind6(xu_tcp_t tcp, const char *address, int port)
{
	int err;
	struct sockaddr_in6 addr;

	if (address == NULL)
		address = "::0";

	err = uv_ip6_addr(address,  port, &addr);
	if (err == 0) {
		err = uv_tcp_bind(&tcp->handle, (struct sockaddr *)&addr, 0);
	}
	return (err);
}

static void __on_connection(uv_stream_t *stream, int status)
{
	xu_tcp_t tcp = (xu_tcp_t)stream;

	if (tcp->on_con) {
		xu_tcp_t client = NULL;
		if (status == 0) {
			client = xu_tcp_open(tcp->ctx);
			if (uv_accept(stream, (uv_stream_t *)&client->handle))
				return;
		}
		tcp->on_con(tcp, client, status);
	}
}

int xu_tcp_listen(xu_tcp_t tcp, int backlog, void (*on_con)(xu_tcp_t server, xu_tcp_t client, int status))
{
	tcp->on_con = on_con;
	return uv_listen((uv_stream_t *)&tcp->handle, backlog, __on_connection);
}

struct tcp_con_req {
	uv_connect_t req;
	xu_tcp_t tcp;
	void (*con)(xu_tcp_t , int);
};

static void __connected(uv_connect_t *con, int status)
{
	struct tcp_con_req *tcr = (struct tcp_con_req *)con;

	if (tcr->con)
		tcr->con(tcr->tcp, status);

	xu_free(tcr);
}

int xu_tcp_connect(xu_tcp_t tcp, const char *addr, int port, void (*con)(xu_tcp_t, int status))
{
	struct sockaddr_in adr;
	int err;
	struct tcp_con_req *tcr;

	if ((err = uv_ip4_addr(addr, port, &adr)) != 0)
		return err;

	tcr = xu_malloc(sizeof *tcr);
	tcr->tcp = tcp;
	tcr->con = con;
	err = uv_tcp_connect(&tcr->req, &tcp->handle, (struct sockaddr *)&adr, __connected);
	if (err) {
		xu_free(tcr);
	}
	return err;
}

int xu_tcp_connect6(xu_tcp_t tcp, const char *addr, int port, void (*con)(xu_tcp_t, int status))
{
	struct sockaddr_in6 adr;
	int err;
	struct tcp_con_req *tcr;

	if ((err = uv_ip6_addr(addr, port, &adr)) != 0)
		return err;

	tcr = xu_malloc(sizeof *tcr);
	tcr->tcp = tcp;
	tcr->con = con;
	err = uv_tcp_connect(&tcr->req, &tcp->handle, (struct sockaddr *)&adr, __connected);
	if (err) {
		xu_free(tcr);
	}
	return err;
}

struct tcp_write_req {
	uv_write_t req;
	xu_tcp_t tcp;
	void (*cb)(xu_tcp_t, int);
};

static void __write_done(uv_write_t *req, int status)
{
	struct tcp_write_req *twr = (struct tcp_write_req *)req;

	if (twr->cb)
		twr->cb(twr->tcp, status);
	xu_free(twr);
}

int xu_tcp_send(xu_tcp_t tcp, struct xu_buf buf[], int nbuf, void (*cb)(xu_tcp_t, int status))
{
	struct tcp_write_req *req;
	uv_buf_t vb[nbuf];
	int err, i;

	for (i = 0; i < nbuf; ++i) {
		vb[i].base = buf[i].base;
		vb[i].len = buf[i].len;
	}
	req = xu_calloc(1, sizeof *req);
	req->tcp = tcp;
	req->cb = cb;
	err = uv_write(&req->req, (uv_stream_t *)&tcp->handle, vb, nbuf, __write_done);
	if (err)
		xu_free(req);
	return err;
}

static void __on_alloc(uv_handle_t *handle, size_t size, uv_buf_t *buf)
{
	buf->base = xu_calloc(1, size);
	buf->len  = size;
}

static void __on_recv(uv_stream_t *stream, int nread, const uv_buf_t *buf)
{
	xu_tcp_t tcp = (xu_tcp_t)stream;

	if (nread == 0) {
		goto skip;
	}

	if (nread == UV_EOF) {
		printf("recv eof\n");
		nread = 0;
	}

	if (tcp->recv)
		tcp->recv(tcp, buf->base, nread);
skip:
	if (buf->base)
		xu_free(buf->base);
}

int xu_tcp_recv_start(xu_tcp_t tcp, void (*cb)(xu_tcp_t, const void *buf, int len))
{
	tcp->recv = cb;
	return uv_read_start((uv_stream_t *)&tcp->handle, __on_alloc, __on_recv);
}

int xu_tcp_recv_stop(xu_tcp_t tcp)
{
	return uv_read_stop((uv_stream_t *)&tcp->handle);
}

