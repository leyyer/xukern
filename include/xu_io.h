#ifndef __XU_IO__H__
#define __XU_IO__H__
#include <stdint.h>
#include <netinet/in.h>

#define XIE_EVENT_ERROR      1
#define XIE_EVENT_LISTEN     2
#define XIE_EVENT_CONNECT    3
#define XIE_EVENT_CONNECTION 4
#define XIE_EVENT_MESSAGE    5 /* UDP DATA */
#define XIE_EVENT_DATA       6
#define XIE_EVENT_CLOSE      7
#define XIE_EVENT_DRAIN      8

#define XIE_ERR_SUCC      0
#define XIE_ERR_DNS       1
#define XIE_ERR_LISTEN    2
#define XIE_ERR_RECV_DATA 3
#define XIE_ERR_SEND_DATA 4
#define XIE_ERR_LOOKUP    5
#define XIE_ERR_NOTSUPP   6
#define XIE_ERR_EOF       7
#define XIE_ERR_CONNECT   8

union sockaddr_all {
	struct sockaddr     in;
	struct sockaddr_in  in4;
	struct sockaddr_in6 in6;
};

struct xu_io_event {
	uint32_t fdesc;
	int      event;
	size_t   size; 
	union {
		int  errcode;
		union sockaddr_all sa;
	} u;
	char data[0];
};

uint32_t xu_io_tcp_server(uint32_t handle, const char *addr, int port);
uint32_t xu_io_tcp_connect(uint32_t handle, const char *addr, int port);
int xu_io_tcp_nodelay(uint32_t handle, uint32_t fdesc, int on);
int xu_io_tcp_keepalive(uint32_t handle, uint32_t fdesc, int enable, int delay);

uint32_t xu_io_fd_open(uint32_t handle, int fd);
int xu_io_write(uint32_t handle, uint32_t fdesc, const void *data, int len);

int xu_io_close(uint32_t handle, uint32_t fdesc);

uint32_t xu_io_udp_server(uint32_t handle, const char *addr, int port);
uint32_t xu_io_udp_open(uint32_t handle, int udp6);

int xu_io_udp_send(uint32_t handle, uint32_t fdesc, union sockaddr_all *, const void *data, int len);
int xu_io_udp_membership(uint32_t handle, uint32_t fdesc, const char *mcast, const char *iaddr, int join);
int xu_io_udp_set_multicast_loop(uint32_t handle, uint32_t fdesc, int on);
int xu_io_udp_set_broadcast(uint32_t handle, uint32_t fdesc, int on);
int xu_io_udp_set_ttl(uint32_t handle, uint32_t fd, int on);
int xu_io_udp_set_multicast_ttl(uint32_t handle, uint32_t fd, int on);

#endif

