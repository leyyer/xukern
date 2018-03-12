#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "xu_impl.h"
#include "xu_malloc.h"
#include "xu_kern.h"

#define TIME_NEAR_SHIFT  8
#define TIME_NEAR        (1 << TIME_NEAR_SHIFT)
#define TIME_LEVEL_SHIFT 6
#define TIME_LEVEL       (1 << TIME_LEVEL_SHIFT)
#define TIME_NEAR_MASK   (TIME_NEAR - 1)
#define TIME_LEVEL_MASK  (TIME_LEVEL - 1)

struct timer_event {
	uint32_t handle;
	int      session;
};

struct timer_node {
	struct timer_node *next;
	uint32_t expire;
};

struct link_list {
	struct timer_node head;
	struct timer_node *tail;
};

struct timer {
	struct link_list near[TIME_NEAR];
	struct link_list t[4][TIME_LEVEL];
	struct spinlock  lock;
	uint32_t         time;
	uint32_t         starttime;
	uint64_t         current;
	uint64_t         current_point;
};

static struct timer __TM[1];

static inline struct timer_node *link_clear(struct link_list *list)
{
	struct timer_node *r = list->head.next;

	list->head.next = 0;
	list->tail = &(list->head);

	return r;
}

static inline void link(struct link_list *list, struct timer_node *node)
{
	list->tail->next = node;
	list->tail = node;
	node->next = 0;
}

static void add_node(struct timer *T,struct timer_node *node) {
	uint32_t time=node->expire;
	uint32_t current_time=T->time;

	if ((time|TIME_NEAR_MASK)==(current_time|TIME_NEAR_MASK)) {
		link(&T->near[time&TIME_NEAR_MASK],node);
	} else {
		int i;
		uint32_t mask=TIME_NEAR << TIME_LEVEL_SHIFT;
		for (i=0;i<3;i++) {
			if ((time|(mask-1))==(current_time|(mask-1))) {
				break;
			}
			mask <<= TIME_LEVEL_SHIFT;
		}

		link(&T->t[i][((time>>(TIME_NEAR_SHIFT + i*TIME_LEVEL_SHIFT)) & TIME_LEVEL_MASK)],node);	
	}
}

static void timer_add(struct timer *T,void *arg, size_t sz,int time)
{
	struct timer_node *node = (struct timer_node *)xu_malloc(sizeof(*node)+sz);

	memcpy(node+1,arg,sz);

	spinlock_lock(&T->lock);

	node->expire = time + T->time;
	add_node(T, node);

	spinlock_unlock(&T->lock);
}

static void move_list(struct timer *tm, int level, int idx)
{
	struct timer_node *cur = link_clear(&tm->t[level][idx]);
	while (cur) {
		struct timer_node *tn = cur->next;
		add_node(tm, cur);
		cur = tn;
	}
}

static void timer_shift(struct timer *tm)
{
	int mask = TIME_NEAR;
	uint32_t ct = ++tm->time;

	if (ct == 0) {
		move_list(tm, 3, 0);
	} else {
		uint32_t time = ct >> TIME_NEAR_SHIFT;
		int i = 0;

		while ((ct & (mask - 1)) == 0) {
			int idx = time & TIME_LEVEL_MASK;
			if (idx != 0) {
				move_list(tm, i, idx);
				break;
			}
			mask <<= TIME_LEVEL_SHIFT;
			time >>= TIME_LEVEL_SHIFT;
			++i;
		}
	}
}

static inline void dispatch_list(struct timer_node *tn)
{
	struct timer_event *te;
	struct timer_node *tr;

	do {
		te = (struct timer_event *)(tn + 1);
		struct xu_actor *ctx = xu_handle_ref(te->handle);
		if (ctx) {
			xu_send(ctx, 0, te->handle, MTYPE_TIMEOUT | MTYPE_TAG_DONTCOPY, (void *)te->session, 0);
			xu_actor_unref(ctx);
		}
		tr = tn;
		tn = tn->next;
		xu_free(tr);
	} while (tn);
}

static inline void timer_execute(struct timer *tn)
{
	int idx = tn->time & TIME_NEAR_MASK;

	while (tn->near[idx].head.next) {
		struct timer_node *cur = link_clear(&tn->near[idx]);

		SPIN_UNLOCK(tn);
		dispatch_list(cur);
		SPIN_LOCK(tn);
	}
}

static void timer_update(struct timer *tr)
{
	SPIN_LOCK(tr);
	timer_execute(tr);
	timer_shift(tr);
	timer_execute(tr);
	SPIN_UNLOCK(tr);
}

uint64_t xu_now(void)
{
	return __TM->current;
}

uint64_t xu_starttime(void)
{
	return __TM->starttime;
}

static void systime(uint32_t *sec, uint32_t *cs)
{
	struct timespec ti;

	clock_gettime(CLOCK_REALTIME, &ti);

	*sec = (uint32_t)ti.tv_sec;
	*cs  = (uint32_t)(ti.tv_nsec / 1000000);
}

static uint64_t gettime(void)
{
	uint64_t t;
	struct timespec ti;

	clock_gettime(CLOCK_MONOTONIC, &ti);
	t = (uint64_t)ti.tv_sec * 1000;
	t += ti.tv_nsec / 1000000;

	return t;
}

void xu_timer_init(void)
{
	int i, j;
	uint32_t cur = 0;
	struct timer *r = __TM;

	for (i = 0; i < TIME_NEAR; ++i) {
		link_clear(&r->near[i]);
	}
	for (i = 0; i < 4; ++i) {
		for (j = 0; j < TIME_LEVEL; ++j) {
			link_clear(&r->t[i][j]);
		}
	}
	spinlock_init(&r->lock);
	systime(&r->starttime, &cur);
	r->current = cur;
	r->current_point = gettime();
}

void xu_updatetime(void)
{
	int i;
	uint32_t diff;
	uint64_t cp = gettime();

	if (cp < __TM->current_point) {
		xu_error(NULL, "time diff error: change from %lld to %lld", cp, __TM->current_point);
		__TM->current_point  = cp;
	} else if (cp != __TM->current_point) {
		diff = (uint32_t)(cp - __TM->current_point);
		__TM->current_point = cp;
		__TM->current += diff;
	//	printf("diff = %d\n", diff);
		for (i = 0; i < diff; ++i) {
			timer_update(__TM);
		}
	//	printf("diff = %d exit\n", diff);
	}
}

int xu_timeout(uint32_t handle, int time, int session)
{
	if (time <= 0) {
		struct xu_actor *ctx = xu_handle_ref(handle);
		if (ctx) {
			xu_send(ctx, 0, handle, MTYPE_TIMEOUT | MTYPE_TAG_DONTCOPY | MTYPE_TAG_DASINT, (void *)session, 0);
			xu_actor_unref(ctx);
		}
	} else {
		struct timer_event evt;
		evt.handle = handle;
		evt.session = session;
		timer_add(__TM, &evt, sizeof evt, time);
	}
	return session;
}

