#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "xu_kern.h"
#include "xu_malloc.h"
#include "xu_util.h"
#include "xu_io.h"
#include "btif.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lua.h"

#define BTIF_CLASS "Btif"
#define BTIF_MTCLASS "mt.Btif"
#define BTIF() (luaL_checkudata(L, 1, BTIF_MTCLASS))

struct btif {
	btif_t    btif;
	lua_State *L;
	uint32_t  handle;
	uint32_t  fd;
};

static int __btif_step(lua_State *L)
{
	struct btif *bi = BTIF();

	if (bi->btif)
		btif_step(bi->btif);
	return 0;
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

static void __on_cmd(void *arg, unsigned char cmd, unsigned char *cmdbuf, int cmdlen)
{
	struct btif *bi = arg;
	lua_State *L;

	L = bi->L;
	int top = lua_gettop(L);
	if (top < 1) {
		lua_pushcfunction(L, traceback);
	} else {
		assert(top == 1);
	}
	lua_rawgetp(L, LUA_REGISTRYINDEX, __on_cmd);
	lua_pushinteger(L, cmd); /* <1>: cmd */
	lua_pushlightuserdata(L, cmdbuf); /* <2>: data buffer */
	lua_pushinteger(L, cmdlen); /* <3> : cmdlen */
	int r = lua_pcall(L, 3, 0, 1);
	if (r != 0) {
		xu_error(0, "%s: %s", __func__, lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

static int __btif_open(lua_State *L)
{
	btif_t btif;
	const char *dev;
	struct btif *bi;
	struct xu_actor *ctx;

	dev = luaL_checkstring(L, 1);
	btif = btif_new(dev);

	lua_getfield(L, LUA_REGISTRYINDEX, "xu_actor");
	ctx = lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (btif == NULL || ctx == NULL) {
		if (ctx) {
			xu_error(ctx, "open %s failed.", dev);
		}
		return 0;
	}

	bi = lua_newuserdata(L, sizeof *bi);
	bi->btif = btif;
	bi->handle = xu_actor_handle(ctx);
	bi->fd = xu_io_fd_open(bi->handle, btif_get_fd(btif));
	luaL_getmetatable(L, BTIF_MTCLASS);
	lua_setmetatable(L, -2);
	return 1;
}

static int __btif_close(lua_State *L)
{
	struct btif *bi = BTIF();

	if (bi->btif) {
		xu_io_close(bi->handle, bi->fd);
		xu_error(0, "btif close %p => %p\n", bi, bi->btif);
		btif_free(bi->btif);
		bi->btif = NULL;
	}
	return 0;
}

static int __btif_set_callback(lua_State *L)
{
	struct btif *bi = BTIF();

	luaL_checktype(L, 2, LUA_TFUNCTION);
	lua_settop(L, 2);
	lua_rawsetp(L, LUA_REGISTRYINDEX, __on_cmd);

	lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
	lua_State *_L = lua_tothread(L, -1);
	bi->L = _L;
	btif_notify_handler(bi->btif, __on_cmd, bi);

	return 0;
}

static int __btif_cmd(lua_State *L)
{
	struct btif *bi = BTIF();
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

int luaopen_btif(lua_State *L)
{
	luaL_Reg r[] = {
		{"open", __btif_open},
		{NULL, NULL}
	};

	luaL_Reg mt_r[] = {
		{"setCallback", __btif_set_callback},
		{"command",     __btif_cmd},
		{"step",        __btif_step},
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

	luaL_openlib(L, "btif", r, 0);

	return 1;
}

