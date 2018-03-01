#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "xu_kern.h"
#include "xu_malloc.h"
#include "xu_util.h"
#include "xu_io.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lua.h"
#include "uv.h"

struct xulua {
	lua_State *L;
	uint32_t handle;
};

#define SOCK_MTADDR  "mt.SockAddr"
#define SOCKADDR(x)  (luaL_checkudata(L, (x), SOCK_MTADDR))

static int __sock_port(lua_State *L)
{
	union sockaddr_all *sa = SOCKADDR(1);
	struct sockaddr *addr = &sa->in;
	struct sockaddr_in *s = (struct sockaddr_in *)addr;
	int port;

	port = ntohs(s->sin_port);
	lua_pushinteger(L, port);
	return 1;
}

static int __sock_address(lua_State *L)
{
	union sockaddr_all *sa = SOCKADDR(1);
	struct sockaddr *addr = &sa->in;
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
	union sockaddr_all *sa = SOCKADDR(1);
	struct sockaddr *addr = &sa->in;
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

static void create_metatable(lua_State *L, const char *name, luaL_Reg reg[])
{
	luaL_newmetatable(L, name);
	lua_pushstring(L, "__index");
	lua_pushvalue(L, -2);
	lua_settable(L, -3);
	luaL_setfuncs(L, reg, 0);
	//luaL_openlib(L, NULL, reg, 0);
	lua_pop(L, 1);
}

static void __sock_addr_mt(lua_State *L)
{
	luaL_Reg sa[] = {
		{"family",  __sock_addr_fmaily},
		{"address", __sock_address},
		{"port",  __sock_port},
#ifdef DBG_ADDR_GC
		{"__gc", __sock_addr_gc},
#endif
		{NULL, NULL}
	};

	create_metatable(L, SOCK_MTADDR, sa);
}

static int __xie_get_event(lua_State *L)
{
	struct xu_io_event *xie = lua_touserdata(L, 1);

	lua_pushinteger(L, xie->event);
	return 1;
}

static int __xie_get_fd(lua_State *L)
{
	struct xu_io_event *xie = lua_touserdata(L, 1);
	lua_pushinteger(L, xie->fdesc);
	return 1;
}

static int __xie_get_len(lua_State *L)
{
	struct xu_io_event *xie = lua_touserdata(L, 1);
	lua_pushinteger(L, xie->size);
	return 1;
}

static int __xie_get_errcode(lua_State *L)
{
	struct xu_io_event *xie = lua_touserdata(L, 1);
	lua_pushinteger(L, xie->u.errcode);
	return 1;
}

static int __xie_get_address(lua_State *L)
{
	struct xu_io_event *xie = lua_touserdata(L, 1);
	union sockaddr_all *p;

	p = lua_newuserdata(L, sizeof *p);
	*p = xie->u.sa;
	luaL_getmetatable(L, SOCK_MTADDR);
	lua_setmetatable(L, -2);

	return 1;
}

static int __xie_free(lua_State *L)
{
	struct xu_io_event *xie = lua_touserdata(L, 1);

	xu_free(xie);
	return 0;
}

static int __xie_tostring(lua_State *L)
{
	struct xu_io_event *xie = lua_touserdata(L, 1);

	lua_pushlstring(L, xie->data, xie->size);
	return 1;
}

static int __xie_get_data(lua_State *L)
{
	struct xu_io_event *xie = lua_touserdata(L, 1);

	lua_pushlightuserdata(L, xie->data);
	return 1;
}

static void __xio_event(lua_State *L)
{
	luaL_Reg xe[] = {
		{"event", __xie_get_event},
		{"fd", __xie_get_fd},
		{"len", __xie_get_len},
		{"errno", __xie_get_errcode},
		{"tostring", __xie_tostring},
		{"data", __xie_get_data},
		{"address", __xie_get_address},
		{"free", __xie_free},
		{NULL, NULL}
	};
//	luaL_newlib(L, xe);
	luaL_openlib(L, "ioevent", xe, 0);
	lua_pop(L, 1);
}

static int __buf_read_u8(lua_State *L)
{
	unsigned char *buf = lua_touserdata(L, 1);
	int off = luaL_checkinteger(L, 2);

	lua_pushinteger(L, buf[off]);

	return 1;
}

static int __buf_read8(lua_State *L)
{
	signed char *buf = lua_touserdata(L, 1);
	int off = luaL_checkinteger(L, 2);

	lua_pushinteger(L, buf[off]);

	return 1;
}

static int __buf_read_u16(lua_State *L)
{
	unsigned short *buf = lua_touserdata(L, 1);
	int off = luaL_checkinteger(L, 2);

	lua_pushinteger(L, buf[off/2]);

	return 1;
}

static int __buf_read16(lua_State *L)
{
	signed short *buf = lua_touserdata(L, 1);
	int off = luaL_checkinteger(L, 2);

	lua_pushinteger(L, buf[off/2]);

	return 1;
}

static int __buf_read_u32(lua_State *L)
{
	unsigned int *buf = lua_touserdata(L, 1);
	int off = luaL_checkinteger(L, 2);

	lua_pushinteger(L, buf[off/4]);

	return 1;
}

static int __buf_read32(lua_State *L)
{
	signed int *buf = lua_touserdata(L, 1);
	int off = luaL_checkinteger(L, 2);

	lua_pushinteger(L, buf[off/4]);

	return 1;
}

static void __xio_buffer(lua_State *L)
{
	luaL_Reg xb[] = {
		{"readu8", __buf_read_u8},
		{"read8", __buf_read8},
		{"readu16", __buf_read_u16},
		{"read16", __buf_read16},
		{"reau32", __buf_read_u32},
		{"read32", __buf_read32},
		{NULL, NULL}
	};
//	luaL_newlib(L, xb);
	luaL_openlib(L, "rdbuf", xb, 0);
	lua_pop(L, 1);
}

static void * __alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
	if (nsize == 0) {
		xu_free(ptr);
		return NULL;
	}
	return xu_realloc(ptr, nsize);
}

