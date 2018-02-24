#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "xu_malloc.h"
#include "xu_util.h"
#include "xu_lua.h"

/*
 *  Copy string src to buffer dst of size dsize.  At most dsize-1
 *  chars will be copied.  Always NUL terminates (unless dsize == 0).
 *  Returns strlen(src); if retval >= dsize, truncation occurred.
 */
size_t xu_strlcpy(char *dst, const char *src, size_t dsize)
{
	const char *osrc = src;
	size_t nleft = dsize;

	/* Copy as many bytes as will fit. */
	if (nleft != 0) {
		while (--nleft != 0) {
			if ((*dst++ = *src++) == '\0')
				break;
		}
	}

	/* Not enough room in dst, add NUL and traverse rest of src. */
	if (nleft == 0) {
		if (dsize != 0)
			*dst = '\0';		/* NUL-terminate dst */
		while (*src++)
			;
	}

	return(src - osrc - 1);	/* count does not include NUL */
}

char *xu_strdup(const char *src)
{
	size_t sz = strlen(src) + 1;
	void *p;

	p = xu_malloc(sz);
	memcpy(p, src, sz);

	return p;
}

void xu_nonblock(int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFD);
	flags |= O_NONBLOCK;
	fcntl(fd, F_SETFD, flags);
}

int xu_luaclass(lua_State *L, const char *class_name, struct luaL_Reg *methods,
		const char *mt_name, struct luaL_Reg *mt_methods)
{
	luaL_newmetatable(L, mt_name);
	lua_pushstring(L, "__index");
	lua_pushvalue(L, -2);
	lua_settable(L, -3);
	luaL_openlib(L, NULL, mt_methods, 0);
	lua_pop(L, 1);
	luaL_openlib(L, class_name, methods, 0);
	lua_pop(L, 1);

	return 0;
}

int xu_luatraceback(lua_State *L)
{
	const char *msg = lua_tostring(L, 1);

	if (msg) {
		luaL_traceback(L, L, msg, 1);
	} else {
		lua_pushliteral(L, "(no error message)");
	}
	return 1;
}

static void * __alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
	if (nsize == 0) {
		xu_free(ptr);
		return NULL;
	}
	return xu_realloc(ptr, nsize);
}

lua_State *xu_newstate()
{
	return lua_newstate(__alloc, NULL);
}

void xu_create_metatable(lua_State *L, const char *name, luaL_Reg reg[])
{
	luaL_newmetatable(L, name);
	lua_pushstring(L, "__index");
	lua_pushvalue(L, -2);
	lua_settable(L, -3);
	luaL_openlib(L, NULL, reg, 0);
	lua_pop(L, 1);
}

