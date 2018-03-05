#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "xu_kern.h"
#include "xu_malloc.h"
#include "xu_util.h"
#include "xu_io.h"
#include "btif.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lua.h"

#define BTIF_CLASS   "Btif"
#define BTIF_MTCLASS "mt.Btif"
#define BTIF() (luaL_checkudata(L, 1, BTIF_MTCLASS))

struct lua_btif {
	btif_t    btif;
	int       tty;
	uint32_t  handle;
	uint32_t  fd;
};

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

static void __on_cmd(void *arg, unsigned char cmd, unsigned char *cmdbuf, int cmdlen)
{
	lua_State *L = arg;

	int top = lua_gettop(L);
	if (top < 1) {
		lua_pushcfunction(L, traceback);
	} else {
		assert(top == 1);
	}
	lua_rawgetp(L, LUA_REGISTRYINDEX, __on_cmd);
	lua_pushinteger(L, cmd);          /* <1>:  cmd */
	lua_pushlightuserdata(L, cmdbuf); /* <2>:  data buffer */
	lua_pushinteger(L, cmdlen);       /* <3> : cmdlen */
	int r = lua_pcall(L, 3, 0, 1);
	if (r != 0) {
		xu_error(0, "%s: %s", __func__, lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

static int __output(btif_t bi, void *ud, const unsigned char *buf, int len)
{
	int n;
	const unsigned char *sbuf = buf;
	struct lua_btif *bif = ud;
	
	while (len > 0) {
		n = write(bif->tty, sbuf, len);
		if (n == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			break;
		}
		if (n > 0) {
			sbuf += n;
			len -= n;
		}
	}
	if (n < 0) {
		perror("sendto: ");
	}
	return sbuf - buf;
}

static void __do_close(struct lua_btif *bi)
{
	close(bi->tty);
	xu_io_close(bi->handle, bi->fd);
	xu_error(0, "btif close %p => %p\n", bi, bi->btif);
	btif_free(bi->btif);
	bi->btif = NULL;
	bi->tty = -1;
}

static int __btif_new(lua_State *L, int fd, int tty)
{
	btif_t btif;
	struct lua_btif *bi;
	struct xu_actor *ctx = lua_touserdata(L, lua_upvalueindex(1));

	bi = lua_newuserdata(L, sizeof *bi);
	bi->tty = fd;
	if (tty) {
		btif = btif_new(__output, bi);
	} else {
		btif = btif_sock_new(__output, bi);
	}
	assert(btif != NULL);
	bi->btif = btif;
	bi->handle = xu_actor_handle(ctx);
	bi->fd = xu_io_fd_open(bi->handle, fd);
	luaL_getmetatable(L, BTIF_MTCLASS);
	lua_setmetatable(L, -2);
	lua_pushinteger(L, bi->fd);
	xu_error(ctx, "btif: handle %u, fd %u, tty, %d", bi->handle, bi->fd, bi->tty);
	return 2;
}

static int __btif_open(lua_State *L)
{
	const char *dev;
	int fd;

	dev = luaL_checkstring(L, 1);
	fd = btif_tty_open(dev);
	if (fd < 0) {
		return 0;
	}
	return __btif_new(L, fd, 1);
}

static int __btif_sockopen(lua_State *L)
{
	const char *dev;
	int fd;

	dev = luaL_checkstring(L, 1);
	fd = btif_sock_open(dev);
	if (fd < 0) {
		return 0;
	}
	return __btif_new(L, fd, 0);
}

static int __btif_close(lua_State *L)
{
	struct lua_btif *bi = BTIF();

	if (bi->btif) {
		__do_close(bi);
	}
	return 0;
}

static int __btif_set_callback(lua_State *L)
{
	struct lua_btif *bi = BTIF();

	luaL_checktype(L, 2, LUA_TFUNCTION);
	lua_settop(L, 2);
	lua_rawsetp(L, LUA_REGISTRYINDEX, __on_cmd);

	lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
	lua_State *xL = lua_tothread(L, -1);

	btif_notify_handler(bi->btif, __on_cmd, xL);

	return 0;
}

static int __btif_data(lua_State *L)
{
	struct lua_btif *bi = BTIF();
	unsigned char *data = lua_touserdata(L, 2);
	size_t sz = luaL_checkinteger(L, 3);

	btif_recv(bi->btif, data, sz);

	return 0;
}

static int __btif_cmd(lua_State *L)
{
	struct lua_btif *bi = BTIF();
	const char *str;
	size_t len = 0;
	int cmd, err;

	cmd = luaL_checkinteger(L, 2);
	str = luaL_checklstring(L, 3, &len);
	err = btif_cmd(bi->btif, cmd, (void *)str, len);
	xu_error(0, "%s %d\n", __func__, err);
	lua_pushboolean(L, err == 0);
	return 1;
}

static int __btif_reboot(lua_State *L)
{
	struct lua_btif *bi = BTIF();

	btif_reboot(bi->btif);

	return 0;
}

static int __btif_power(lua_State *L)
{
	struct lua_btif *bi = BTIF();
	int type = luaL_checkinteger(L, 2);

	btif_set_power(bi->btif, type);
	return 0;
}

int luaopen_btif(lua_State *L)
{
	luaL_Reg r[] = {
		{"open", __btif_open},
		{"sockOpen", __btif_sockopen},
		{NULL, NULL}
	};

	luaL_Reg mt_r[] = {
		{"setCallback", __btif_set_callback},
		{"command",     __btif_cmd},
		{"put",         __btif_data},
		{"reboot",      __btif_reboot},
		{"power",       __btif_power},
		{"close",       __btif_close},
		{"__gc",        __btif_close},
		{NULL, NULL}
	};

	luaL_newmetatable(L, BTIF_MTCLASS);
	lua_pushstring(L, "__index");
	lua_pushvalue(L, -2);
	lua_settable(L, -3);
	luaL_setfuncs(L, mt_r, 0);
	lua_pop(L, 1);

	lua_getfield(L, LUA_REGISTRYINDEX, "xu_actor");
	luaL_openlib(L, "btif", r, 1);

	return 1;
}