struct xulua *xulua_new(void)
{
	struct xulua *xl = xu_calloc(1, sizeof *xl);
	xl->L = lua_newstate(__alloc, NULL);
	luaL_openlibs(xl->L);

	return xl;
}

static int traceback(lua_State *L)
{
	const char *msg = lua_tostring(L, 1);

	if (msg) {
		luaL_traceback(L, L, msg, 1);
	} else {
		lua_pushliteral(L, "(no error message)");
	}
	return 1;
}

static int __cb(struct xu_actor *ctx, void *ud, int mtype, uint32_t src, void *msg, size_t sz)
{
	lua_State *L = ud;
	int top = lua_gettop(L);

/*	xu_error(ctx, "callback running: %d, top = %d", mtype, top); */
	if (top == 0) {
		lua_pushcfunction(L, traceback);
		lua_rawgetp(L, LUA_REGISTRYINDEX, __cb);
	} else {
		assert(top == 2);
	}

	lua_pushvalue(L, 2);
	lua_pushinteger(L, mtype);
	lua_pushinteger(L, src);
	lua_pushlightuserdata(L, (void *)msg);
	lua_pushinteger(L, sz);

	int r = lua_pcall(L, 4, 0, 1);

	if (r == 0) {
		return 0;
	} else {
		xu_error(ctx, "lua error: %s", lua_tostring(L, -1));
	}
	lua_pop(L, 1);
	return 0;
}

static int lcallback(lua_State *L)
{
	struct xu_actor *ctx = lua_touserdata(L, lua_upvalueindex(1));

	luaL_checktype(L, 1, LUA_TFUNCTION);
	lua_settop(L, 1);
	lua_rawsetp(L, LUA_REGISTRYINDEX, __cb);

	lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
	lua_State *_L = lua_tothread(L, -1);

	xu_actor_callback(ctx, _L, __cb);

	return 0;
}

