#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "xu_core.h"
#include "xu_lua.h"
#include "xu_malloc.h"
#include "xu_util.h"
#include "uv.h"
#include "btif.h"
#include "../core/builtin/lua_buffer.h"

#define BTIF_CLASS "Btif"
#define BTIF_MTCLASS "mt.Btif"
#define BTIF() (luaL_checkudata(L, 1, BTIF_MTCLASS))

void * xu_ctx_loop(xuctx_t ctx);

struct btif {
	uv_poll_t handle;
	btif_t    btif;
	lua_State *L;
	int       cmd;
};

static void __on_read(uv_poll_t *p, int status, int events)
{
	struct btif *bi = (struct btif *)p;

	if (events & UV_READABLE) {
		btif_step(bi->btif);
	}
}

static void __on_cmd(void *arg, unsigned char cmd, unsigned char *cmdbuf, int cmdlen)
{
	struct btif *bi = arg;
	lua_State *L;
	struct buffer *b;

	if (bi->cmd == LUA_REFNIL)
		return;

	L = bi->L;
	int top = lua_gettop(L);
	if (top < 1) {
		lua_pushcfunction(L, xu_luatraceback);
	} else {
		assert(top == 1);
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, bi->cmd);
	lua_pushinteger(L, cmd); /* <1>: cmd */
	b = buffer_new(L, cmdlen); /* buffer object */
	memcpy(b->data, cmdbuf, cmdlen);
	int r = lua_pcall(L, 2, 0, 1);
	if (r != 0) {
		xu_println("%s: %s", __func__, lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

static int __btif_open(lua_State *L)
{
	btif_t btif;
	const char *dev;
	struct btif *bi;
	xuctx_t ctx;

	dev = luaL_checkstring(L, 1);
	btif = btif_new(dev);

	lua_getfield(L, LUA_REGISTRYINDEX, "xuctx");
	ctx = lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (btif == NULL || ctx == NULL) {
		return 0;
	}

	bi = lua_newuserdata(L, sizeof *bi);
	bi->btif = btif;
	bi->L     = L;
	bi->cmd = LUA_REFNIL;
	btif_notify_handler(bi->btif, __on_cmd, bi);
	uv_poll_init(xu_ctx_loop(ctx), &bi->handle, btif_get_fd(btif));
	uv_poll_start(&bi->handle, UV_READABLE, __on_read);
	luaL_getmetatable(L, BTIF_MTCLASS);
	lua_setmetatable(L, -2);
	return 1;
}

static int __btif_close(lua_State *L)
{
	struct btif *bi = BTIF();

	uv_poll_stop(&bi->handle);
	if (bi->cmd != LUA_REFNIL) {
		luaL_unref(L, LUA_REGISTRYINDEX, bi->cmd);
		bi->cmd = LUA_REFNIL;
	}
	xu_println("btif close %p => %p\n", bi, bi->btif);
	btif_free(bi->btif);
	bi->btif = NULL;
	return 0;
}

static int __btif_gc(lua_State *L)
{
	struct btif *bi = BTIF();

	if (bi->btif) {
		uv_poll_stop(&bi->handle);
		btif_free(bi->btif);
		bi->btif = NULL;
	}

	if (bi->cmd != LUA_REFNIL) {
		luaL_unref(L, LUA_REGISTRYINDEX, bi->cmd);
		bi->cmd = LUA_REFNIL;
	}
	return 0;
}

static int __btif_set_callback(lua_State *L)
{
	struct btif *bi = BTIF();
	
	if (bi->cmd != LUA_REFNIL) {
		luaL_unref(L, LUA_REGISTRYINDEX, bi->cmd);
	}
	bi->cmd = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

static int __btif_cmd(lua_State *L)
{
	struct btif *bi = BTIF();
	struct buffer *b;
	int cmd;
	int err;

	cmd = luaL_checkinteger(L, 2);
	b = BUFFER(3);
	err = btif_cmd(bi->btif, cmd, (void *)b->data, b->cap);
	xu_println("%s %d\n", __func__, err);
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
		{"command", __btif_cmd},
		{"close", __btif_close},
		{"__gc", __btif_gc},
		{NULL, NULL}
	};

	xu_luaclass(L, BTIF_CLASS, r, BTIF_MTCLASS, mt_r);
	return 1;
}

