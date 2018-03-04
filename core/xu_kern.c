#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <dlfcn.h>
#include "xu_impl.h"
#include "xu_kern.h"

struct xu_actor;
struct queue;

struct xu_module {
	struct xu_module *next;
	void *handle;
	void* (*new)();
	int   (*init)(struct xu_actor *, void *, const char *p);
	void  (*free)(void *);
	char name[1];
};

struct xu_actor {
	struct xu_module *module;
	char name[XU_NAME_LEN + 1];

	void *instance;

	uint32_t handle;

	void *data;
	xu_callback_t cb;

	int ref;

	FILE *logfile;

	struct queue *q;
};

struct queue {
	struct queue *next;

	int cap;
	int head;
	int tail;
	struct xu_msg *msgs;

	struct spinlock lock;

	uint32_t handle;

	int in_global;

	int drop;
};

struct module_mgr {
	struct xu_module *head;
	struct spinlock  lock;
	char path[1];
};

static struct module_mgr *_mmgr = NULL;

static void xu_modules_init(const char *path)
{
	_mmgr = xu_calloc(1, (sizeof *_mmgr) + strlen(path) + 1);
	strcpy(_mmgr->path, path);
	SPIN_INIT(_mmgr);
}

static void *__get(struct xu_module *mod, const char *api)
{
	size_t name_size = strlen(mod->name);
	size_t api_size = strlen(api);
	char tmp[name_size + api_size + 1];
	void *sym;
	
	snprintf(tmp, sizeof tmp, "%s%s", mod->name, api);
	char *ptr = strrchr(tmp, '.');
	if (ptr == NULL)
		ptr = tmp;
	else 
		ptr += 1;
	sym = dlsym(mod->handle, ptr);
	if (sym == NULL) {
		fprintf(stderr, "load <%s:%s> failed: %s\n", mod->name, api, dlerror());
	}
	return sym;
}

static int __open_sym(struct xu_module *m)
{
	m->new   = __get(m, "_new");
	m->init  = __get(m, "_init");
	m->free  = __get(m, "_free");

	return (m->init == NULL);
}

static void *__try_open(const char *name)
{
	const char *l;
	const char *path = _mmgr->path;
	size_t psz = strlen(path);
	void *dl = NULL;
	char tmp[PATH_MAX], tp[psz + 1];
	const char *end = path + psz;

	do {
		memset(tmp, 0, sizeof tmp);
		memset(tp, 0, sizeof tp);
		while (*path == ';')
			++path;
		l = strchr(path, ';');
		if (l == NULL)
			l = path + strlen(path);
		int len = l - path;
		memcpy(tp, path, len);
		snprintf(tmp, sizeof tmp, "%s/%s.so", tp, name);
		dl = dlopen(tmp, RTLD_NOW | RTLD_GLOBAL);
		path = l;
	} while (dl == NULL && l < end);

	if (dl == NULL) {
		fprintf(stderr, "try open %s failed : %s\n", name, dlerror());
	}
	return dl;
}

static struct xu_module *_load_module(const char *name)
{
	struct xu_module *xo;
	size_t size;
	void *dl = __try_open(name);

	if (!dl) {
		return NULL;
	}
	size = sizeof *xo + strlen(name) + 1;
	xo = xu_calloc(1, size);
	strcpy(xo->name, name);
	xo->handle = dl;
	if (__open_sym(xo)) {
		xu_free(xo);
		return NULL;
	}
	xo->next = _mmgr->head;
	_mmgr->head = xo;
	return xo;
}

static struct xu_module *_find_module(const char *name)
{
	struct xu_module *xo, *found = NULL;

	xo = _mmgr->head;
	while (xo) {
		if (strcmp(xo->name, name) == 0) {
			found = xo;
			break;
		}
		xo = xo->next;
	}
	return found;
}

struct xu_module *xu_module_query(const char *name)
{
	struct xu_module *xo;

	SPIN_LOCK(_mmgr);

	xo = _find_module(name);
	if (xo == NULL) {
		xo = _load_module(name);
	}
	SPIN_UNLOCK(_mmgr);
	return xo;
}

static int _total_actors = 0;

static void inline __actors_inc()
{
	ATOM_INC(&_total_actors);
}

static void inline __actors_dec()
{
	ATOM_DEC(&_total_actors);
}

int xu_actors_total()
{
	return _total_actors;
}

/* queue */
struct queue_mgr {
	struct queue *head;
	struct queue *tail;
	struct spinlock lock;
};

static struct queue_mgr _Q[1];

/* actor */
struct actor_mgr {
	struct rwlock lock;

	uint32_t handle_index;

	struct queue *q;

	int slot_size;
	struct xu_actor **slot;
};

static struct actor_mgr *_am = NULL;