static int lsetname(lua_State *L)
{
	struct xu_actor *ctx = lua_touserdata(L, lua_upvalueindex(1));
	int top = lua_gettop(L);
	uint32_t h = xu_actor_handle(ctx);
	const char *n;

	if (top == 2) {
		h = luaL_checkinteger(L, 1);
		n = luaL_checkstring(L, 2);
	} else if (top == 1) {
		n = luaL_checkstring(L, 1);
	} else {
		return 0;
	}
	xu_actor_namehandle(h, n);
	return 0;
}

static int lquery(lua_State *L)
{
	struct xu_actor *ctx = lua_touserdata(L, lua_upvalueindex(1));
	int top = lua_gettop(L);
	uint32_t h;
	char name[128] = {0};

	if (top >= 1) {
		h = luaL_checkinteger(L, 1);
		ctx = xu_handle_ref(h);
	} 

	if (ctx) {
		xu_actor_name(ctx, name, sizeof name);
		lua_pushstring(L, name);
		return 1;
	}
	return 0;
}

static int ltimeout(lua_State *L)
{
	struct xu_actor *ctx = lua_touserdata(L, lua_upvalueindex(1));
	int tmo;
	int session;

	tmo = luaL_checkinteger(L, 1);
	session = luaL_checkinteger(L, 2);
	xu_timeout(xu_actor_handle(ctx), tmo, session);
	return 0;
}

/* 
 * xu_send wrapper.
 *
 * <1> : dest address `int' or `string.
 * <2> : source address `int'.
 * <3> : type `int'.
 * <4> : msg (lightuserdata or string).
 * <5> : size or nil.
 */
static int lsend(lua_State *L)
{
	struct xu_actor *ctx = lua_touserdata(L, lua_upvalueindex(1));
	uint32_t dst, source;
	const char *dstring = NULL;
	int type;
	void *msg;
	size_t len = 0;

	if ((dst = luaL_checkinteger(L, 1)) == 0) {
		if (lua_type(L, 1) == LUA_TNUMBER) {
			return luaL_error(L, "Invalid dest address 0");
		}
		dstring = lua_tostring(L, 1);
		if (dstring == NULL) {
			return luaL_error(L, "dest address type (%s) must be string or number", lua_typename(L, lua_type(L, 1)));
		}
	}
	source = luaL_checkinteger(L, 2);
	type = luaL_checkinteger(L, 3);
	int mtype = lua_type(L, 4);
	switch (mtype) {
		case LUA_TSTRING:
			msg = (void *)lua_tolstring(L, 4, &len);
			if (len == 0) {
				msg = "";
			}
			break;
		case LUA_TLIGHTUSERDATA:
			msg = lua_touserdata(L, 4);
			len = luaL_checkinteger(L, 5);
			break;
		default:
			luaL_error(L, "invalid param %s", lua_typename(L, lua_type(L, 4)));
			break;
	}
	if (dstring) {
		xu_sendname(ctx, source, dstring, type, msg, len);
	} else {
		xu_send(ctx, source, dst, type, msg, len);
	}
	return 0;
}

static int lnow(lua_State *L)
{
	uint64_t ti = xu_now();
	lua_pushinteger(L, ti);
	return 1;
}

#define STYPE_TCP 1
#define STYPE_UDP 2
#define STYPE_CON 3
static int __lio(lua_State *L, int type)
{
	struct xu_actor *ctx = lua_touserdata(L, lua_upvalueindex(1));
	const char *host;
	int port;
	uint32_t h, owner;

	host = luaL_checkstring(L, 1);
	port = luaL_checkinteger(L, 2);
	owner = xu_actor_handle(ctx);
	switch (type) {
		case STYPE_TCP:
			h = xu_io_tcp_server(owner, host, port);
			break;
		case STYPE_UDP:
			h = xu_io_udp_server(owner, host, port);
			break;
		case STYPE_CON:
			h = xu_io_tcp_connect(owner, host, port);
			break;
		default:
			return 0;
	}
	lua_pushinteger(L, h);
	return 1;
}

