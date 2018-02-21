#ifndef __XA_RWLOCK_H__
#define __XA_RWLOCK_H__
#ifdef __cplusplus
extern "C" {
#endif

struct rwlock {
	int write;
	int read;
};

static inline void __attribute__((unused)) rwlock_init(struct rwlock *lock)
{
	lock->write = 0;
	lock->read = 0;
}

static inline void __attribute__((unused)) rwlock_rlock(struct rwlock *lock)
{
	for (;;) {
		while (lock->write) {
			__sync_synchronize();
		}
		__sync_add_and_fetch(&lock->read,1);
		if (lock->write) {
			__sync_sub_and_fetch(&lock->read,1);
		} else {
			break;
		}
	}
}

static inline void __attribute__((unused)) rwlock_wlock(struct rwlock *lock)
{
	while (__sync_lock_test_and_set(&lock->write,1)) {}
	while (lock->read) {
		__sync_synchronize();
	}
}

static inline void __attribute__((unused)) rwlock_wunlock(struct rwlock *lock)
{
	__sync_lock_release(&lock->write);
}

static inline void __attribute__((unused)) rwlock_runlock(struct rwlock *lock)
{
	__sync_sub_and_fetch(&lock->read,1);
}

#ifdef __cplusplus
}
#endif
#endif

