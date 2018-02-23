#include "uv.h"
#include "xu_impl.h"
#include "lua_buffer.h"

#define SOCK_MTDGRAM  "mt.Dgram"
#define SOCK_MTSTREAM "mt.Stream"

#define SOCK_MTADDR  "mt.SockAddr"
#define SOCKADDR()  (luaL_checkudata(L, 1, SOCK_MTADDR))
#define UDP()       (luaL_checkudata(L, 1, SOCK_MTDGRAM))
#define TCP()       (luaL_checkudata(L, 1, SOCK_MTSTREAM))

static int __sock_address(lua_State *L)
{
	struct sockaddr *addr = SOCKADDR();
	char dst[128] = {0};
	int err = -1;

	switch (addr->sa_family) {
		case AF_INET:
			err = uv_ip4_name((struct sockaddr_in *)addr, dst, sizeof dst);
			break;
		case AF_INET6:
			err = uv_ip6_name((struct sockaddr_in6 *)addr, dst, sizeof dst);
			break;
	}

	if (err == 0) {
		lua_pushstring(L, dst);
		return 1;
	}

	return 0;
}

static int __sock_addr_fmaily(lua_State *L)
{
	struct sockaddr *addr = SOCKADDR();
	int err = 1;
	switch (addr->sa_family) {
		case AF_INET:
			lua_pushstring(L, "ipv4");
			break;
		case AF_INET6:
			lua_pushstring(L, "ipv6");
			break;
		default:
			err = 0;
			break;
	}

	return (err);
}

#ifdef DBG_ADDR_GC
static int __sock_addr_gc(lua_State *L)
{
	struct sockaddr *addr = SOCKADDR();
	xu_println("address gc %p", addr);
	return 0;
}
#endif

static int __sock_port(lua_State *L)
{
	struct sockaddr *addr = SOCKADDR();
	struct sockaddr_in *s = (struct sockaddr_in *)addr;
	int port;

	port = ntohs(s->sin_port);
	lua_pushinteger(L, port);
	return 1;
}

static void __create_metatable(lua_State *L, const char *name, luaL_Reg reg[])
{
	luaL_newmetatable(L, name);
	lua_pushstring(L, "__index");
	lua_pushvalue(L, -2);
	lua_settable(L, -3);
	luaL_openlib(L, NULL, reg, 0);
	lua_pop(L, 1);
}

static void __sock_addr_mt(lua_State *L)
{
	static luaL_Reg sa[] = {
		{"family",  __sock_addr_fmaily},
		{"address", __sock_address},
		{"port",  __sock_port},
#ifdef DBG_ADDR_GC
		{"__gc", __sock_addr_gc},
#endif
		{NULL, NULL}
	};

	__create_metatable(L, SOCK_MTADDR, sa);
}

struct udp_wrap {
	xu_udp_t udp;
	lua_State *L;
	int is_ipv4;
	int recv;
	int send;
};

static int __sock_udp_new__(lua_State *L, int fd)
{
	struct udp_wrap *uwr;
	xu_udp_t udp;
	const char *str;
	int ipv4;
	xuctx_t ctx = lua_touserdata(L, lua_upvalueindex(1));

	str = luaL_checkstring(L, 1);
	if (strcasecmp(str, "udp4") == 0) {
		ipv4 = 1;
	} else {
		ipv4 = 0;
	}

	if (fd < 0)
		udp = xu_udp_open(ctx);
	else
		udp = xu_udp_open_with_fd(ctx, fd);

	if (!udp)
		return 0;

	uwr = lua_newuserdata(L, sizeof *uwr);

	uwr->udp = udp;
	uwr->is_ipv4 = ipv4;
	uwr->recv = LUA_REFNIL;
	uwr->send = LUA_REFNIL;
	uwr->L = L;
	xu_udp_set_data(udp, uwr);

	luaL_getmetatable(L, SOCK_MTDGRAM);
	lua_setmetatable(L, -2);

	return 1;
}

static int __sock_udp_new(lua_State *L)
{
	return __sock_udp_new__(L, -1);
}