static void xu_actors_init(void)
{
	_am = xu_calloc(1, sizeof *_am);
	rwlock_init(&_am->lock);
	_am->handle_index = 1;
	_am->slot_size = 4;
	_am->slot = xu_malloc(_am->slot_size * sizeof _am->slot[0]);
}

static uint32_t xu_actor_register(struct xu_actor *xa)
{
	rwlock_wlock(&_am->lock);
	for (;;) {
		int i;
		for (i = 0; i < _am->slot_size; ++i) {
			uint32_t handle = (i + _am->handle_index) & 0xffffff;
			int hash = handle & (_am->slot_size - 1);
			if (_am->slot[hash] == NULL) {
				_am->slot[hash] = xa;
				_am->handle_index = handle + 1;
				rwlock_wunlock(&_am->lock);
				return handle;
			}
		}
		assert((_am->slot_size * 2 - 1) <= 0xffffff);
		struct xu_actor **new_slot = xu_calloc(_am->slot_size * 2, sizeof _am->slot[0]);
		for (i = 0; i < _am->slot_size; ++i) {
			int hash = (_am->slot[i])->handle & (_am->slot_size * 2 - 1);
			assert(new_slot[hash] == NULL);
			new_slot[hash] = _am->slot[i];
		}
		xu_free(_am->slot);
		_am->slot = new_slot;
		_am->slot_size *= 2;
	}
}

int xu_handle_retire(uint32_t handle)
{
	int r = 0;
	struct actor_mgr *s = _am;

	rwlock_wlock(&s->lock);
	uint32_t hash = handle & (s->slot_size - 1);
	struct xu_actor *ctx = s->slot[hash];

	if (ctx != NULL && ctx->handle == handle) {
		s->slot[hash] = NULL;
		r = 1;
	} else {
		ctx = NULL;
	}
	rwlock_wunlock(&s->lock);
	if (ctx) {
		xu_actor_unref(ctx);
	}
	return r;
}

void xu_queue_push(struct queue *q)
{
	SPIN_LOCK(_Q);
	assert(q->next == NULL);
	if (_Q->tail) {
		_Q->tail->next = q;
		_Q->tail = q;
	} else {
		_Q->head = _Q->tail = q;
	}
	SPIN_UNLOCK(_Q);
}

struct queue *xu_queue_pop()
{
	SPIN_LOCK(_Q);
	struct queue *q = _Q->head;
	if (q) {
		_Q->head = q->next;
		if (_Q->head == NULL) {
			assert(q == _Q->tail);
			_Q->tail = NULL;
		}
		q->next = NULL;
	}
	SPIN_UNLOCK(_Q);
	return q;
}

static struct queue *xu_queue_new(uint32_t h)
{
	struct queue *q = xu_malloc(sizeof *q);

	q->handle = h;
	q->cap = 64;
	q->head = 0;
	q->tail = 0;

	SPIN_INIT(q);
	q->in_global = 1;
	q->drop = 0;
	q->msgs = xu_calloc(q->cap, sizeof q->msgs[0]);
	q->next = NULL;

	return q;
}

static void expand_q(struct queue *q)
{
	struct xu_msg *nq = xu_calloc(q->cap * 2, sizeof *nq);
	int i;

	for (i = 0; i < q->cap; ++i) {
		nq[i] = q->msgs[(q->head + i) % q->cap];
	}
	q->head = 0;
	q->tail = q->cap;
	q->cap *= 2;
	xu_free(q->msgs);
	q->msgs = nq;
}

static void xu_queue_put(struct queue *q, struct xu_msg *msg)
{
	SPIN_LOCK(q);
	q->msgs[q->tail] = *msg;
	if (++q->tail >= q->cap) {
		q->tail = 0;
	}

	if (q->head == q->tail) {
		expand_q(q);
	}

	if (q->in_global == 0) {
		q->in_global = 1;
		xu_queue_push(q);
	}
	SPIN_UNLOCK(q);
}

int xu_queue_get(struct queue *q, struct xu_msg *msg)
{
	int ret = 0;

	SPIN_LOCK(q);
	if (q->head != q->tail) {
		*msg = q->msgs[q->head++];
		if (q->head >= q->cap) {
			q->head = 0;
		}
	} else {
		ret = 1;
	}
	if (ret)
		q->in_global = 0;
	SPIN_UNLOCK(q);
	return ret;
}

static void __drop_q(struct queue *q)
{
	struct xu_msg msg;

	while (!xu_queue_get(q, &msg)) {
		if (msg.size > 0)
			xu_free((void *)msg.data);
	}
	SPIN_RELEASE(q);
	xu_free(q->msgs);
	xu_free(q);
}

void xu_queue_free(struct queue *q)
{
	SPIN_LOCK(q);

	if (q->drop) {
		SPIN_UNLOCK(q);
		__drop_q(q);
	} else {
		xu_queue_push(q);
		SPIN_UNLOCK(q);
	}
}

