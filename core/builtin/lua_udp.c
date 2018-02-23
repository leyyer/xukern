#include "uv.h"
#include "xu_impl.h"
#include "lua_buffer.h"

#define SOCK_MTDGRAM "mt.Dgram"
#define SOCK_MTADDR  "mt.SockAddr"
#define SOCKADDR()  (luaL_checkudata(L, 1, SOCK_MTADDR))
#define UDP()       (luaL_checkudata(L, 1, SOCK_MTDGRAM))

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
		udp = xu_udp_new(ctx);
	else
		udp = xu_udp_new_with_fd(ctx, fd);

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
	const char *addr;
	int port, err;
	int top = lua_gettop(L);

	switch (top) {
		case 3:
			addr = luaL_checkstring(L, 2);  /* <2> address */
			port = luaL_checkinteger(L, 3); /* <3> port */
			break;
		case 2:
			port = luaL_checkinteger(L, 2); /* <2> only port */
			if (uwr->is_ipv4) {
				addr = "0.0.0.0";
			} else {
				addr = "::0";
			}
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
	xu_udp_free(udp);
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

void init_lua_udp(lua_State *L, xuctx_t ctx)
{
	__sock_addr_mt(L);
	__sock_dgram(L, ctx);
}

