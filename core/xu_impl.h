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

void xu_timer_init(void);
void xu_kern_global_init(const char *mod_path);
void xu_io_init(void);

int xu_actors_total();
void xu_log_output(FILE *f, uint32_t source, int type, const void * buffer, size_t sz);

/* init environment */
void xu_envinit(void);
/* deinit environment */
void xu_envexit(void);

void xu_env_load(const char *file);
#endif

