#ifndef __XU_IO__H__
#define __XU_IO__H__
#include <stdint.h>
#include <netinet/in.h>

#define XIE_EVENT_ERROR      1
#define XIE_EVENT_LISTEN     2
#define XIE_EVENT_CONNECTION 3
#define XIE_EVENT_MESSAGE    4 /* UDP */
#define XIE_EVENT_DATA       5
#define XIE_EVENT_CLOSE      6

#define XIE_ERR_SUCC      0
#define XIE_ERR_DNS       1
#define XIE_ERR_LISTEN    2
#define XIE_ERR_RECV_DATA 3
#define XIE_ERR_SEND_DATA 4
#define XIE_ERR_LOOKUP    5
#define XIE_ERR_NOTSUPP   6
#define XIE_ERR_EOF       7

#define XIE_EVENT_MASK  (0xffffff)
#define XIE_EVENT_SHIFT (24)

union sockaddr_all {
	struct sockaddr     in;
	struct sockaddr_in  in4;
	struct sockaddr_in6 in6;
};

struct xu_io_event {
	uint32_t fdesc;
	size_t   size; /* 1 byte (event) + 3 bytes (length) */
	union {
		int  errcode;
		union sockaddr_all sa;
	} u;
	char data[0];
};

int xu_io_tcp_server(uint32_t handle, const char *addr, int port);
int xu_io_tcp_connect(uint32_t handle, const char *addr, int port);

int xu_io_fd_open(uint32_t handle, int fd);
int xu_io_write(uint32_t handle, uint32_t fdesc, const void *data, int len);

int xu_io_udp_server(uint32_t handle, const char *addr, int port);
int xu_io_udp_client(uint32_t handle, const char *p, int port);
int xu_io_udp_send(uint32_t handle, uint32_t fdesc, union sockaddr_all *, const void *data, int len);
#endif

