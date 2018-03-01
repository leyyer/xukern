#ifndef __XA_SPINLOCK_H__
#define __XA_SPINLOCK_H__

#ifdef __cplusplus
extern "C" {
#endif

#define SPIN_INIT(x)    spinlock_init(&(x)->lock)
#define SPIN_LOCK(x)    spinlock_lock(&(x)->lock)
#define SPIN_UNLOCK(x)  spinlock_unlock(&(x)->lock)
#define SPIN_RELEASE(x) spinlock_destroy(&(x)->lock)

/*
 * Use gcc's instric instructions implentate 
 * spinlock.
 */
#ifndef __UNUSED__
#define __UNUSED__ __attribute__((unused))
#endif

struct spinlock {
	int lock;
};

/*
 * Initialize spinlock.
 */
static inline void __UNUSED__ spinlock_init(struct spinlock *lock)
{
	lock->lock = 0;
}

/*
 * Lock
 */
static inline void __UNUSED__ spinlock_lock(struct spinlock *lock)
{
	while (__sync_lock_test_and_set(&lock->lock, 1))
		;
}

/*
 * Trylock.
 *
 * return 1 on locked, 0 on faile.
 */
static inline int __UNUSED__ spinlock_trylock(struct spinlock *lock)
{
	return __sync_lock_test_and_set(&lock->lock, 1) == 0;
}

/*
 * Unlock.
 */
static inline void __UNUSED__ spinlock_unlock(struct spinlock *lock)
{
	__sync_lock_release(&lock->lock);
}

/*
 * Deinitialize.
 */
static inline void __UNUSED__ spinlock_destroy(struct spinlock *lock)
{
	(void)lock;
}

#ifdef __cplusplus
}
#endif
#endif

