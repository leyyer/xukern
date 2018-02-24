#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include "xu_impl.h"
#include "xu_kern.h"
#include "cJSON.h"
#include "uv.h"

struct worker {
	int count;
	uv_mutex_t lock;
	uv_cond_t  cond;
	int sleep;
	int quit;
};

static void *__malloc(size_t sz)
{
	return xu_malloc(sz);
}

static void wakeup(struct worker *w, int busy)
{
	if (w->sleep >= w->count - busy) {
		uv_cond_signal(&w->cond);
	}
}

static void thread_worker(void *p)
{
	struct worker *w = p;
	struct queue *q = NULL;

	while (!w->quit) {
		do {
			q = xu_dispatch_message(q, 0);
		} while (q);

		uv_mutex_lock(&w->lock);
		++w->sleep;
		if (!w->quit) {
			uv_cond_wait(&w->cond, &w->lock);
		}
		--w->sleep;
		uv_mutex_unlock(&w->lock);
	}
}

static void timer_loop(struct worker *w)
{
	while (1) {
		xu_updatetime();
		if (xu_actors_total() == 0)
			break;
		wakeup(w, w->count - 1);
		usleep(2500);
	}
	uv_mutex_lock(&w->lock);
	w->quit = 1;
	uv_cond_broadcast(&w->cond);
	uv_mutex_unlock(&w->lock);
}

static void start(int thread)
{
	int i;
	uv_thread_t pid[thread+1];
	struct worker *w = xu_malloc(sizeof *w);

	w->count = thread;
	w->sleep = 0;
	w->quit = 0;
	uv_mutex_init(&w->lock);
	uv_cond_init(&w->cond);

	for (i = 0; i < thread; ++i) {
		uv_thread_create(&pid[i+1], thread_worker, w);
	}
	
	timer_loop(w);

	for (i = 0; i < thread; ++i) {
		uv_thread_join(&pid[i + 1]);
	}

	xu_free(w);
}

void xu_kern_start(const char *modpath, int threads)
{
	static cJSON_Hooks hook = {
		__malloc,
		xu_free
	};

	signal(SIGPIPE, SIG_IGN);

	cJSON_InitHooks(&hook);
	uv_replace_allocator(__malloc, xu_realloc, xu_calloc, xu_free);

	xu_timer_init();
	xu_kern_init(modpath);
	start(threads);
}