static int ltcpserver(lua_State *L)
{
	return __lio(L, STYPE_TCP);
}

static int ludpserver(lua_State *L)
{
	return __lio(L, STYPE_UDP);
}

static int lconnect(lua_State *L)
{
	return __lio(L, STYPE_CON);
}

static int lclose(lua_State *L)
{
	uint32_t fd;
	struct xu_actor *ctx = lua_touserdata(L, lua_upvalueindex(1));

	fd = luaL_checkinteger(L, 1);
	xu_io_close(xu_actor_handle(ctx), fd);

	return 0;
}

static int lwrite(lua_State *L)
{
	struct xu_actor *ctx = lua_touserdata(L, lua_upvalueindex(1));
	uint32_t owner = xu_actor_handle(ctx);
	uint32_t fd = luaL_checkinteger(L, 1);
	void *msg = NULL;
	size_t len = 0;

	int mtype = lua_type(L, 2);
	switch (mtype) {
		case LUA_TSTRING:
			msg = (void *)lua_tolstring(L, 2, &len);
			break;
		case LUA_TLIGHTUSERDATA:
			msg = lua_touserdata(L, 2);
			len = luaL_checkinteger(L, 3);
			break;
		default:
			return luaL_error(L, "write invalid param %s", lua_typename(L, lua_type(L, 2)));
	}

	if (msg)  {
		xu_io_write(owner, fd, msg, len);
	}
	return 0;
}

static int ludpsend(lua_State *L)
{
	struct xu_actor *ctx = lua_touserdata(L, lua_upvalueindex(1));
	uint32_t owner = xu_actor_handle(ctx);
	uint32_t fd = luaL_checkinteger(L, 1);
	union sockaddr_all *sa = SOCKADDR(2);
	void *msg;
	size_t len = 0;

	int mtype = lua_type(L, 3);
	switch (mtype) {
		case LUA_TSTRING:
			msg = (void *)lua_tolstring(L, 2, &len);
			break;
		case LUA_TLIGHTUSERDATA:
			msg = lua_touserdata(L, 2);
			len = luaL_checkinteger(L, 3);
			break;
		default:
			return luaL_error(L, "write invalid param %s", lua_typename(L, lua_type(L, 2)));
	}

	xu_io_udp_send(owner, fd, sa, msg, len);

	return 0;
}

static int ludppeer(lua_State *L)
{
	union sockaddr_all *sa, *p;

	sa = lua_touserdata(L, 1);
	if (sa) {
		p = lua_newuserdata(L, sizeof *p);
		*p = *sa;
		luaL_getmetatable(L, SOCK_MTADDR);
		lua_setmetatable(L, -2);
		return 1;
	}
	return 0;
}

static int ludpaddress(lua_State *L)
{
	union sockaddr_all *sa, adr;
	const char *ip;
	size_t sz = 0;
	int port;
	struct xu_actor *ctx = lua_touserdata(L, lua_upvalueindex(1));

	ip = luaL_checklstring(L, 1, &sz);
	xu_error(ctx, "string size = %d", sz);
	if (ip) {
		port = luaL_checkinteger(L, 2);
		if (ip[0] == '[' && ip[sz-1] == ']') {
			char ip6[sz];
			memcpy(ip6, ip + 1, sz - 2);
			ip6[sz-2] = '\0';
			if (uv_ip6_addr(ip6, port, &adr.in6)) {
				xu_error(ctx, "ip6 address invalid: %s:%d", ip6, port);
				return 0;
			}
		} else {
			if (uv_ip4_addr(ip, port, &adr.in4)) {
				xu_error(ctx, "ip address invalid: %s:%d", ip, port);
				return 0;
			}
		}
		sa = lua_newuserdata(L, sizeof *sa);
		*sa = adr;
		luaL_getmetatable(L, SOCK_MTADDR);
		lua_setmetatable(L, -2);
		return 1;
	}

	return 0;
}