uint32_t xu_queue_length(struct queue *q)
{
	int head, tail, cap;
	SPIN_LOCK(q);
	head = q->head;
	tail = q->tail;
	cap = q->cap;
	SPIN_UNLOCK(q);

	if (head <= tail) {
		return tail - head;
	}
	return tail + cap - head;
}

void xu_queue_mark_drop(struct queue *q)
{
	SPIN_LOCK(q);
	assert(q->drop == 0);
	if (!q->in_global)
		xu_queue_push(q);
	SPIN_UNLOCK(q);
}

struct xu_actor *xu_actor_unref(struct xu_actor *ctx)
{
	if (ATOM_DEC(&ctx->ref) == 0) {
		if (ctx->logfile)
			fclose(ctx->logfile);
		ctx->module->free(ctx->instance);
		xu_queue_mark_drop(ctx->q);
		xu_free(ctx);
		__actors_dec();
		return NULL;
	}
	return ctx;
}

int xu_actor_logon(struct xu_actor *ctx, const char *p)
{
	FILE *f = NULL, *lastf = ctx->logfile;

	if (lastf == NULL) {
		if (p) {
			f = fopen(p, "ab");
		} else {
			f = xu_log_open(ctx, ctx->handle);
		}
		if (f) {
			if (!ATOM_CAS_POINTER(&ctx->logfile, NULL, f)) {
				fclose(f);
			}
		}
	}
	return ctx->logfile == NULL;
}

void xu_actor_logoff(struct xu_actor *ctx)
{
	FILE *f = ctx->logfile;

	if (f) {
		if (ATOM_CAS_POINTER(&ctx->logfile, f, NULL)) {
			xu_log_close(ctx, f, ctx->handle);
		}
	}
}

struct xu_actor *xu_actor_new(const char *name, const char *p)
{
	struct xu_module *m;
	void *ud;
	struct xu_actor *xa = NULL;

	m = xu_module_query(name);
	if (!m) {
		xu_error(NULL, "can't find module %s", name);
		return NULL;
	}

	ud = m->new();
	if (ud == NULL) {
		xu_error(NULL, "module %s create failed.", name);
		return NULL;
	}

	xa = xu_calloc(1, sizeof *xa);
	xa->module = m;
	xa->instance = ud;
	xa->ref = 2;
	xa->handle = xu_actor_register(xa);
	struct queue *q = xa->q = xu_queue_new(xa->handle);

	__actors_inc();

	int r = m->init(xa, ud, p);
	if (r == 0) {
		struct xu_actor *ctx = xu_actor_unref(xa);
		xu_queue_push(q);
		return ctx;
	} else {
		xu_error(xa, "launch %s failed.", name);
		uint32_t handle = xa->handle;
		xu_actor_unref(xa);
		xu_queue_free(q);
		xu_handle_retire(handle);
		return NULL;
	}
}

void xu_actors_foreach(void *ud, int (*f)(void *ud, struct xu_actor *))
{
	struct xu_actor *ctx;
	int i = 0, n;

	rwlock_rlock(&_am->lock);
	n = _am->slot_size;
	while (i < n) {
		ctx = _am->slot[i];
		if (ctx) {
			ATOM_INC(&ctx->ref);
			rwlock_runlock(&_am->lock);
			f(ud, ctx);
			xu_actor_unref(ctx);
			rwlock_rlock(&_am->lock);
			n = _am->slot_size;
		}
		++i;
	}
	rwlock_runlock(&_am->lock);
}

struct xu_actor *xu_handle_ref(uint32_t handle)
{
	struct xu_actor *rest = NULL, *ctx;

	rwlock_rlock(&_am->lock);
	uint32_t hash = handle & (_am->slot_size - 1);
	ctx = _am->slot[hash];
	if (ctx && ctx->handle == handle) {
		rest = ctx;
		ATOM_INC(&rest->ref);
	}
	rwlock_runlock(&_am->lock);

	return rest;
}

static void dispatch_message(struct xu_actor *ctx, struct xu_msg *msg)
{
	int rmsg;

	if (ctx->logfile)
		xu_log_output(ctx->logfile, msg->source, msg->type, msg->data, msg->size);
	//fprintf(stderr, "type = %d, src = %d, len = %d\n", msg->type, msg->source, msg->size);
	rmsg = ctx->cb(ctx, ctx->data, msg->type, msg->source, (void *)msg->data, msg->size);
	if (!rmsg && msg->size > 0) {
		xu_free((void *)msg->data);
	}
}

struct queue *xu_dispatch_message(struct queue *q, int weight)
{
	if (q == NULL) {
		q = xu_queue_pop();
	}

