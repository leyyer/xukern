#include <unistd.h>
#include <sys/timerfd.h>
#include "xu_impl.h"
#include "uv.h"

struct timerfd {
	uv_poll_t handle;
	lua_State *L;
	int closed;
	int tm;
	int fd;
	int cb;
};

struct timer_wrap {
	uv_timer_t handle;
	lua_State *L;
	int cb;
};

#define TM_CLASS "Timer"
#define TM_MTNAME "mt.Timer"
#define TIMER() (luaL_checkudata(L, (1), TM_MTNAME))

#define TMFD_CLASS "TimerFd"
#define TMFD_MTNAME "mt.TimerFd"
#define TMFD() (luaL_checkudata(L, 1, TMFD_MTNAME))

static int __tmfd_new(lua_State *L)
{
	struct timerfd *tfd;
	int fd;
	xuctx_t ctx = lua_touserdata(L, lua_upvalueindex(1));

	if ((fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)) < 0) {
		return 0;
	}
	tfd = lua_newuserdata(L, sizeof *tfd);
	tfd->fd = fd;
	tfd->cb = LUA_REFNIL;
	tfd->L = L;
	tfd->closed = 0;
	uv_poll_init(xu_ctx_loop(ctx), &tfd->handle, fd);

	luaL_getmetatable(L, TMFD_MTNAME);
	lua_setmetatable(L, -2);

	return 1;
}

static void __set_time(struct timerfd *tfd, int ms)
{
	struct timespec ts;
	struct itimerspec new_value;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	new_value.it_value.tv_sec = ts.tv_sec + (ms / 1000);
	new_value.it_value.tv_nsec = ts.tv_nsec + (ms % 1000) * 1000000;
	if (new_value.it_value.tv_nsec >= 1000000000) {
		new_value.it_value.tv_nsec -= 1000000000;
		new_value.it_value.tv_sec += 1;
	}

	new_value.it_interval.tv_sec = ms / 1000;
	new_value.it_interval.tv_nsec = (ms % 1000) * 1000000;
	if (new_value.it_interval.tv_nsec >= 1000000000) {
		new_value.it_interval.tv_nsec -= 1000000000;
		new_value.it_interval.tv_sec += 1;
	}

	int len;
	do {
		uint64_t v;
		len = read(tfd->fd, &v, sizeof v);
	} while (len > 0);

	int r = timerfd_settime(tfd->fd, TFD_TIMER_ABSTIME, &new_value, NULL);
	if (r < 0) {
		perror("error: ");
	}
}

static void __on_read(uv_poll_t *p, int status, int events)
{
	struct timerfd *tfd = (struct timerfd *)p;
	uint64_t v;
	int len, top, r;

	if (!(events & UV_READABLE))
		return;

	do {
		len = read(tfd->fd, &v, sizeof v);
	} while (len > 0);
	if (tfd->cb == LUA_REFNIL) {
		return;
	}
	lua_State *L = tfd->L;
	top = lua_gettop(L);
	if (top != 1) {
		lua_pushcfunction(L, xu_luatraceback);
	} else {
		assert(top == 1);
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, tfd->cb);
	r = lua_pcall(L, 0, 0, 1);
	if (r != 0) {
		xu_println("%s: %s", __func__, lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

static int __tmfd_start(lua_State *L)
{
	struct timerfd *tfd = TMFD();
	int err = 0;
	int tmo;

	if (lua_type(L, 2) != LUA_TFUNCTION) {
		err = -1;
		goto skip;
	}

	tmo = luaL_checkinteger(L, 3);

	lua_settop(L, 2);

	if (tfd->cb != LUA_REFNIL)
		luaL_unref(L, LUA_REGISTRYINDEX, tfd->cb);

	tfd->cb = luaL_ref(L, LUA_REGISTRYINDEX);
	__set_time(tfd, tmo);
	tfd->tm = tmo;

	err = uv_poll_start(&tfd->handle, UV_READABLE, __on_read);
skip:
	lua_pushboolean(L, err == 0);
	return 1;
}

static int __tmfd_restart(lua_State *L)
{
	struct timerfd *tfd = TMFD();
	int err = 0;

	__set_time(tfd, tfd->tm);

	err = uv_poll_start(&tfd->handle, UV_READABLE, __on_read);
	lua_pushboolean(L, err == 0);
	return 1;
}

static int __tmfd_stop(lua_State *L)
{
	struct timerfd *tfd = TMFD();
	struct itimerspec new_value;
	memset(&new_value, 0, sizeof new_value);
	timerfd_settime(tfd->fd, TFD_TIMER_ABSTIME, &new_value, NULL);
	uv_poll_stop(&tfd->handle);
	return 0;
}

static int __tmfd_close(lua_State *L)
{
	struct timerfd *tfd = TMFD();

	if (tfd->cb != LUA_REFNIL) {
		luaL_unref(L, LUA_REGISTRYINDEX, tfd->cb);
		tfd->cb = LUA_REFNIL;
	}

	if (!tfd->closed) {
		close(tfd->fd);
		tfd->closed = 1;
	}

	return 0;
}

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

	repeat = luaL_checkinteger(L, 4);
	timeout = luaL_checkinteger(L, 3);

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

	r = luaL_checkinteger(L, 2);
	uv_timer_set_repeat(&twr->handle, r);
	return 0;
}

static int __tm_get_repeat(lua_State *L)
{
	struct timer_wrap *twr = TIMER();
	uint64_t r;

	r = uv_timer_get_repeat(&twr->handle);
	lua_pushinteger(L, r);
	return 1;
}

void init_lua_timer(lua_State *L, xuctx_t ctx)
{
	static struct luaL_Reg tm [] = {
		{"new", __tm_new},
		{"newTimerfd", __tmfd_new},
		{NULL, NULL}
	};

	static struct luaL_Reg mt_tm [] = {
		{"start",   __tm_start},
		{"restart", __tm_restart},
		{"setRepeat", __tm_set_repeat},
		{"getRepeat", __tm_get_repeat},
		{"stop",    __tm_stop},
		{"close",   __tm_close},
		{NULL, NULL}
	};

	static struct luaL_Reg mt_tmfd[] = {
		{"start", __tmfd_start},
		{"restart", __tmfd_restart},
		{"stop", __tmfd_stop},
		{"close", __tmfd_close},
		{"__gc", __tmfd_close},
		{NULL, NULL}
	};

	xu_create_metatable(L, TM_MTNAME, mt_tm);
	xu_create_metatable(L, TMFD_MTNAME, mt_tmfd);
	lua_pushlightuserdata(L, ctx);
	luaL_openlib(L, TM_CLASS, tm, 1);
	lua_pop(L, 1);
}

