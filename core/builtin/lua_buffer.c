#include "xu_impl.h"
#include "lua_buffer.h"

/*
 * buffer object.
 */
struct buffer * buffer_new(lua_State *L, size_t len)
{
	struct buffer *b;

	b = lua_newuserdata(L, len + sizeof *b);
	b->cap = len;
	luaL_getmetatable(L, BUF_CLS_MTNAME);
	lua_setmetatable(L, -2);

	return b;
}

static int __buf_alloc(lua_State *L)
{
	size_t len;

	len = luaL_checkinteger(L, 1);
	buffer_new(L, len);
	return 1;
}

static int __buf_cap(lua_State *L)
{
	struct buffer *b = BUFFER(1);

	lua_pushinteger(L, b->cap);
	return 1;
}

static int __buf_off_len(lua_State *L, int dump)
{
	struct buffer *b = BUFFER(1);
	int top, off, len = b->cap;

	top = lua_gettop(L);
	switch (top) {
		case 3:
			len = luaL_checkinteger(L, 3);
		case 2:
			off = luaL_checkinteger(L, 2);
			break;
		default:
			off = 0;
			len = b->cap;
			break;
	}


	if (off < b->cap) {
		if (off + len > b->cap) {
			len = b->cap - off;
		}
		/* printf("cap %d, off = %d, len = %d\n", b->cap, off, len); */
		if (dump) {
			xu_log_blob(b->data + off, len);
		} else {
			lua_pushlstring(L, b->data + off, len);
		}
		return 1;
	}

	return 0;
}

static int __buf_dump(lua_State *L)
{
	__buf_off_len(L, 1);
	return 0;
}

static int __buf_toString(lua_State *L)
{
	return __buf_off_len(L, 0);
}

static int __buf_clear(lua_State *L)
{
	struct buffer *b = BUFFER(1);

	memset(b->data, 0, b->cap);

	return 0;
}

static int __buf_fill(lua_State *L)
{
	struct buffer *b = BUFFER(1);
	int8_t var = luaL_checkinteger(L, 2);

	memset(b->data, var, b->cap);
	return 0;
}

static int __buf_writeInt8(lua_State *L)
{
	struct buffer *b = BUFFER(1);
	int8_t value = luaL_checkinteger(L, 2);
	int off = luaL_checkinteger(L, 3);

	if (off >= 0 && off < b->cap) {
		b->data[off] = value;
	}
	return 0;
}

static int  __buf_readInt8(lua_State *L)
{
	struct buffer *b = BUFFER(1);
	int off = luaL_checkinteger(L, 2);

	if (off >= 0 && off < b->cap) {
		lua_pushinteger(L, b->data[off]);
		return 1;
	}

	return 0;
}

static int __buf_gc(lua_State *L)
{
	struct buffer *b = BUFFER(1);
	xu_println("buffer gc: %p", b);
	return 0;
}

void init_lua_buffer(lua_State *L)
{
	static luaL_reg buf[] = {
		{"alloc", __buf_alloc},
		{NULL, NULL}
	};

	static luaL_reg mt_buf[] = {
		{"len",       __buf_cap},
		{"clear",     __buf_clear},
		{"fill",      __buf_fill},
		{"writeInt8", __buf_writeInt8},
		{"readInt8",  __buf_readInt8},
		{"toString",  __buf_toString},
		{"dump",      __buf_dump},
		{"__gc",      __buf_gc},
		{NULL, NULL}
	};

	xu_luaclass(L, BUF_CLS_NAME, buf, BUF_CLS_MTNAME, mt_buf);
}

