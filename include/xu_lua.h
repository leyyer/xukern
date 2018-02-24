#ifndef __XU_LUA_H__
#define __XU_LUA_H__
#ifdef __cplusplus
extern "C" {
#endif

#include "lauxlib.h"
#include "lualib.h"
#include "luajit.h"

/* new luastate, specify our allocator */
lua_State *xu_newstate(void);

/* register user data class */
int xu_luaclass(lua_State *L, const char *class_name, struct luaL_Reg *methods,
		const char *mt_name, struct luaL_Reg *mt_methods);

void xu_create_metatable(lua_State *L, const char *name, luaL_Reg reg[]);

/* traceback function */
int xu_luatraceback(lua_State *L);

#ifdef __cplusplus
}
#endif
#endif