static int __sock_udp_new_with_fd(lua_State *L)
{
	int fd = luaL_checkinteger(L, 1);
	return __sock_udp_new__(L, fd);
}

static int __sock_udp_bind(lua_State *L)
{
	struct udp_wrap *uwr = UDP();
	xu_udp_t udp = uwr->udp;
	const char *addr = NULL;
	int port, err;
	int top = lua_gettop(L);

	switch (top) {
		case 3:
			addr = luaL_checkstring(L, 2);  /* <2> address */
			port = luaL_checkinteger(L, 3); /* <3> port */
			break;
		case 2:
			port = luaL_checkinteger(L, 2); /* <2> only port */
			break;
		default:
			return 0;
	}

	if (uwr->is_ipv4)
		err = xu_udp_bind(udp, addr, port);
	else
		err = xu_udp_bind6(udp, addr, port);
	lua_pushboolean(L, err == 0);
	return 1;
}

static int __sock_udp_close(lua_State *L)
{
	struct udp_wrap *uwr = UDP();
	xu_udp_t udp = uwr->udp;

	luaL_unref(L, LUA_REGISTRYINDEX, uwr->recv);
	luaL_unref(L, LUA_REGISTRYINDEX, uwr->send);
	uwr->recv  = LUA_REFNIL;
	uwr->send = LUA_REFNIL;
	xu_udp_close(udp);
	return 0;
}

static int __sock_udp_peer(lua_State *L, const struct sockaddr *peer)
{
	struct sockaddr *adr;

	adr = lua_newuserdata(L, sizeof *adr);
	*adr = *peer;
	luaL_getmetatable(L, SOCK_MTADDR);
	lua_setmetatable(L, -2);
	return 1;
}