	if (q == NULL) {
		return NULL;
	}
	uint32_t h = q->handle;
	struct xu_actor *ctx = xu_handle_ref(h);
	if (ctx == NULL) {
		xu_queue_free(q);
		return xu_queue_pop();
	}

	int i, n = 1;
	struct xu_msg msg;
	for (i = 0; i < n; ++i) {
		if (xu_queue_get(q, &msg)) {
			xu_actor_unref(ctx);
			return xu_queue_pop();
		} else if (i == 0 && weight >= 0) {
			n = xu_queue_length(q);
			n >>= weight;
		}

		if (ctx->cb == NULL) {
			if (msg.size > 0)
				xu_free((void *)msg.data);
		} else {
			dispatch_message(ctx, &msg);
		}
	}
	assert(q == ctx->q);
	struct queue *nq = xu_queue_pop();
	if (nq) {
		xu_queue_push(q);
		q = nq;
	}
	xu_actor_unref(ctx);
	return q;
}

void xu_actor_callback(struct xu_actor *ctx, void *ud, xu_callback_t cb)
{
	ctx->data = ud;
	ctx->cb = cb;
}

int xu_actor_name(struct xu_actor *ctx, char *buf, int len)
{
	int r;

	if (ctx->name[0] == '\0') {
		r = snprintf(buf, len, ":%08x", xu_actor_handle(ctx));
	} else {
		r = xu_strlcpy(buf, ctx->name, len);
	}

	return r;
}

const char *xu_actor_namehandle(uint32_t h, const char *name)
{
	struct xu_actor *xa;
	const char *r = NULL;

	xa = xu_handle_ref(h);
	if (xa) {
		xu_strlcpy(xa->name, name, sizeof xa->name);
		r = xa->name;
		xu_actor_unref(xa);
	}

	return (r);
}

uint32_t xu_actor_findname(const char *name)
{
	struct actor_mgr *s = _am;
	struct xu_actor *xa;

	rwlock_rlock(&s->lock);
	uint32_t h = 0;
	for (int i = 0; i < s->slot_size; ++i) {
		if (s->slot[i]) {
			xa = s->slot[i];
			if (strcmp(xa->name, name) == 0) {
				h = xa->handle;
				break;
			}
		}
	}
	rwlock_runlock(&s->lock);

	return h;
}

static void __filter_args(struct xu_actor *ctx, int type, void **data, size_t *sz)
{
	int needcopy = !(type & MTYPE_TAG_DONTCOPY);

	type &= 0xff;
	if (type & MTYPE_TAG_DASINT) {
		*sz = 0;
	} else if (needcopy && *data) {
		char *msg = xu_malloc(*sz);
		memcpy(msg, *data, *sz);
		*data = msg;
	}
}

int xu_send(struct xu_actor *ctx, uint32_t src, uint32_t dest, int type, void *msg, size_t sz)
{
	struct xu_actor *dctx;
	struct xu_msg smsg;

	if (dest == 0) {
		return -1;
	}

	dctx = xu_handle_ref(dest);
	if (dctx == NULL) {
		return -2;
	}

	if ((sz & MESSAGE_TYPE_MASK) != sz) {
		xu_error(ctx, "The message to %x is too large", dest);
		if (type & MTYPE_TAG_DONTCOPY) {
			xu_free(msg);
		}
		xu_actor_unref(dctx);
		return -1;
	}

	__filter_args(ctx, type, &msg, &sz);

	if (src == 0) {
		src = ctx->handle;
	}

	smsg.source = src;
	smsg.data = msg;
	smsg.type = type & MESSAGE_TYPE_MASK;
	smsg.size = sz;

	xu_queue_put(dctx->q, &smsg);
	xu_actor_unref(dctx);

	return 0;
}

int xu_sendname(struct xu_actor *context, uint32_t source, const char *addr , int type, void * data, size_t sz)
{
	if (source == 0) {
		source = context->handle;
	}
	uint32_t des = 0;
	if (addr[0] == ':') {
		des = strtoul(addr+1, NULL, 16);
	} else if (addr[0] == '.') {
		des = xu_actor_findname(addr + 1);
		if (des == 0) {
			if (type & MTYPE_TAG_DONTCOPY) {
				xu_free(data);
			}
			return -1;
		}
	} else {
		return -2;
	}
	return xu_send(context, source, des, type, data, sz);
}

int xu_handle_msgput(uint32_t handle, struct xu_msg *msg)
{
	struct xu_actor *ctx = xu_handle_ref(handle);

	if (ctx == NULL) {
		return -1;
	}
	xu_queue_put(ctx->q, msg);
	xu_actor_unref(ctx);
	return 0;
}

uint32_t xu_actor_handle(struct xu_actor *ctx)
{
	return ctx->handle;
}

void xu_kern_global_init(const char *mod_path)
{
	xu_modules_init(mod_path);
	xu_actors_init();
}

