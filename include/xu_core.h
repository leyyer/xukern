#ifndef __XU_CORE_H__
#define __XU_CORE_H__
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include "atomic.h"
#include "rwlock.h"
#include "spinlock.h"

typedef struct xu_ctx * xuctx_t;
/* 
 * initialize the xucore library
 *
 * return 0 on success, -1 on error.
 */
int xu_core_init(int argc, char *argv[]);

/*
 * deinitialize library.
 */
void xu_core_exit(void);

/*
 * allocate a context.
 */
xuctx_t xu_ctx_new(void);

/*
 * run ctx loop.
 */
int xu_ctx_run(xuctx_t ctx);

/*
 * load a script file.
 *
 * ctx  : allocate from xu_ctx_new().
 * file : lua file to load.
 * return 0: on success, < 0 on error.
 */
int xu_ctx_load(xuctx_t ctx, const char *file);

/*
 * load environ value from file.
 */
void xu_env_load(const char *file);

/*
 * dump all environ to file.
 */
void xu_env_dump(const char *file);

/*
 * iterator foreach environ.
 *
 * if `map' return non-zero stop iterating.
 *
 * XXX: must not call xu_env_XXX in `map' function, if so
 * lead to deadlock.
 */
void xu_env_map(int (*map)(void *ud, const char *key, const char *value), void *ud);

/*
 * get environment
 *
 * env: environ name.
 * buf: environ value to save, if it is null, return core'buffer.
 * size: buf size
 *
 * return buf if buf is not null or core'buffer, NULL on failure.
 */
const char *xu_getenv(const char *env, char *buf, size_t size);

/*
 * set environ.
 */
void xu_setenv(const char *env, const char *value);

#ifdef __cplusplus
}
#endif
#endif

