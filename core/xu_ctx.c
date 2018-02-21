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

/*
 * buffer object.
 */
#define BUF_CLS_NAME "Buffer"
#define BUF_CLS_MTNAME "mt.Buffer"

struct buffer {
	int len;
	int cap;
	char data[0];
};

static int __buf_alloc(lua_State *L)
{
	struct buffer *b;
	size_t len;

	len = luaL_checkinteger(L, 1);
	b = lua_newuserdata(L, len + sizeof *b);
	b->len = 0;
	b->cap = len;
	luaL_getmetatable(L, BUF_CLS_MTNAME);
	lua_setmetatable(L, -2);

	return 1;
}

static int __buf_cap(lua_State *L)
{
	struct buffer *b = luaL_checkudata(L, 1, BUF_CLS_MTNAME);

	lua_pushinteger(L, b->cap);
	return 1;
}

static int __buf_len(lua_State *L)
{
	struct buffer *b = luaL_checkudata(L, 1, BUF_CLS_MTNAME);

	lua_pushinteger(L, b->len);
	return 1;
}

static int __buf_toString(lua_State *L)
{
	struct buffer *b = luaL_checkudata(L, 1, BUF_CLS_MTNAME);

	if (b->len > 0) {
		lua_pushlstring(L, b->data, b->len);
		return 1;
	}
	return 0;
}

static int __buf_clear(lua_State *L)
{
	struct buffer *b = luaL_checkudata(L, 1, BUF_CLS_MTNAME);
	b->len = 0;
	return 0;
}

static int __buf_fill(lua_State *L)
{
	struct buffer *b = luaL_checkudata(L, 1, BUF_CLS_MTNAME);
	int8_t var = luaL_checkinteger(L, 2);

	memset(b->data, var, b->cap);
	return 0;
}

static int __buf_writeInt8(lua_State *L)
{
	struct buffer *b = luaL_checkudata(L, 1, BUF_CLS_MTNAME);
	int8_t value = luaL_checkinteger(L, 2);
	int off = luaL_checkinteger(L, 3);

	if (off >= 0 && off < b->cap) {
		b->data[off] = value;
		if (b->len <= off)
			b->len = off + 1;
	}
	return 0;
}

static int  __buf_readInt8(lua_State *L)
{
	struct buffer *b = luaL_checkudata(L, 1, BUF_CLS_MTNAME);
	int off = luaL_checkinteger(L, 2);

	if (off >= 0 && off < b->len) {
		lua_pushinteger(L, b->data[off]);
		return 1;
	}

	return 0;
}

static void __init_buffer(lua_State *L)
{
	static luaL_reg buf[] = {
		{"alloc", __buf_alloc},
		{NULL, NULL}
	};

	static luaL_reg mt_buf[] = {
		{"cap", __buf_cap},
		{"len", __buf_len},
		{"clear", __buf_clear},
		{"fill", __buf_fill},
		{"writeInt8", __buf_writeInt8},
		{"readInt8",  __buf_readInt8},
		{"toString", __buf_toString},
		{NULL, NULL}
	};

	xu_luaclass(L, BUF_CLS_NAME, buf, BUF_CLS_MTNAME, mt_buf);
}

xuctx_t xu_ctx_new()
{
	lua_State *L;
	xuctx_t ctx;

	static luaL_reg xu[] = {
		{"setenv", __setenv},
		{"getenv", __getenv},
		{"println", __println},
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

	__init_buffer(L);

	printf("top = %d\n", lua_gettop(L));
	return ctx;
}

int xu_ctx_load(xuctx_t ctx, const char *file)
{
	int r;
	lua_State *L = ctx->L;

	lua_pushcfunction(L, xu_luatraceback);
	r = luaL_loadfile(L, file);
	if (r != 0) {
		xu_println("Can't load %s : %s\n", file, lua_tostring(L, -1));
		return r;
	}

	r = lua_pcall(L, 0, 0, 1);
	if (r != 0) {
		xu_println("lua load error : %s", lua_tostring(L, -1));
		return r;
	}
	return 0;
}

int xu_ctx_run(xuctx_t ctx)
{
	return uv_run(ctx->loop, UV_RUN_DEFAULT);
}

