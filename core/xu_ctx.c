#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "cJSON.h"
#include "uv.h"
#include "xu_malloc.h"
#include "xu_util.h"
#include "xu_impl.h"
#include "xu_lua.h"

struct xu_ctx {
	uv_loop_t *loop;
	lua_State *L;
};

static int __setenv(lua_State *L)
{
	const char *e, *v;

	e = luaL_checkstring(L, 1);
	v = luaL_checkstring(L, 2);
	if (e && v) {
		xu_setenv(e, v);
	}

	return 0;
}

static int __getenv(lua_State *L)
{
	const char *e;
	char v[128];
	size_t len = 0;

	e = luaL_checkstring(L, 1);
	if (e) {
		xu_getenv(e, v, sizeof v);
		len = strlen(v);
	}

	if (len > 0) {
		lua_pushlstring(L, v, len);
		return 1;
	}

	return 0;
}

static int __println(lua_State *L)
{
	const char *msg = luaL_checkstring(L, 1);
	xu_println(msg);
	return 0;
}

static int __sleep(lua_State *L)
{
	int n = luaL_checkinteger(L, 1);
	sleep(n);

	return 0;
}

static int __usleep(lua_State *L)
{
	int n = luaL_checkinteger(L, 1);

	usleep(n);
	return 0;
}

static int __stop(lua_State *L)
{
	xuctx_t ctx = lua_touserdata(L, lua_upvalueindex(1));

	xu_ctx_stop(ctx);

	return 0;
}

static int __run(lua_State *L)
{
	xuctx_t ctx = lua_touserdata(L, lua_upvalueindex(1));

	xu_ctx_run(ctx);

	return 0;
}

static int __run_once(lua_State *L)
{
	xuctx_t ctx = lua_touserdata(L, lua_upvalueindex(1));

	xu_ctx_run_once(ctx);

	return 0;
}

static int __now(lua_State *L)
{
	xuctx_t ctx = lua_touserdata(L, lua_upvalueindex(1));
	uint64_t n;
#if 0
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	n = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
	xu_println("now: %llu", uv_now(ctx->loop));
#endif
	n = uv_now(ctx->loop);

	lua_pushinteger(L, n);

	return 1;
}

static int __load(lua_State *L)
{
	int err;
	const char *file;
	xuctx_t ctx = lua_touserdata(L, lua_upvalueindex(1));

	file = luaL_checkstring(L, 1);
	err = xu_ctx_load(ctx, file);
	lua_pushboolean(L, err == 0);

	return 1;
}

xuctx_t xu_ctx_new()
{
	lua_State *L;
	xuctx_t ctx;

	static luaL_reg xu[] = {
		{"setenv", __setenv},
		{"getenv", __getenv},
		{"println", __println},
		{"now", __now},
		{"sleep", __sleep},
		{"usleep", __usleep},
		{"load", __load},
		{"run",  __run},
		{"runOnce",  __run_once},
		{"stop", __stop},
		{NULL, NULL}
	};

	ctx = xu_calloc(1, sizeof *ctx);

	ctx->loop = xu_malloc(sizeof *ctx->loop);
	uv_loop_init(ctx->loop);

	L = ctx->L = xu_newstate();

	luaL_openlibs(L);

	lua_pushlightuserdata(L, ctx);
	luaL_openlib(L, "xucore", xu, 1);
	lua_pop(L, 1);

	lua_pushlightuserdata(L, ctx);
	lua_setfield(L, LUA_REGISTRYINDEX, "xuctx");
	init_lua_buffer(L);
	init_lua_net(L, ctx);
	init_lua_timer(L, ctx);

	//xu_println("top = %d", lua_gettop(L));
	return ctx;
}

int xu_ctx_load(xuctx_t ctx, const char *file)
{
	int r;
	lua_State *L = ctx->L;

	lua_settop(L, 0);
	lua_pushcfunction(L, xu_luatraceback);
	r = luaL_loadfile(L, file);
	if (r != 0) {
		xu_println("Can't load %s : %s", file, lua_tostring(L, -1));
		return r;
	}

	r = lua_pcall(L, 0, 0, 1);
	if (r != 0) {
		xu_println("lua load error : %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return r;
	}
	return 0;
}

int xu_ctx_run(xuctx_t ctx)
{
	return uv_run(ctx->loop, UV_RUN_DEFAULT);
}

int xu_ctx_run_once(xuctx_t ctx)
{
	return uv_run(ctx->loop, UV_RUN_ONCE);
}

void xu_ctx_stop(xuctx_t ctx)
{
	uv_stop(ctx->loop);
}

void *xu_ctx_loop(xuctx_t ctx)
{
	return ctx->loop;
}

