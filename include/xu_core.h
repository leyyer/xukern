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
int xu_ctx_run_once(xuctx_t ctx);

/*
 * load a script file.
 *
 * ctx  : allocate from xu_ctx_new().
 * file : lua file to load.
 * return 0: on success, < 0 on error.
 */
int xu_ctx_load(xuctx_t ctx, const char *file);

/*
 * stop running context.
 */
void xu_ctx_stop(xuctx_t ctx);

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

/* object functions */
struct xu_buf {
	char   *base;
	size_t len;
};

/* 
 * UDP
 */
typedef struct xu_udp* xu_udp_t;

/* 
 * create udp handle.
 */
xu_udp_t xu_udp_new(xuctx_t ctx);

/*
 * create udp handle with `fd'
 */
xu_udp_t xu_udp_new_with_fd(xuctx_t ctx, int fd);
/*
 * free udp handle.
 */
void xu_udp_free(xu_udp_t udp);

/*
 * set cookie data.
 */
void xu_udp_set_data(xu_udp_t udp, void *data);

/*
 * get cookied data.
 */
void *xu_udp_get_data(xu_udp_t udp);

/*
 * bind local ipv4 address.
 *
 * return < 0 on error, 0 on success.
 */
int xu_udp_bind(xu_udp_t udp, const char *addr, int port);

/*
 * bind local ipv6 address.
 *
 * return < 0 on error, 0 on success.
 */
int xu_udp_bind6(xu_udp_t udp, const char *addr, int port);

/*
 * udp send data.
 */
int xu_udp_send(xu_udp_t udp, struct xu_buf buf[], int nbuf, const char *addr, int port, void (*cb)(xu_udp_t, int status));

/*
 * ipv6 udp send.
 */
int xu_udp_send6(xu_udp_t udp, struct xu_buf buf[], int nbuf, const char *addr, int port, void (*cb)(xu_udp_t, int status));

/*
 * udp group operation.
 */
int xu_udp_add_membership(xu_udp_t udp, const char *mulitcast_addr, const char *interface_addr);
int xu_udp_drop_membership(xu_udp_t udp, const char *mulitcast_addr, const char *interface_addr);
int xu_udp_set_multicast_interface(xu_udp_t udp, const char *interface_addr);
/* 
 * options.
 */
int xu_udp_set_multicast_loopback(xu_udp_t udp, int on);
int xu_udp_set_multicast_ttl(xu_udp_t udp, int ttl);
int xu_udp_set_ttl(xu_udp_t udp, int ttl);
int xu_udp_set_broadcast(xu_udp_t udp, int on);

/*
 * start recv data.
 */
struct sockaddr;
int xu_udp_recv_start(xu_udp_t udp, void (*recv)(xu_udp_t, const void *buf, int nread, const struct sockaddr *addr));
/*
 * stop recv udp data.
 */
int xu_udp_recv_stop(xu_udp_t udp);

/*
 * query local address.
 */
int xu_udp_address(xu_udp_t udp, struct sockaddr *addr, int *size);

/*
 * buffer size.
 *
 * if *value == 0 get buffer size, otherwise set buffer size.
 */
int xu_udp_recv_buffer_size(xu_udp_t udp, int *value);
int xu_udp_send_buffer_size(xu_udp_t udp, int *value);

#ifdef __cplusplus
}
#endif
#endif

