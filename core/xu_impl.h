#ifndef __XA_IMPL_H___
#define __XA_IMPL_H___
#include <stdio.h>

#include "xu_core.h"
#include "xu_malloc.h"
#include "xu_util.h"

#define SPIN_INIT(x)    spinlock_init(&(x)->lock)
#define SPIN_LOCK(x)    spinlock_lock(&(x)->lock)
#define SPIN_UNLOCK(x)  spinlock_unlock(&(x)->lock)
#define SPIN_RELEASE(x) spinlock_destroy(&(x)->lock)

/* init environment */
void xu_envinit(void);
/* deinit environment */
void xu_envexit(void);

#endif

