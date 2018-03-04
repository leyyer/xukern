#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>

static int iface_get_id(int fd, const char *device)
{
	struct ifreq	ifr;

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));

	if (ioctl(fd, SIOCGIFINDEX, &ifr) == -1) {
		fprintf(stderr, "get index failed.\n");
		return -1;
	}

	return ifr.ifr_ifindex;
}

static int iface_bind(int fd, int ifindex)
{
	struct sockaddr_ll	sll;
	int			err;
	socklen_t		errlen = sizeof(err);

	memset(&sll, 0, sizeof(sll));
	sll.sll_family		= AF_PACKET;
	sll.sll_ifindex		= ifindex;
	sll.sll_protocol	= htons(ETH_P_ALL);

	if (bind(fd, (struct sockaddr *) &sll, sizeof(sll)) == -1) {
		fprintf(stderr, "find failed\n");
		return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	int s;
	char buf[BUFSIZ] = {0};
	int i, n, ifindex;

	if ((s = socket(AF_PACKET, SOCK_RAW, 0)) == -1) {
		fprintf(stderr, "create socket failed: %s\n", strerror(errno));
		exit(-1);
	}

	ifindex = iface_get_id(s, "sl0");
	iface_bind(s, ifindex);
	for (;;) {
		n = recvfrom(s, buf, sizeof buf, 0, NULL, NULL);
		printf("read %d bytes\n", n);
		if (n < 0)
			break;
		for (i = 0; i < n; ++i) {
			printf("%02x ", buf[i]);
		}
		printf("\n");
	}

	close(s);
	return 0;
}
