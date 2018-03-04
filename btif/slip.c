#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include "slip.h"
#include "xu_malloc.h"

#define BITS_PER_LONG 32
#define BIT(nr)       (1UL << (nr))
#define BIT_MASK(nr)  (1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)  ((nr) / BITS_PER_LONG)

struct fd_rdwr {
	struct slip_rdwr base;
	int fd;
};

struct slip {
	struct slip_rdwr *rdwr;

	unsigned long flags;		/* Flag values/ mode etc	*/

	int           mtu;
	/* These are pointers to the malloc()ed frame buffers. */
	unsigned char *rbuff;		/* receiver buffer		*/
	int            rcount;         /* received chars counter       */

	/* frame callback */
	void         (*frame)(struct slip *, void *arg, unsigned char *buf, int len);
	void         *arg;
};

static ssize_t __write(struct slip_rdwr *srd, const void *buf, size_t size)
{
	int n;
	const unsigned char *sbuf = buf;
	struct fd_rdwr *fdr = (struct fd_rdwr *)srd;

	while (size > 0) {
		n = write(fdr->fd, sbuf, size);
		if (n > 0) {
			sbuf += n;
			size -= n;
		} else if (n < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			else {
				perror("slip_send: ");
				break;
			}
		}
	}
	return sbuf - (const unsigned char *)buf;
}

static ssize_t __read(struct slip_rdwr *srd, void *buf, size_t size)
{
	int r;
	unsigned char *sbuf = buf;
	struct fd_rdwr *fdr = (struct fd_rdwr *)srd;

retry:
	r = read(fdr->fd, sbuf, size);
	if (r == -1) {
		if (errno == EINTR || errno == EAGAIN)
			goto retry;
	}
	if (r > 0) {
		sbuf += r;
		size -= r;
	}

	return sbuf - (unsigned char *)buf;
}

static int __close(struct slip_rdwr *srd)
{
	struct fd_rdwr *fdr = (struct fd_rdwr *)srd;

	xu_free(fdr);
	return 0;
}

static struct slip_rdwr * __fdrdwr_get(int fd)
{
	struct fd_rdwr *fdr;

	fdr = xu_calloc(1, sizeof *fdr);
	fdr->base.read = __read;
	fdr->base.write = __write;
	fdr->base.close = __close;
	fdr->fd = fd;

	return &fdr->base;
}

struct slip *slip_generic_new(struct slip_rdwr *srd, int mtu)
{
	int sz;
	struct slip *sl;

	mtu += SLIP_MIN_LEN;

	sz = sizeof *sl + mtu;
	sl = xu_calloc(1, sz);

	sl->rdwr  = srd;
	sl->mtu   = mtu;
	sl->rbuff = (unsigned char *)&sl[1];

	return sl;
}

struct slip *slip_new(int fd, int mtu)
{
	return slip_new_with_rdwr(__fdrdwr_get(fd), mtu);
}

void slip_free(struct slip *sl)
{
	if (sl->rdwr && sl->rdwr->close)
		sl->rdwr->close(sl->rdwr);
	xu_free(sl);
}

void slip_set_callback(struct slip *sl, void (*frame)(struct slip *, void *arg, unsigned char *buf, int len), void *arg)
{
	sl->frame = frame;
	sl->arg   = arg;
}

static int slip_esc(unsigned char *s, unsigned char *d, int len)
{
	unsigned char *ptr = d;
	unsigned char c;

	/*
	 * Send an initial END character to flush out any
	 * data that may have accumulated in the receiver
	 * due to line noise.
	 */

	*ptr++ = END;

	/*
	 * For each byte in the packet, send the appropriate
	 * character sequence, according to the SLIP protocol.
	 */

	while (len-- > 0) {
		switch (c = *s++) {
		case END:
			*ptr++ = ESC;
			*ptr++ = ESC_END;
			break;
		case ESC:
			*ptr++ = ESC;
			*ptr++ = ESC_ESC;
			break;
		default:
			*ptr++ = c;
			break;
		}
	}
	*ptr++ = END;
	return ptr - d;
}

static inline int test_and_clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);
	unsigned long old;

	old = *p;
	*p = old & ~mask;

	return (old & mask) != 0;
}

static inline void clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

	*p &= ~mask;
}

static inline void set_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

	*p  |= mask;
}

static inline int test_bit(int nr, const volatile unsigned long *addr)
{
	return 1UL & (addr[BIT_WORD(nr)] >> (nr & (BITS_PER_LONG-1)));
}

static void slip_unesc(struct slip *sl, unsigned char s)
{

	switch (s) {
	case END:
		if (!test_and_clear_bit(SLF_ERROR, &sl->flags) &&
		    (sl->rcount >= SLIP_MIN_LEN)) {
			if (sl->frame)
				sl->frame(sl, sl->arg, sl->rbuff, sl->rcount);
		}
		clear_bit(SLF_ESCAPE, &sl->flags);
		sl->rcount = 0;
		return;

	case ESC:
		set_bit(SLF_ESCAPE, &sl->flags);
		return;
	case ESC_ESC:
		if (test_and_clear_bit(SLF_ESCAPE, &sl->flags))
			s = ESC;
		break;
	case ESC_END:
		if (test_and_clear_bit(SLF_ESCAPE, &sl->flags))
			s = END;
		break;
	}

	if (!test_bit(SLF_ERROR, &sl->flags))  {
		if (sl->rcount < sl->mtu)  {
			sl->rbuff[sl->rcount++] = s;
			return;
		}
		set_bit(SLF_ERROR, &sl->flags);
	}
}

int  slip_recv(struct slip *sl)
{
	int i, n;
	unsigned char mtu[sl->mtu];

	n = sl->rdwr->read(sl->rdwr, mtu, sizeof mtu);
	for (i = 0; i < n; ++i) {
		slip_unesc(sl, mtu[i]);
	}

	return n >= 0 ? 0 : -1;
}

int slip_send(struct slip *sl, unsigned char *buf, int len)
{
	unsigned char mtu[len << 1];
	int tot, n;

	n = slip_esc(buf, mtu, len);
	if (n <= 0) { /* invalid length */
		return -1;
	}

	tot = sl->rdwr->write(sl->rdwr, mtu, n);
	return tot == n ? 0 : -2;
}

