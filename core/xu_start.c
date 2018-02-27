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

#define XU_DEFAULT_THREADS (2)
struct workqueue {
	uv_work_t req;
	int busy;
};

struct worker {
	int count;

	uv_timer_t sched;

	uv_prepare_t wup;

	int sleep;
	int quit;

	struct workqueue wq[0];
};

static void *__malloc(size_t sz)
{
	return xu_malloc(sz);
}

static void parsing(int argc, char *argv[])
{
	int i, c;
	char *agv[argc];

	/* parse arguments, copy argvs locals to parse.*/
	for (i = 0; i < argc; ++i) {
		agv[i] = xu_strdup(argv[i]);
	}

	while ((c = getopt(argc, agv, "c:?")) != -1) {
		switch (c) {
			case 'c':
				xu_env_load(optarg);
				break;
		}
	}

	for (i = 0; i < argc; ++i) {
		xu_free(agv[i]);
	}
}

static void load_logger()
{
	struct xu_actor *ctx;
	char log[256] = {0};
	char *p, *args;

	if (xu_getenv("logger", log, sizeof log) != NULL) {
		args = log;
		p = strsep(&args, " \t\r\n");
		args = strsep(&args, "\r\n");
	} else {
		p = "logger";
		args = "";
	}

	ctx = xu_actor_new(p, args);
	if (ctx == NULL) {
		xu_println("can't find logger");
		exit(-1);
	}
}

void xu_kern_init(int argc, char *argv[])
{
	static cJSON_Hooks hook = {
		__malloc,
		xu_free
	};
	const char *s, *mod_path;

	signal(SIGPIPE, SIG_IGN);

	cJSON_InitHooks(&hook);
	uv_replace_allocator(__malloc, xu_realloc, xu_calloc, xu_free);
	xu_envinit();

	parsing(argc, argv);

	if ((s = xu_getenv("threads", NULL, 0)) != NULL) {
		setenv("UV_THREADPOOL_SIZE", s, 1);
	} else {
		char thr[4] = {0};
		snprintf(thr, sizeof thr, "%d", XU_DEFAULT_THREADS);
		setenv("UV_THREADPOOL_SIZE", thr, 1);
	}

	xu_timer_init();
	xu_io_init();

	mod_path = xu_getenv("mod_path", NULL, 0);
	xu_kern_global_init(mod_path ?: "./svc" );
	load_logger();
}

static void on_work(uv_work_t *req)
{
	struct worker *w = req->data;
	struct queue *q = NULL;

	do {
		q = xu_dispatch_message(q, 0);
	} while (q && !w->quit);
}

static void on_done(uv_work_t *req, int status)
{
	struct worker *w = req->data;
	struct workqueue *wq = (struct workqueue *)req;

	ATOM_CAS(&wq->busy, 1, 0);
	ATOM_INC(&w->sleep);
/*	xu_error(NULL, "thread %p done: %d", wq, w->sleep); */
}

static void do_wakeup(struct worker *w, int busy)
{
	int i;
	struct workqueue *wq;
	if (w->sleep >= w->count - busy) {
		for (i = 0; i < w->count; ++i) {
			wq = &w->wq[i];
			if (wq->busy == 0) {
				ATOM_CAS(&wq->busy, 0, 1);
				if (uv_queue_work(uv_default_loop(), &wq->req, on_work, on_done) == 0) {
					ATOM_DEC(&w->sleep);
/*					xu_error(NULL, "thread %p wakeup: %d", wq, w->sleep); */
					break;
				} else {
					ATOM_CAS(&wq->busy, 1, 0);
				}
			}
		}
	}
}

static void on_timer(uv_timer_t *d)
{
	struct worker *w = d->data;

	xu_updatetime();
	do_wakeup(w, w->count - 1);
}

static void on_prepare(uv_prepare_t *p)
{
	struct worker *w = p->data;
	struct workqueue *wq;
	int i;

	if (xu_actors_total() == 0) { /* stop loopping */
		fprintf(stderr, "stopping\n");
		ATOM_CAS(&w->quit, 0, 1);
		for (i = 0; i < w->count; ++i) {
			wq = &w->wq[i];
			if (wq->busy) {
				uv_cancel((uv_req_t *)&wq->req);
			}
		}
		uv_stop(uv_default_loop());
	} else {
		do_wakeup(w, 0);
	}
}

void xu_kern_start()
{
	struct worker *w;
	struct workqueue *wq;
	int i, threads = XU_DEFAULT_THREADS;
	char *s;
	uv_loop_t *loop = uv_default_loop();

	s = getenv("UV_THREADPOOL_SIZE");
	if (s) 
		threads = atoi(s);
	w = xu_calloc(1, sizeof *w + threads * sizeof (struct workqueue));
	w->count = threads;

	for (i = 0; i < threads; ++i) {
		wq = &w->wq[i];
		wq->busy = 0;
		wq->req.data = w;
	}

	uv_timer_init(loop, &w->sched);
	uv_timer_start(&w->sched, on_timer, 3, 3);
	w->sched.data = w;
	w->sleep = threads;

	uv_prepare_init(loop, &w->wup);
	uv_prepare_start(&w->wup, on_prepare);
	w->wup.data = w;

	xu_error(NULL, "running");
	uv_run(loop, UV_RUN_DEFAULT);
}

