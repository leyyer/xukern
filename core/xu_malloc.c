#include <stdlib.h>
#include <stdio.h>

#include "xu_malloc.h"

#ifdef USE_JEMALLOC
#include "jemalloc/jemalloc.h"
#endif

static void __oom(size_t size)
{
	fprintf(stderr, "xmalloc: out of memory, expect %d bytes.\n", size);
	fflush(stderr);
	abort();
}

void *xu_calloc(size_t n, size_t size)
{
	void *p;
#ifdef USE_JEMALLOC
	p = je_calloc(n, size);
#else
	p = calloc(n, size);
#endif

	if (!p) {
		__oom(size);
	}

	return p;
}

void *xu_realloc(void *p, size_t size)
{
	void *np;

#ifdef USE_JEMALLOC
	np = je_realloc(p, size);
#else
	np = realloc(p, size);
#endif

	if (!np) {
		__oom(size);
	}

	return np;
}

void  xu_free(void *p)
{
	if (p) {
#ifdef USE_JEMALLOC
		je_free(p);
#else
		free(p);
#endif
	}
}

