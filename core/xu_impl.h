#ifndef __XA_IMPL_H___
#define __XA_IMPL_H___
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "atomic.h"
#include "spinlock.h"
#include "rwlock.h"

#include "xu_malloc.h"
#include "xu_util.h"

struct xu_actor;

void xu_timer_init(void);
uint64_t xu_starttime(void);
void xu_updatetime(void);

void xu_kern_global_init(const char *mod_path);
void xu_io_init(void);

int xu_actors_total();
void xu_log_output(FILE *f, uint32_t source, int type, const void * buffer, size_t sz);
FILE *xu_log_open(struct xu_actor *ctx, const char *logname, const char *def);
void xu_log_close(struct xu_actor * ctx, FILE *f, uint32_t handle);

/* init environment */
void xu_envinit(void);
/* deinit environment */
void xu_envexit(void);

void xu_env_load(const char *file);
#endif

