#include <stdlib.h>
#include <stdio.h>

#include "xu_malloc.h"

#ifdef USE_JEMALLOC
#include "jemalloc/jemalloc.h"
#define malloc  je_malloc
#define calloc  je_calloc
#define realloc je_realloc
#define free    je_free
#endif

static void __oom(size_t size)
{
	fprintf(stderr, "xmalloc: out of memory, expect %d bytes.\n", size);
	fflush(stderr);
	abort();
}

void *xu_calloc(size_t n, size_t size)
{
	void *p = calloc(n, size);

	if (!p) {
		__oom(size);
	}

	return p;
}

void *xu_realloc(void *p, size_t size)
{
	void *np = realloc(p, size);

	if (!np) {
		__oom(size);
	}

	return np;
}

void  xu_free(void *p)
{
	if (p) {
		free(p);
	}
}

