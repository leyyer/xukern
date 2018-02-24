#ifndef __XA_IMPL_H___
#define __XA_IMPL_H___
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "xu_core.h"
#include "xu_malloc.h"
#include "xu_util.h"
#include "xu_lua.h"

#define SPIN_INIT(x)    spinlock_init(&(x)->lock)
#define SPIN_LOCK(x)    spinlock_lock(&(x)->lock)
#define SPIN_UNLOCK(x)  spinlock_unlock(&(x)->lock)
#define SPIN_RELEASE(x) spinlock_destroy(&(x)->lock)

void xu_timer_init(void);
void xu_kern_init(const char *mod_path);
int xu_actors_total();

/* init environment */
void xu_envinit(void);
/* deinit environment */
void xu_envexit(void);

void * xu_ctx_loop(xuctx_t ctx);
/* 
 * builtin lua objects.
 */
void init_lua_buffer(lua_State *L);
void init_lua_net(lua_State *L, xuctx_t);
void init_lua_timer(lua_State *L, xuctx_t);
#endif