static void __on_recv(xu_udp_t udp, const void *data, int nread, const struct sockaddr *addr)
{
	struct udp_wrap *uwr;
	int r;
	struct buffer *buf;
	lua_State *L;

	uwr = xu_udp_get_data(udp);
	L = uwr->L;
	if (uwr->recv != LUA_REFNIL) {
		int top = lua_gettop(L);
		xu_println("%s: top = %d", __func__, top);
		if (top != 1) {
			lua_pushcfunction(L, xu_luatraceback);
		} else {
			assert(top == 1);
		}
		lua_rawgeti(L, LUA_REGISTRYINDEX, uwr->recv);
		lua_pushinteger(L, nread); /* <1>: length */
		if (nread > 0) {           /* <2>: buffer or nil */
			buf = buffer_new(L, nread);
			memcpy(buf->data, data, nread);
		} else {
			lua_pushnil(L);
		}
		if (addr) {                 /* <3>: address or nil */
			__sock_udp_peer(L, addr);
		} else {
			lua_pushnil(L);
		}
		r = lua_pcall(L, 3, 0, 1);
		if (r != 0) {
			xu_println("%s: %s", __func__, lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
}

static int __sock_udp_recv_start(lua_State *L)
{
	struct udp_wrap *uwr = UDP();
	xu_udp_t udp = uwr->udp;
	int err = -1;

	if (lua_type(L, 2) != LUA_TFUNCTION) {
		goto skip;
	}

	if (uwr->recv != LUA_REFNIL) {
		luaL_unref(L, LUA_REGISTRYINDEX, uwr->recv);
	}
	uwr->recv = luaL_ref(L, LUA_REGISTRYINDEX);
	err = xu_udp_recv_start(udp, __on_recv);
skip:
	lua_pushboolean(L, err == 0);
	return 1;
}

static int __sock_udp_on_send(lua_State *L)
{
	struct udp_wrap *uwr = UDP();
	int err = 0;

	if (lua_type(L, 2) != LUA_TFUNCTION) {
		err = -1;
		goto skip;
	}
	if (uwr->send != LUA_REFNIL) {
		luaL_unref(L, LUA_REGISTRYINDEX, uwr->send);
	}
	uwr->send = luaL_ref(L, LUA_REGISTRYINDEX);
skip:
	lua_pushboolean(L, err == 0);
	return 1;
}

static int __sock_udp_recv_stop(lua_State *L)
{
	struct udp_wrap *uwr = UDP();
	xu_udp_t udp = uwr->udp;
	int err;

	err = xu_udp_recv_stop(udp);
	luaL_unref(L, LUA_REGISTRYINDEX, uwr->recv);
	uwr->recv = LUA_REFNIL;
	lua_pushboolean(L, err == 0);
	return 1;
}

static void __on_send(xu_udp_t udp, int status)
{
	struct udp_wrap *uwr;
	int r, top;
	lua_State *L;

	uwr = xu_udp_get_data(udp);
	if (uwr->send != LUA_REFNIL) {
		L = uwr->L;
		top = lua_gettop(L);
		if (top != 1) {
			lua_pushcfunction(L, xu_luatraceback);
		} else {
			assert(top == 1);
		}
		lua_rawgeti(L, LUA_REGISTRYINDEX, uwr->recv);
		lua_pushinteger(L, status); /* <1>: status */
		r = lua_pcall(L, 1, 0, 1);
		if (r != 0) {
			xu_println("%s: %s", __func__, lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
	xu_println("send status %d", status);
}

static int __sock_udp_send(lua_State *L)
{
	struct udp_wrap *uwr = UDP();
	xu_udp_t udp = uwr->udp;
	int err = 0;
	const char *s, *addr;
	size_t sz;
	int port;
	struct xu_buf xb;
	struct buffer *buf;

	addr = luaL_checkstring(L, 2);  /* <2> remote address */
	port = luaL_checkinteger(L, 3); /* <3> remote port */
	switch (lua_type(L, 4)) {
		case LUA_TSTRING:           /* <4> string */
			s = luaL_checklstring(L, 4, &sz);
			xb.base = (void *)s;
			xb.len = sz;
			break;
		case LUA_TUSERDATA:
			buf = BUFFER(4);       /* <4> data */
			if (buf) {
				xb.base = buf->data;
				xb.len = buf->cap;
			} else {
				err = -1;
			}
			break;
		default:
			err = -2;
			break;
	}

	if (err != 0) {
		goto skip;
	}

	if (uwr->is_ipv4) {
		err = xu_udp_send(udp, &xb, 1, addr, port, __on_send);
	} else {
		err = xu_udp_send6(udp, &xb, 1, addr, port, __on_send);
	}
skip:
	lua_pushboolean(L, err == 0);
	return 1;
}

static int __sock_udp_address(lua_State *L)
{
	struct udp_wrap *uwr = UDP();
	xu_udp_t udp = uwr->udp;
	struct sockaddr *adr;
	size_t sz = sizeof *adr;

	adr = lua_newuserdata(L, sz);
	xu_udp_address(udp, adr, (void *)&sz);
	luaL_getmetatable(L, SOCK_MTADDR);
	lua_setmetatable(L, -2);

	return 1;
}

static int __sock_udp_set_multicast_interface(lua_State *L)
{
	struct udp_wrap *uwr = UDP();
	xu_udp_t udp = uwr->udp;
	int err = -1;
	const char *ia;

	ia = luaL_checkstring(L, 2);
	err = xu_udp_set_multicast_interface(udp, ia);
	lua_pushboolean(L, err == 0);
	return 1;
}

static int __sock_membership(lua_State *L, int join)
{
	struct udp_wrap *uwr = UDP();
	xu_udp_t udp = uwr->udp;
	int err = -1;
	const char *ma, *ia;

	ma = luaL_checkstring(L, 2);
	if (lua_gettop(L) == 3) {
		ia = luaL_checkstring(L, 3);
	} else {
		if (uwr->is_ipv4) {
			ia = "0.0.0.0";
		} else {
			ia = "::0";
		}
	}
	if (join) {
		err = xu_udp_add_membership(udp, ma, ia);
	} else {
		err = xu_udp_drop_membership(udp, ma, ia);
	}
	lua_pushboolean(L, err == 0);
	return 1;
}

static int __sock_udp_add_membership(lua_State *L)
{
	return __sock_membership(L, 1);
}

static int __sock_udp_drop_membership(lua_State *L)
{
	return __sock_membership(L, 0);
}

static int __sock_udp_set_broadcast(lua_State *L)
{
	struct udp_wrap *uwr = UDP();
	xu_udp_t udp = uwr->udp;
	int err = -1;
	int on;

	on = lua_toboolean(L, 2);
	err = xu_udp_set_broadcast(udp, on);

	lua_pushboolean(L, err == 0);
	return 1;
}

static int __sock_udp_multicast_loopback(lua_State *L)
{
	struct udp_wrap *uwr = UDP();
	xu_udp_t udp = uwr->udp;
	int err = -1;
	int on;

	on = lua_toboolean(L, 2);
	err = xu_udp_set_multicast_loopback(udp, on);

	lua_pushboolean(L, err == 0);
	return 1;
}

static int __sock_udp_multicast_ttl(lua_State *L)
{
	struct udp_wrap *uwr = UDP();
	xu_udp_t udp = uwr->udp;
	int err = -1;
	int on;

	on = lua_toboolean(L, 2);
	err = xu_udp_set_multicast_ttl(udp, on);

	lua_pushboolean(L, err == 0);
	return 1;
}

static int __sock_udp_ttl(lua_State *L)
{
	struct udp_wrap *uwr = UDP();
	xu_udp_t udp = uwr->udp;
	int err = -1;
	int on;

	on = lua_toboolean(L, 2);
	err = xu_udp_set_ttl(udp, on);

	lua_pushboolean(L, err == 0);

	return 1;
}

static int __sock_get_recv_buffer_size(lua_State *L)
{
	struct udp_wrap *uwr = UDP();
	xu_udp_t udp = uwr->udp;
	int value = 0;
	int err;

	err = xu_udp_recv_buffer_size(udp, &value);
	lua_pushinteger(L, value);
	lua_pushboolean(L, err == 0);

	return 2;
}

static int __sock_get_send_buffer_size(lua_State *L)
{
	struct udp_wrap *uwr = UDP();
	xu_udp_t udp = uwr->udp;
	int value = 0;
	int err;

	err = xu_udp_send_buffer_size(udp, &value);
	lua_pushinteger(L, value);
	lua_pushboolean(L, err == 0);

	return 2;
}

static int __sock_set_recv_buffer_size(lua_State *L)
{
	struct udp_wrap *uwr = UDP();
	xu_udp_t udp = uwr->udp;
	int value;
	int err;

	value = luaL_checkinteger(L, 2);
	err = xu_udp_recv_buffer_size(udp, &value);
	lua_pushinteger(L, value);
	lua_pushboolean(L, err == 0);

	return 2;
}

static int __sock_set_send_buffer_size(lua_State *L)
{
	struct udp_wrap *uwr = UDP();
	xu_udp_t udp = uwr->udp;
	int value;
	int err;

	value = luaL_checkinteger(L, 2);
	err = xu_udp_send_buffer_size(udp, &value);
	lua_pushinteger(L, value);
	lua_pushboolean(L, err == 0);

	return 2;
}

static void __sock_dgram(lua_State *L, xuctx_t ctx)
{
	static luaL_Reg sd[] = {
		{"new", __sock_udp_new},
		{"newWithFd", __sock_udp_new_with_fd},
		{NULL, NULL}
	};

	static luaL_Reg mt_sd[] = {
		{"bind",     __sock_udp_bind},
		{"send",     __sock_udp_send},
		{"close",    __sock_udp_close},
		{"recvStart", __sock_udp_recv_start},
		{"recvStop", __sock_udp_recv_stop},
		{"address",  __sock_udp_address},
		{"addMembership", __sock_udp_add_membership},
		{"dropMembership", __sock_udp_drop_membership},
		{"setMulticastInterface", __sock_udp_set_multicast_interface},
		{"setMulticastLoopback", __sock_udp_multicast_loopback},
		{"setMulticastTTL",     __sock_udp_multicast_ttl},
		{"getRecvBufferSize", __sock_get_recv_buffer_size},
		{"getSendBufferSize", __sock_get_send_buffer_size},
		{"setRecvBufferSize", __sock_set_recv_buffer_size},
		{"setSendBufferSize", __sock_set_send_buffer_size},
		{"setTTL",  __sock_udp_ttl},
		{"setBroadcast", __sock_udp_set_broadcast},
		{"onSend",  __sock_udp_on_send},
		{"__gc",    __sock_udp_close},
		{NULL, NULL}
	};
	__create_metatable(L, SOCK_MTDGRAM, mt_sd);
	lua_pushlightuserdata(L, ctx);
	luaL_openlib(L, "Udp", sd, 1);
	lua_pop(L, 1);
}

struct tcp_wrap {
	xu_tcp_t tcp;
	lua_State *L;

	int is_ipv4;

	int recv;
	int send;
	int connect;
	int accept;
};

static struct tcp_wrap * __tcp_new(lua_State *L, xu_tcp_t tcp, int ipv4)
{
	struct tcp_wrap *uwr;

	uwr = lua_newuserdata(L, sizeof *uwr);

	uwr->tcp = tcp;
	uwr->is_ipv4 = ipv4;
	uwr->recv = LUA_REFNIL;
	uwr->send = LUA_REFNIL;
	uwr->connect = LUA_REFNIL;
	uwr->accept = LUA_REFNIL;
	uwr->L = L;
	xu_tcp_set_data(tcp, uwr);

	luaL_getmetatable(L, SOCK_MTSTREAM);
	lua_setmetatable(L, -2);

	return uwr;
}

static int __sock_tcp_new__(lua_State *L, int fd)
{
	const char *str;
	xu_tcp_t tcp;
	int ipv4;
	xuctx_t ctx = lua_touserdata(L, lua_upvalueindex(1));

	str = luaL_checkstring(L, 1);
	if (strcasecmp(str, "tcp4") == 0) {
		ipv4 = 1;
	} else {
		ipv4 = 0;
	}

	if (fd < 0)
		tcp = xu_tcp_open(ctx);
	else
		tcp = xu_tcp_open_with_fd(ctx, fd);

	if (!tcp)
		return 0;

	__tcp_new(L, tcp, ipv4);

	return 1;
}

static int __sock_tcp_new(lua_State *L)
{
	return __sock_tcp_new__(L, -1);
}

static int __sock_tcp_new_with_fd(lua_State *L)
{
	int fd = luaL_checkinteger(L, 1);
	return __sock_tcp_new__(L, fd);
}

static int __sock_tcp_close(lua_State *L)
{
	struct tcp_wrap *uwr = TCP();
	xu_tcp_t tcp = uwr->tcp;

	luaL_unref(L, LUA_REGISTRYINDEX, uwr->recv);
	luaL_unref(L, LUA_REGISTRYINDEX, uwr->send);
	luaL_unref(L, LUA_REGISTRYINDEX, uwr->connect);
	luaL_unref(L, LUA_REGISTRYINDEX, uwr->accept);
	uwr->recv  = LUA_REFNIL;
	uwr->send = LUA_REFNIL;
	uwr->connect = LUA_REFNIL;
	uwr->accept = LUA_REFNIL;
	xu_tcp_close(tcp);
	return 0;
}

static void __on_tcp_recv(xu_tcp_t udp, const void *data, int nread)
{
	struct tcp_wrap *uwr;
	int r;
	struct buffer *buf;
	lua_State *L;

	uwr = xu_tcp_get_data(udp);
	L = uwr->L;
	if (uwr->recv != LUA_REFNIL) {
		int top = lua_gettop(L);
		if (top != 1) {
			lua_pushcfunction(L, xu_luatraceback);
		} else {
			assert(top == 1);
		}
		lua_rawgeti(L, LUA_REGISTRYINDEX, uwr->recv);
		printf("nread = %d\n", nread);
		lua_pushinteger(L, nread); /* <1>: length */
		if (nread > 0) {           /* <2>: buffer or nil */
			buf = buffer_new(L, nread);
			memcpy(buf->data, data, nread);
		} else {
			lua_pushnil(L);
		}
		r = lua_pcall(L, 2, 0, 1);
		if (r != 0) {
			xu_println("%s: %s", __func__, lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
}

static int __sock_tcp_recv_start(lua_State *L)
{
	struct tcp_wrap *uwr = TCP();
	xu_tcp_t udp = uwr->tcp;
	int err = -1;

	if (lua_type(L, 2) != LUA_TFUNCTION) {
		goto skip;
	}

	if (uwr->recv != LUA_REFNIL) {
		luaL_unref(L, LUA_REGISTRYINDEX, uwr->recv);
	}
	uwr->recv = luaL_ref(L, LUA_REGISTRYINDEX);
	err = xu_tcp_recv_start(udp, __on_tcp_recv);
skip:
	lua_pushboolean(L, err == 0);
	return 1;
}

static int __sock_tcp_recv_stop(lua_State *L)
{
	struct tcp_wrap *uwr = TCP();
	xu_tcp_t udp = uwr->tcp;
	int err;

	err = xu_tcp_recv_stop(udp);
	luaL_unref(L, LUA_REGISTRYINDEX, uwr->recv);
	uwr->recv = LUA_REFNIL;
	lua_pushboolean(L, err == 0);
	return 1;
}

static int __sock_tcp_on_send(lua_State *L)
{
	struct tcp_wrap *uwr = TCP();
	int err = 0;

	if (lua_type(L, 2) != LUA_TFUNCTION) {
		err = -1;
		goto skip;
	}
	if (uwr->send != LUA_REFNIL) {
		luaL_unref(L, LUA_REGISTRYINDEX, uwr->send);
	}
	uwr->send = luaL_ref(L, LUA_REGISTRYINDEX);
skip:
	lua_pushboolean(L, err == 0);
	return 1;
}

static void __on_tcp_send(xu_tcp_t udp, int status)
{
	struct tcp_wrap *uwr;
	int r, top;
	lua_State *L;

	uwr = xu_tcp_get_data(udp);
	if (uwr->send != LUA_REFNIL) {
		L = uwr->L;
		top = lua_gettop(L);
		if (top != 1) {
			lua_pushcfunction(L, xu_luatraceback);
		} else {
			assert(top == 1);
		}
		lua_rawgeti(L, LUA_REGISTRYINDEX, uwr->recv);
		lua_pushinteger(L, status); /* <1>: status */
		r = lua_pcall(L, 1, 0, 1);
		if (r != 0) {
			xu_println("%s: %s", __func__, lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
//	xu_println("send status %d", status);
}

static int __sock_tcp_send(lua_State *L)
{
	struct tcp_wrap *uwr = TCP();
	xu_tcp_t tcp = uwr->tcp;
	int err = 0;
	const char *s;
	struct xu_buf xb;
	struct buffer *buf;
	size_t sz = 0;

	switch (lua_type(L, 2)) {
		case LUA_TSTRING:           /* <2> string */
			s = luaL_checklstring(L, 2, &sz);
			xb.base = (void *)s;
			xb.len = sz;
			printf("sz = %u\n", sz);
			break;
		case LUA_TUSERDATA:
			buf = BUFFER(2);       /* <2> data */
			if (buf) {
				xb.base = buf->data;
				xb.len = buf->cap;
			} else {
				err = -1;
			}
			break;
		default:
			err = -2;
			break;
	}

	if (err != 0) {
		goto skip;
	}

	err = xu_tcp_send(tcp, &xb, 1, __on_tcp_send);
skip:
	lua_pushboolean(L, err == 0);
	return 1;
}

static void __on_accept(xu_tcp_t server, xu_tcp_t tcp, int status)
{
	struct tcp_wrap *twr = xu_tcp_get_data(server);
	lua_State *L;
	int r, top;

	//xu_println("on accept");
	if (twr->accept != LUA_REFNIL) {
		L = twr->L;
		top = lua_gettop(L);
		if (top != 1) {
			lua_pushcfunction(L, xu_luatraceback);
		} else {
			assert(top == 1);
		}
		lua_rawgeti(L, LUA_REGISTRYINDEX, twr->accept);
		lua_pushboolean(L, status == 0); /* <1>: status */
		if (tcp) {                       /* <2>: new client */
			__tcp_new(L, tcp, twr->is_ipv4);
		} else {
			lua_pushnil(L);
		}
		r = lua_pcall(L, 2, 0, 1);
		if (r != 0) {
			xu_println("%s: %s", __func__, lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	} else {
		xu_println("on accept callback nil");
	}
}

static int __sock_tcp_listen(lua_State *L)
{
	struct tcp_wrap *uwr = TCP();
	xu_tcp_t tcp = uwr->tcp;
	int backlog;
	int err;

	backlog = luaL_checkinteger(L, 2);
	if (uwr->accept != LUA_REFNIL)
		luaL_unref(L, LUA_REGISTRYINDEX, uwr->accept);
	uwr->accept = luaL_ref(L, LUA_REGISTRYINDEX);
	err = xu_tcp_listen(tcp, backlog, __on_accept);
	lua_pushboolean(L, err == 0);
	return 1;
}

static void __on_connect(xu_tcp_t tcp, int status)
{
	struct tcp_wrap *twr = xu_tcp_get_data(tcp);

	if (twr->connect != LUA_REFNIL) {
		lua_State *L = twr->L;
		int top = lua_gettop(L);
		if (top != 1) {
			lua_pushcfunction(L, xu_luatraceback);
		} else {
			assert(top == 1);
		}
		lua_rawgeti(L, LUA_REGISTRYINDEX, twr->connect);
		lua_pushinteger(L, status); /* <1>: status */
		int r = lua_pcall(L, 1, 0, 1);
		if (r != 0) {
			xu_println("%s: %s", __func__, lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
}

static int __sock_tcp_connect(lua_State *L)
{
	struct tcp_wrap *uwr = TCP();
	xu_tcp_t tcp = uwr->tcp;
	const char *addr;
	int port;
	int err;

	addr = luaL_checkstring(L, 2); 
	port = luaL_checkinteger(L, 3);

	if (uwr->connect != LUA_REFNIL)
		luaL_unref(L, LUA_REGISTRYINDEX, uwr->connect);
	uwr->connect = luaL_ref(L, LUA_REGISTRYINDEX);

	if (uwr->is_ipv4)
		err = xu_tcp_connect(tcp, addr, port, __on_connect);
	else 
		err = xu_tcp_connect6(tcp, addr, port, __on_connect);

	lua_pushboolean(L, err == 0);
	return 1;
}

static int __sock_tcp_bind(lua_State *L)
{
	struct tcp_wrap *twr = TCP();
	xu_tcp_t tcp = twr->tcp;
	const char *addr = NULL;
	int port, err;
	int top = lua_gettop(L);

	switch (top) {
		case 3:
			addr = luaL_checkstring(L, 2);  /* <2> address */
			port = luaL_checkinteger(L, 3); /* <3> port */
			break;
		case 2:
			port = luaL_checkinteger(L, 2); /* <2> only port */
			break;
		default:
			return 0;
	}

	if (twr->is_ipv4)
		err = xu_tcp_bind(tcp, addr, port);
	else
		err = xu_tcp_bind6(tcp, addr, port);
	lua_pushboolean(L, err == 0);
	return 1;
}

static void __sock_stream(lua_State *L, xuctx_t ctx)
{
	static luaL_Reg sd[] = {
		{"new", __sock_tcp_new},
		{"newWithFd", __sock_tcp_new_with_fd},
		{NULL, NULL}
	};

	static luaL_Reg mt_sd[] = {
		{"close", __sock_tcp_close},
		{"recvStart", __sock_tcp_recv_start},
		{"recvStop",  __sock_tcp_recv_stop},
		{"bind",      __sock_tcp_bind},
		{"listen",    __sock_tcp_listen},
		{"connect",   __sock_tcp_connect},
		{"send",      __sock_tcp_send},
		{"onSend",    __sock_tcp_on_send},
		{"__gc",      __sock_tcp_close},
		{NULL, NULL}
	};

	__create_metatable(L, SOCK_MTSTREAM, mt_sd);
	lua_pushlightuserdata(L, ctx);
	luaL_openlib(L, "Tcp", sd, 1);
	lua_pop(L, 1);
}

void init_lua_net(lua_State *L, xuctx_t ctx)
{
	__sock_addr_mt(L);
	__sock_dgram(L, ctx);
	__sock_stream(L, ctx);
}

