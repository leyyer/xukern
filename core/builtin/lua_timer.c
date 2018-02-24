#include "xu_impl.h"
#include "uv.h"

struct timer_wrap {
	uv_timer_t handle;
	lua_State *L;
	int cb;
};

#define TM_CLASS "Timer"
#define TM_MTNAME "mt.Timer"
#define TIMER() (luaL_checkudata(L, (1), TM_MTNAME))

int __tm_new(lua_State *L)
{
	struct timer_wrap *twr;
	xuctx_t ctx = lua_touserdata(L, lua_upvalueindex(1));

	twr = lua_newuserdata(L, sizeof *twr);
	twr->cb = LUA_REFNIL;
	twr->L = L;

	uv_timer_init(xu_ctx_loop(ctx), &twr->handle);

	luaL_getmetatable(L, TM_MTNAME);
	lua_setmetatable(L, -2);

	return 1;
}

static void __on_timer(uv_timer_t *tm)
{
	struct timer_wrap *twr = (struct timer_wrap *)tm;

	if (twr->cb != LUA_REFNIL) {
		lua_State *L = twr->L;
		int top = lua_gettop(L);
		if (top != 1) {
			lua_pushcfunction(L, xu_luatraceback);
		} else {
			assert(top == 1);
		}
		lua_rawgeti(L, LUA_REGISTRYINDEX, twr->cb);
		int r = lua_pcall(L, 0, 0, 1);
		if (r != 0) {
			xu_println("%s: %s", __func__, lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
}

static int __tm_start(lua_State *L)
{
	struct timer_wrap *twr = TIMER();
	int err = 0;
	uint64_t repeat, timeout;

	if (lua_type(L, 2) != LUA_TFUNCTION) {
		err = -1;
		goto skip;
	}

	repeat = luaL_checknumber(L, 4);
	timeout = luaL_checknumber(L, 3);

	lua_settop(L, 2);
	twr->cb = luaL_ref(L, LUA_REGISTRYINDEX);

	err = uv_timer_start(&twr->handle, __on_timer, timeout, repeat);
skip:
	lua_pushboolean(L, err == 0);
	return 1;
}

static int __tm_close(lua_State *L)
{
	struct timer_wrap *twr = TIMER();

	if (twr->cb != LUA_REFNIL) {
		luaL_unref(L, LUA_REGISTRYINDEX, twr->cb);
		twr->cb = LUA_REFNIL;
	}

	return 0;
}

static int __tm_stop(lua_State *L)
{
	struct timer_wrap *twr = TIMER();

	int err = uv_timer_stop(&twr->handle);
	lua_pushboolean(L, err == 0);
	return 1;
}

static int __tm_restart(lua_State *L)
{
	struct timer_wrap *twr = TIMER();

	int err = uv_timer_again(&twr->handle);
	lua_pushboolean(L, err == 0);

	return 1;
}

static int __tm_set_repeat(lua_State *L)
{
	struct timer_wrap *twr = TIMER();
	uint64_t r;

	r = luaL_checknumber(L, 2);
	uv_timer_set_repeat(&twr->handle, r);
	return 0;
}

static int __tm_get_repeat(lua_State *L)
{
	struct timer_wrap *twr = TIMER();
	uint64_t r;

	r = uv_timer_get_repeat(&twr->handle);
	lua_pushnumber(L, r);
	return 1;
}

void init_lua_timer(lua_State *L, xuctx_t ctx)
{
	struct luaL_Reg tm [] = {
		{"new", __tm_new},
		{NULL, NULL}
	};

	struct luaL_Reg mt_tm [] = {
		{"start",   __tm_start},
		{"restart", __tm_restart},
		{"setRepeat", __tm_set_repeat},
		{"getRepeat", __tm_get_repeat},
		{"stop",    __tm_stop},
		{"close",   __tm_close},
		{NULL, NULL}
	};

	xu_create_metatable(L, TM_MTNAME, mt_tm);
	lua_pushlightuserdata(L, ctx);
	luaL_openlib(L, TM_CLASS, tm, 1);
	lua_pop(L, 1);
}

