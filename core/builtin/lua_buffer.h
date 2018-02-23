#ifndef __LUA_BUFFER_H__
#define __LUA_BUFFER_H__
struct buffer {
	int cap;
	char data[0];
};

#define BUF_CLS_NAME   "Buffer"
#define BUF_CLS_MTNAME "mt.Buffer"
#define BUFFER(idx) (luaL_checkudata(L, (idx), BUF_CLS_MTNAME))

struct lua_State;
struct buffer * buffer_new(struct lua_State *L, size_t len);
#endif


