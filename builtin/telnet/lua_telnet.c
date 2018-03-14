#include "libtelnet.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lua.h"

#define __NAME    "telnet"
#define __VERSION "1.0"

#define TELNET_MTCLASS "mt.Telnet"
#define TELNET() (luaL_checkudata(L, 1, TELNET_MTCLASS))

struct mytelnet {
	int       nego;
	telnet_t *tel;
	lua_State *L;
};

static void __evh(telnet_t *tel, telnet_event_t *event, void *data)
{
	struct mytelnet *mytel = data;
	int t = lua_rawgetp(mytel->L, LUA_REGISTRYINDEX, &mytel->nego);

	if (t == LUA_TFUNCTION) {
		int p = 2;
		switch (event->type) {
			case TELNET_EV_DATA:
				lua_pushliteral(mytel->L, "data");
				lua_pushlstring(mytel->L, event->data.buffer, event->data.size);
				break;
			case TELNET_EV_SEND:
				lua_pushliteral(mytel->L, "send");
				lua_pushlstring(mytel->L, event->data.buffer, event->data.size);
				break;
			case TELNET_EV_ERROR:
				lua_pushliteral(mytel->L, "error");
				lua_pushstring(mytel->L, event->error.msg);
				break;
			default:
				/* printf("type = %d\n", event->type); */
				p = 0;
				break;
		}
		if (p > 0) {
			lua_pcall(mytel->L, p, 0, 0);
		} else {
			lua_pop(mytel->L, 1);
		}
	} else {
		/* printf("is not function: %d\n", lua_gettop(mytel->L)); */
		lua_pop(mytel->L, 1);
	}
}

static void __do_negotiate(struct mytelnet *_tel)
{
	telnet_t *tel = _tel->tel;

	telnet_negotiate(tel, TELNET_WILL, TELNET_TELOPT_ECHO);
	telnet_negotiate(tel, TELNET_DONT, TELNET_TELOPT_ECHO);
}

static int ltelnet_new(lua_State *L)
{
	static const telnet_telopt_t telopts[] = {
		{ TELNET_TELOPT_ECHO,      TELNET_WILL, TELNET_DONT },
		{ TELNET_TELOPT_TTYPE,     TELNET_WILL, TELNET_DONT },
		{ TELNET_TELOPT_COMPRESS2, TELNET_WONT, TELNET_DO   },
		{ TELNET_TELOPT_MSSP,      TELNET_WONT, TELNET_DO   },
		{ -1, 0, 0 }
	};
	struct mytelnet *tel = lua_newuserdata(L, sizeof *tel);
	tel->L = L;

	tel->tel = telnet_init(telopts, __evh, 0, tel);

	if (tel->tel == NULL) {
		return 0;
	}
	tel->nego = 0;

	luaL_getmetatable(L, TELNET_MTCLASS);
	lua_setmetatable(L, -2);

	return 1;
}

static int __telnet_recv(lua_State *L)
{
	struct mytelnet *tel = TELNET();
	int pt;
	size_t sz;
	const char *s;

	pt = lua_type(L, 2);
	switch (pt) {
		case LUA_TSTRING:
			s = luaL_checklstring(L, 2, &sz);
			telnet_recv(tel->tel, s, sz);
			break;
		case LUA_TLIGHTUSERDATA:
			s = lua_touserdata(L, 2);
			sz = luaL_checkinteger(L, 3);
			telnet_recv(tel->tel, s, sz);
			break;
	}
	return 0;
}

static int __telnet_send(lua_State *L)
{
	struct mytelnet *tel = TELNET();
	int pt;
	size_t sz;
	const char *s;

	pt = lua_type(L, 2);
	switch (pt) {
		case LUA_TSTRING:
			s = luaL_checklstring(L, 2, &sz);
			telnet_send_text(tel->tel, s, sz);
			break;
		case LUA_TLIGHTUSERDATA:
			s = lua_touserdata(L, 2);
			sz = luaL_checkinteger(L, 3);
			telnet_send(tel->tel, s, sz);
			break;
	}
	return 0;
}

static int __telnet_set_cb(lua_State *L)
{
	struct mytelnet *tel = TELNET();

	lua_settop(L, 2);
	lua_rawsetp(L, LUA_REGISTRYINDEX, &tel->nego);

	if (tel->nego == 0) {
		__do_negotiate(tel);
		tel->nego = 1;
	}

	return 0;
}

static int __telnet_gc(lua_State *L)
{
	struct mytelnet *tel = TELNET();

	fprintf(stderr, "gc: telnet free\n");
	if (tel->tel) {
		telnet_free(tel->tel);
		tel->tel = NULL;
	}
	return 0;
}

int luaopen_telnet(lua_State *L)
{
	luaL_Reg r[] = {
		{"new", ltelnet_new},
		{NULL, NULL}
	};

	luaL_Reg mt_r[] = {
		{"setCallback", __telnet_set_cb},
		{"send", __telnet_send},
		{"recv", __telnet_recv},
		{"close", __telnet_gc},
		{"__gc", __telnet_gc},
		{NULL, NULL}
	};

	/* create metatable */
	luaL_newmetatable(L, TELNET_MTCLASS);
	lua_pushstring(L, "__index");
	lua_pushvalue(L, -2);
	lua_settable(L, -3);
	luaL_setfuncs(L, mt_r, 0);
	lua_pop(L, 1);

	lua_newtable(L);
	luaL_setfuncs(L, r, 0);

	lua_pushliteral(L, __NAME);
	lua_setfield(L, -2, "_NAME");
	lua_pushliteral(L, __VERSION);
	lua_setfield(L, -2, "_VERSION");
	return 1;
}