static int ludpopen(lua_State *L)
{
	struct xu_actor *ctx = lua_touserdata(L, lua_upvalueindex(1));
	uint32_t fd;
	int udp6 = 0;

	if (lua_gettop(L) >= 1) {
		const char *sd = luaL_checkstring(L, 1);
		if (strcmp(sd, "udp6") == 0) {
			udp6 = 1;
		}
	}

	fd = xu_io_udp_open(xu_actor_handle(ctx), udp6);
	lua_pushinteger(L, fd);
	return 1;
}

static int llaunch(lua_State *L)
{
	const char *name, *p;
	uint32_t h = 0;
	struct xu_actor *xa;

	name = luaL_checkstring(L, 1);
	p = lua_tostring(L, 2);
	if (p == NULL)
		p = "";

	xa = xu_actor_new(name, p);
	if (xa) {
		h = xu_actor_handle(xa);
	}
	lua_pushinteger(L, h);
	return 1;
}

static int lgetenv(lua_State *L)
{
	char buf[128] = {0};
	const char *env;

	env = luaL_checkstring(L, 1);
	if ((xu_getenv(env, buf, sizeof buf)) != NULL) {
		lua_pushstring(L, buf);
		return 1;
	}
	return 0;
}

static int lsetenv(lua_State *L)
{
	const char *env, *var;

	env = luaL_checkstring(L, 1);
	var = luaL_checkstring(L, 2);

	xu_setenv(env, var);
	return 0;
}

static int lerror(lua_State *L)
{
	struct xu_actor *ctx = lua_touserdata(L, lua_upvalueindex(1));
	const char *s;

	s = luaL_checkstring(L, 1);
	xu_error(ctx, s);
	return 0;
}

static uint32_t tohandle(struct xu_actor * context, const char * param) {
	uint32_t handle = 0;
	if (param[0] == ':') {
		handle = strtoul(param+1, NULL, 16);
	} else if (param[0] == '.') {
		handle = xu_actor_findname(param+1);
	} else {
		xu_error(context, "Can't convert %s to handle",param);
	}

	return handle;
}

static void do_exit(struct xu_actor *ctx, uint32_t handle)
{
	if (handle == 0) {
		handle = xu_actor_handle(ctx);
		xu_error(ctx, "kill self");
	} else {
		xu_error(ctx, "kill :08x", handle);
	}
	xu_handle_retire(handle);
}

static int lexit(lua_State *L)
{
	struct xu_actor *ctx = lua_touserdata(L, lua_upvalueindex(1));
	do_exit(ctx, 0);
	return 0;
}

static int lkill(lua_State *L)
{
	struct xu_actor *ctx = lua_touserdata(L, lua_upvalueindex(1));
	const char *p;
	p = luaL_checkstring(L, 1);

	uint32_t handle = tohandle(ctx, p);
	if (handle) {
		do_exit(ctx, handle);
	}
	return 0;
}

static int __membership(lua_State *L, int join)
{
	struct xu_actor *ctx = lua_touserdata(L, lua_upvalueindex(1));
	uint32_t fdesc;
	const char *maddr, *iaddr;

	fdesc = luaL_checkinteger(L, 1);
	maddr = luaL_checkstring(L, 2);
	iaddr = luaL_checkstring(L, 3);
	xu_io_udp_membership(xu_actor_handle(ctx), fdesc, maddr, iaddr, join);
	return 0;
}

static int laddmembership(lua_State *L)
{
	return __membership(L, 1);
}

static int ldropmembership(lua_State *L)
{
	return __membership(L, 0);
}

static int lmulticastloop(lua_State *L)
{
	struct xu_actor *ctx = lua_touserdata(L, lua_upvalueindex(1));
	uint32_t fdesc;
	int on;

	fdesc = luaL_checkinteger(L, 1);
	on = lua_toboolean(L, 2);
	xu_io_udp_set_multicast_loop(xu_actor_handle(ctx), fdesc, on);
	return 0;
}

