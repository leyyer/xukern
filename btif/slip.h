#ifndef __SLIP_H___
#define __SLIP_H___
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct slip_rdwr {
	ssize_t (*read)(struct slip_rdwr *, void *buf, size_t len);
	ssize_t (*write)(struct slip_rdwr *, const void *buf, size_t len);
	int     (*close)(struct slip_rdwr *);
};

struct slip;

#define SLIP_MIN_LEN    (4)     /* 1 byte cmd + 2 bytes length + 1 byte checksum */

/* SLIP protocol characters. */
#define END             0xC0		/* indicates end of frame	*/
#define ESC             0xDB		/* indicates byte stuffing	*/
#define ESC_END         0xDC		/* ESC ESC_END means END 'data'	*/
#define ESC_ESC         0xDD		/* ESC ESC_ESC means ESC 'data'	*/

#define SLF_ESCAPE	1               /* ESC received                 */
#define SLF_ERROR	2               /* Parity, etc. error           */

struct slip * slip_generic_new(struct slip_rdwr *srd, int mtu, int noesc);

/* 
 * create slip object. 
 *
 * fd  : the uart file descriptor.
 * mtu : max frame length.
 *
 * return : slip object on sucess, NULL on error.
 */
struct slip * slip_new(int fd, int mtu, int noesc);

/*
 * free slip object, must not use it afterwards.
 */
void          slip_free(struct slip *);

/* 
 * set frame handler.
 *
 * If it has a complete frame, call the `frame' functions.
 */
void slip_set_callback(struct slip *, void (*frame)(struct slip *, void *arg, unsigned char *buf, int len), void *arg);

/* 
 * If file descriptor READABLE, call this function. 
 * or call it periodicly.
 *
 * return: 0 on sucess, < 0 on error.
 */
int  slip_recv(struct slip *);

/*
 * send a slip frame.
 *
 * buf: frame data.
 * len: frame data length in bytes.
 *
 * return: 0 on sucess, < 0 on error.
 */
int slip_send(struct slip *sl, unsigned char *buf, int len);

#ifdef __cplusplus
}
#endif
#endif

