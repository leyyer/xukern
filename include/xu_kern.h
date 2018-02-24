#ifndef __XU_KERN_H__
#define __XU_KERN_H__
#ifdef __cplusplus
extern "C"
#endif

#include <stddef.h>
#include <stdint.h>

#define MESSAGE_TYPE_MASK (SIZE_MAX >> 8)
#define MESSAGE_TYPE_SHIFT ((sizeof (size_t) - 1) * 8)

#define MTYPE_TAG_DONTCOPY 0x10000
#define MTYPE_TAG_DASINT   0x20000

#define MTYPE_TIMEOUT 1

#define XU_NAME_LEN (64)

struct xu_msg {
	uint32_t    source;
	const void *data;
	size_t      sz; /* type | size */
};

struct queue;
struct xu_actor;

void xu_kern_start(const char *modpath, int threads);

typedef int (*xu_callback_t)(struct xu_actor *, void *ud, int type, uint32_t src, void *msg, size_t sz);

void xu_actor_callback(struct xu_actor *ctx, void *ud, xu_callback_t cb);

struct xu_actor *xu_handle_ref(uint32_t handle);
struct xu_actor *xu_actor_unref(struct xu_actor *ctx);

int xu_handle_msgput(uint32_t handle, struct xu_msg *msg);
int xu_send(struct xu_actor *ctx, uint32_t src, uint32_t dest, int type, void *msg, size_t sz);
int xu_sendname(struct xu_actor * context, uint32_t source, const char *addr , int type, void * data, size_t sz);

struct queue *xu_dispatch_message(struct queue *q, int weight);
uint32_t xu_actor_findname(const char *name);
const char *xu_actor_namehandle(uint32_t h, const char *name);

/*
 * time api.
 */
uint64_t xu_now(void);
uint64_t xu_starttime(void);
void xu_updatetime(void);
int xu_timeout(uint32_t handle, int time, int session);

#ifdef __cplusplus
}
#endif
#endif
