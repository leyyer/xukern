#ifndef __SLIP_H___
#define __SLIP_H___
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct slip;

struct slip_io {
	void (*ingoing)(struct slip *, void *data, void *buf, size_t len); /* slip data packet */
	int  (*outgoing)(struct slip *, void *data, const void *buf, size_t len); /* data to wire */
};


#define SLIP_MIN_LEN    (4)     /* 1 byte cmd + 2 bytes length + 1 byte checksum */

/* SLIP protocol characters. */
#define END             0xC0		/* indicates end of frame	*/
#define ESC             0xDB		/* indicates byte stuffing	*/
#define ESC_END         0xDC		/* ESC ESC_END means END 'data'	*/
#define ESC_ESC         0xDD		/* ESC ESC_ESC means ESC 'data'	*/

#define SLF_ESCAPE	1               /* ESC received                 */
#define SLF_ERROR	2               /* Parity, etc. error           */

/* 
 * create slip object. 
 *
 * mtu : max frame length.
 * extra_size: the extra size to malloc.
 *
 * return : slip object on sucess, NULL on error.
 */
struct slip * slip_new(int mtu, size_t extra_size);

/*
 * get the malloced extra data
 */
void *slip_get_extra(struct slip *);

/*
 * free slip object, must not use it afterwards.
 */
void slip_free(struct slip *);

/* 
 * set frame handler.
 *
 * If it has a complete frame, call the `frame' functions.
 */
void slip_set_callback(struct slip *, struct slip_io *io, void *data);

/* 
 * Put data into slip object.
 *
 * return: 0 on sucess, < 0 on error.
 */
int slip_recv(struct slip *sl, unsigned char *buf, int len);

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