static int lbroadcast(lua_State *L)
{
	struct xu_actor *ctx = lua_touserdata(L, lua_upvalueindex(1));
	uint32_t fdesc;
	int on;

	fdesc = luaL_checkinteger(L, 1);
	on = lua_toboolean(L, 2);
	xu_io_udp_set_broadcast(xu_actor_handle(ctx), fdesc, on);
	return 0;
}

static int lkeepalive(lua_State *L)
{
	struct xu_actor *ctx = lua_touserdata(L, lua_upvalueindex(1));
	uint32_t fdesc;
	int on;
	int delay;

	fdesc = luaL_checkinteger(L, 1);
	on = lua_toboolean(L, 2);
	delay = luaL_checkinteger(L, 3);
	xu_io_tcp_keepalive(xu_actor_handle(ctx), fdesc, on, delay);
	return 0;
}

static int __init_cb(struct xu_actor *ctx, void *ud, int mtype, uint32_t src, void *msg, size_t sz)
{
	struct xulua *l = ud;
	lua_State *L = l->L;
	const char *loader;
	int r;
	luaL_Reg funcs[] = {
		{"callback", lcallback},
		{"name",     lsetname},
		{"query",    lquery},
		{"timeout",  ltimeout},
		{"dispatch",     lsend},
		{"launch",   llaunch},
		{"exit",     lexit},
		{"kill",     lkill},
		{"getenv",   lgetenv},
		{"setenv",   lsetenv},
		{"now",      lnow},
		{"error",    lerror},
		{"createTcpServer", ltcpserver},
		{"createUdpServer", ludpserver},
		{"close", lclose},
		{"connect", lconnect},
		{"write", lwrite},
		{"udpOpen", ludpopen},
		{"udpSend", ludpsend},
		{"addMembership", laddmembership},
		{"dropMembership", ldropmembership},
		{"setMulticastLoopback", lmulticastloop},
		{"setBroadcast", lbroadcast},
		{"setKeepalive", lkeepalive},
		{"udpPeer", ludppeer},
		{"address", ludpaddress},
		{NULL, NULL}
	};

	lua_gc(L, LUA_GCSTOP, 0);
/*	xu_error(ctx, "__init_cb: %s", (char *)msg); */
	xu_actor_callback(ctx, NULL, NULL);

	lua_pushlightuserdata(L, ctx);
	lua_setfield(L, LUA_REGISTRYINDEX, "xu_actor");

	lua_pushlightuserdata(L, ctx);
	luaL_openlib(L, "actor", funcs, 1);

	lua_pop(L, 1);

	__sock_addr_mt(L);
	__xio_event(L);
	__xio_buffer(L);

	if ((loader = xu_getenv("lua_loader", NULL, 0)) == NULL)
		loader = "./scripts/loader.lua";

	lua_pushcfunction(L, traceback);
	r = luaL_loadfile(L, loader);
	if (r != 0) {
		xu_error(ctx, "Can't load %s : %s", (char *)msg, lua_tostring(L, -1));
		lua_pop(L, 1);
		return 0;
	}
	lua_pushlstring(L, msg, sz);
	r = lua_pcall(L, 1, 0, 1);
	if (r != 0) {
		xu_error(ctx, "lua load error : %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
//	xu_error(ctx, "__init_done: %d, top = %d", r, lua_gettop(L));
	lua_gc(L, LUA_GCRESTART, 0);
	return 0;
}

int xulua_init(struct xu_actor *ctx, struct xulua *xa, const char *p)
{
	xa->handle = xu_actor_handle(ctx);

	xu_actor_namehandle(xa->handle, "lua");

	xu_actor_callback(ctx, xa, __init_cb);

	xu_send(ctx, xa->handle, xa->handle, 0 , (char *)p, strlen(p));

	xu_error(ctx, "xulua init %d", xa->handle);

	return 0;
}

void xulua_free(struct xulua *ud)
{
	xu_error(NULL, "xulua free %d", ud->handle);
	lua_close(ud->L);
	xu_free(ud);
}

