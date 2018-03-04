/* BT-U131A handler */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <fcntl.h>
#include <limits.h>
#include "slip.h"
#include "btif.h"
#include "xu_malloc.h"

#define BTIF_BAUDRATE      B115200
#define BTIF_CMD_OFF       (3)
#define BTIF_CSTRLEN       (20)

#define BTIF_REGSTRY_OK    (0x00)
#define BTIF_REGSTRY_FAIL  (0x01)
#define BTIF_REGSTRY_UNDIS (0x02)

#define BTIF_SRV_VALID      (0x00)
#define BTIF_SRV_INVALID    (0x01)

struct btif {
	int fd;
	struct slip *slip;

	struct btif_vtbl *vtbl;

	int on;
	int registry;
	int valid;

	int crypt_type;
	unsigned int mtime;
	char dsp_ver[BTIF_CSTRLEN + 1];
	char tmo_ver[BTIF_CSTRLEN + 1];
	char dmo_ver[BTIF_CSTRLEN + 1];
	char adapt_ver[BTIF_CSTRLEN + 1];
	char kern_ver[BTIF_CSTRLEN + 1];

	void (*cmd)(void *arg, unsigned char cmd, unsigned char *cmdbuf, int cmdlen);
	void *arg;
};

void btif_set_vtbl(btif_t bi, struct btif_vtbl *vi)
{
	bi->vtbl = vi;
}

static unsigned char __cs(unsigned char *d, int length)
{
	unsigned int sum;
	int i;

	sum = 0;
	for (i = 0; i < length; ++i) {
		sum += d[i];
	}

	return (~(unsigned char)sum + 0x7a);
}

static int __cmd_0x26(btif_t bi, unsigned char cmd, unsigned char *cbuf, int clen)
{
	int pos;

	bi->crypt_type = cbuf[0];

	pos = 4;
	memcpy(&bi->mtime, cbuf + pos, 3);
	pos += 3;

	memcpy(bi->dsp_ver, cbuf + pos, BTIF_CSTRLEN);
	pos += BTIF_CSTRLEN;

	memcpy(bi->tmo_ver, cbuf + pos, BTIF_CSTRLEN);
	pos += BTIF_CSTRLEN;

	memcpy(bi->dmo_ver, cbuf + pos, BTIF_CSTRLEN);
	pos += BTIF_CSTRLEN;

	memcpy(bi->adapt_ver, cbuf + pos, BTIF_CSTRLEN);
	pos += BTIF_CSTRLEN;

	memcpy(bi->kern_ver, cbuf + pos, BTIF_CSTRLEN);
	pos += BTIF_CSTRLEN;

	bi->on = 1;
	fprintf(stderr, "dsp: %s, tmo: %s, dmo: %s, adapter: %s, kernel: %s\n",
			bi->dsp_ver, bi->tmo_ver, bi->dmo_ver, bi->adapt_ver, bi->kern_ver);
	return 1;
}

static int __cmd_0x13(btif_t bi, unsigned char cmd, unsigned char *cbuf, int clen)
{
	if (bi->vtbl && bi->vtbl->on_work_mode) {
		bi->vtbl->on_work_mode(bi->vtbl, cbuf[0]);
		return 1;
	}
	return 0;
}

static int __cmd_0x30_0x31(btif_t bi, unsigned char cmd, unsigned char *cbuf, int clen)
{
	int r = 1;

	switch (cmd) {
		case BTIF_CMD_REG_STATUS:
			bi->registry = cbuf[0];
			break;

		case BTIF_CMD_SERVICE_STATUS:
			bi->valid = cbuf[0];
			break;

		default:
			r = 0;
	}
	fprintf(stderr, "%s : %s\n", cmd == BTIF_CMD_REG_STATUS ? "registry" : "service", cbuf[0] == 0 ? "succ" : "failed");
	return r;
}

static int __cmd_0x47(btif_t bi, unsigned char cmd, unsigned char *cbuf, int clen)
{
	if (bi->vtbl && bi->vtbl->on_ptton) {
		bi->vtbl->on_ptton(bi->vtbl, cbuf[0] == 0x00, cbuf[1] == 0x00);
		return 1;
	}
	return 0;
}

static int __cmd_0x42(btif_t bi, unsigned char cmd, unsigned char *cbuf, int clen)
{
	if (bi->vtbl && bi->vtbl->on_dial) {
		bi->vtbl->on_dial(bi->vtbl, clen, cbuf);
		return 1;
	}
	return 0;
}

static int __cmd_0x43(btif_t bi, unsigned char cmd, unsigned char *cbuf, int clen)
{
	if (bi->vtbl && bi->vtbl->on_dial) {
		bi->vtbl->on_ringing(bi->vtbl, clen, cbuf);
		return 1;
	}
	return 0;
}

static int __cmd_0x45(btif_t bi, unsigned char cmd, unsigned char *cbuf, int clen)
{
	if (bi->vtbl && bi->vtbl->on_offhook) {
		bi->vtbl->on_offhook(bi->vtbl, cbuf[0] == 0x00, cbuf[1] == 0x00);
		return 1;
	}
	return 0;
}

static int __cmd_0x49(btif_t bi, unsigned char cmd, unsigned char *cbuf, int clen)
{
	if (bi->vtbl && bi->vtbl->on_pttoff) {
		bi->vtbl->on_pttoff(bi->vtbl, cbuf[0] == 0x00, cbuf[3], cbuf + 4);
		return 1;
	}
	return 0;
}

static int __cmd_0x4b(btif_t bi, unsigned char cmd, unsigned char *cbuf, int clen)
{
	if (bi->vtbl && bi->vtbl->on_onhook) {
		bi->vtbl->on_onhook(bi->vtbl, cbuf[0]);
		return 1;
	}
	return 0;
}

static int __filter(btif_t bi, unsigned char cmd, unsigned char *cbuf, int clen)
{
	int i, found = 0;
	static struct cmd_map {
		unsigned char cmd;
		int (*handler)(btif_t bi, unsigned char cmd, unsigned char *cbuf, int clen);
	} maps[] = {
		{BTIF_CMD_MODULE_STATUS, __cmd_0x26},
		{BTIF_CMD_WORK_MODE,     __cmd_0x13},
		{BTIF_CMD_REG_STATUS,    __cmd_0x30_0x31},
		{BTIF_CMD_SERVICE_STATUS,__cmd_0x30_0x31},
		{BTIF_CMD_PN_CONFIRM,    __cmd_0x47},
		{BTIF_CMD_DIAL_CONFIRM,  __cmd_0x42},
		{BTIF_CMD_RINGING,       __cmd_0x43},
		{BTIF_CMD_UOH_CONFIRM,   __cmd_0x45},
		{BTIF_CMD_TX_INDICTOR,   __cmd_0x49},
		{BTIF_CMD_ON_HOOK,       __cmd_0x4b},
		{0, NULL}
	};

	for (i = 0; maps[i].handler; ++i) {
		if (maps[i].cmd == cmd) {
			found = maps[i].handler(bi, cmd, cbuf, clen);
			break;
		}
	}

	return found;
}

static void __cmd_handler(struct slip *sl, void *arg, unsigned char *buf, int len)
{
	btif_t bi = arg;
	int cmd, s;
	int clen;

	(void)sl;
	cmd  = buf[0];
	clen = (buf[1] << 8) | buf[2];

	if (clen > (len - SLIP_MIN_LEN)) { /* cmd(1) + len(2) + cs(1) */
		fprintf(stderr, "data too short: %d, expected: %d\n", len - 4, clen);
		return;
	}

	s = __cs(buf + BTIF_CMD_OFF, len - SLIP_MIN_LEN);
	if (s != buf[len-1]) {
		fprintf(stderr, "checksum don't match: 0x%x, expected: 0x%x\n", s, buf[len-1]);
		return;
	}

	if (__filter(bi, cmd, buf + BTIF_CMD_OFF, clen)) { /* filte out */
		return;
	}

	if (bi->cmd) {
		bi->cmd(bi->arg, cmd, buf + BTIF_CMD_OFF, clen);
	} else {
		fprintf(stderr, "haven't cmd handler drop it.\n");
	}
}

static void __set_tty_mode(int fd)
{
	struct termios tio;

	tcgetattr(fd, &tio);

	cfsetispeed(&tio, BTIF_BAUDRATE);
	cfsetospeed(&tio, BTIF_BAUDRATE);

	/* raw */
	cfmakeraw(&tio);

	/* 8N1 */
	tio.c_cflag  &= ~PARENB;
	tio.c_cflag &= ~CSTOPB;
	tio.c_cflag &= ~CSIZE;
	tio.c_cflag &= ~CRTSCTS;
	tio.c_cflag |= CS8;

	/* disable software flow control */
	tio.c_iflag &= ~(IXON | IXOFF | IXANY);

	tio.c_cc[VMIN] = 1;

	tcsetattr(fd, TCSANOW, &tio);
}

static int __tty_fd(const char *dev)
{
	int fd;
	char path[PATH_MAX] = {0};

	fd = open(dev, O_RDWR | O_NOCTTY | O_NDELAY);
	if (fd < 0 && dev[0] != '/') {
		snprintf(path, PATH_MAX - 1, "/dev/%s", dev);
		fd = open(path, O_RDWR | O_NDELAY);
	}

	if (fd < 0) { /* open device failed. */
		fprintf(stderr, "open device: %s failed (%s).\n", dev, strerror(errno));
		return fd;
	}

	__set_tty_mode(fd);
	return fd;
}

btif_t btif_generic_new(struct slip_rdwr *srd)
{
	btif_t bi = NULL;
	struct slip *sl;
	int mtu = BTIF_MTU;
	char *e;

	e = getenv("BTIF_MTU");
	if (e) {
		mtu = atoi(e);
	}

	sl = slip_generic_new(srd, mtu);
	if (sl == NULL) {
		fprintf(stderr, "can't create slip object.\n");
		goto failed;
	}

	bi = xu_calloc(1, sizeof *bi);
	if (bi == NULL) {
		fprintf(stderr, "%s: out of memory.\n", __func__);
		goto failed;
	}
	bi->fd      = -1;
	bi->slip    = sl;
	bi->on      = 0;
	bi->registry = BTIF_REGSTRY_FAIL;
	bi->valid   = BTIF_SRV_INVALID;

	slip_set_callback(bi->slip, __cmd_handler, bi);

	return bi;
failed:
	return NULL;
}

btif_t btif_new(const char *dev)
{
	int fd;
	btif_t bi = NULL;
	struct slip *sl;
	int mtu = BTIF_MTU;
	char *e;

	if ((fd = __tty_fd(dev)) < 0) {
		return NULL;
	}
	e = getenv("BTIF_MTU");
	if (e) {
		mtu = atoi(e);
	}

	sl = slip_new(fd, mtu);
	if (sl == NULL) {
		fprintf(stderr, "can't create slip object.\n");
		goto failed;
	}

	bi = xu_calloc(1, sizeof *bi);
	if (bi == NULL) {
		fprintf(stderr, "%s: out of memory.\n", __func__);
		goto failed;
	}
	bi->fd      = fd;
	bi->slip    = sl;
	bi->on      = 0;
	bi->registry = BTIF_REGSTRY_FAIL;
	bi->valid   = BTIF_SRV_INVALID;

	slip_set_callback(bi->slip, __cmd_handler, bi);

	return bi;
failed:
	close(fd);
	return NULL;
}

int btif_get_fd(btif_t bi)
{
	return bi->fd;
}

void btif_notify_handler(btif_t bi, void (*cmd)(void *arg, unsigned char cmd, unsigned char *cmdbuf, int cmdlen), void *arg)
{
	bi->cmd = cmd;
	bi->arg = arg;
}

void btif_free(btif_t bi)
{
	slip_free(bi->slip);
	close(bi->fd);
	xu_free(bi);
}

int btif_cmd(btif_t bi, unsigned char cmd, unsigned char *cbuf, int clen)
{
	unsigned char frame[clen + SLIP_MIN_LEN];
	unsigned char cs;

	frame[0] = cmd & 0xff;
	frame[1] = (clen & 0xff00) >> 8;
	frame[2] = (clen & 0xff);

	if (clen > 0) {
		memcpy(frame + BTIF_CMD_OFF, cbuf, clen);
	}

	cs = __cs(&frame[BTIF_CMD_OFF], clen);

	frame[clen + BTIF_CMD_OFF] = cs;

	return slip_send(bi->slip, frame, sizeof frame);
}

int btif_step(btif_t bi)
{
	return slip_recv(bi->slip);
}

int btif_set_id(btif_t bi, unsigned char id_type, unsigned char *id, int len)
{
	unsigned char idc[len + 1];

	idc[0] = id_type;
	if (len > 0) {
		memcpy(idc + 1, id, len);
	}

	return btif_cmd(bi, BTIF_CMD_SET_ID, idc, len + 1);
}

int btif_set_group_number(btif_t bi, unsigned char *numbers, int len)
{
	if (len < 0x32) {
		fprintf(stderr, "group number length is invalid, expect 50, but got %d.\n", len);
		return -1;
	}
	return btif_cmd(bi, BTIF_CMD_GROUP, numbers, 0x32);
}

int btif_set_cfreq(btif_t bi, unsigned char *freq, int len)
{
	if (len < 0x1e) {
		fprintf(stderr, "group number length is invalid, expect 30, but got %d.\n", len);
		return -1;
	}
	return btif_cmd(bi, BTIF_CMD_SET_CFREQ, freq, 0x1e);
}

int btif_set_dfreq(btif_t bi, unsigned char *freq, int len)
{
	if (len < 0x5a) {
		fprintf(stderr, "group number length is invalid, expect 90, but got %d.\n", len);
		return -1;
	}
	return btif_cmd(bi, BTIF_CMD_SET_DFREQ, freq, 0x5a);
}

int btif_dialout_ex(btif_t bi, unsigned char prio, unsigned char secret, unsigned char call_type, unsigned char duplex,
		unsigned char hook_mode, unsigned char dial_mode, unsigned char num_len, unsigned char *numbers)
{
	unsigned char frame[8 + num_len];

	if ((bi->registry == BTIF_REGSTRY_OK) && (bi->valid == BTIF_SRV_VALID)) {
		frame[0] = prio;
		frame[1] = secret;
		frame[2] = call_type;
		frame[3] = duplex;
		frame[4] = hook_mode;
		frame[5] = 0x00;
		frame[6] = dial_mode;
		frame[7] = num_len;
		memcpy(frame + 8, numbers, num_len);

		return btif_cmd(bi, BTIF_CMD_DIALOUT, frame, sizeof frame);
	}
	return -1;
}

/*
 * XXX: reboot device.
 */
int btif_reboot(btif_t bi)
{
	unsigned char frame[] = {BTIF_CMD_REBOOT, 0x00, 0x00};

	/*
	 * XXX: 
	 * please see the spec doc:
	 *  the command have no length field.
	 */
	return slip_send(bi->slip, frame, sizeof frame);
}

/*
 * XXX: * shutdown device.
 */
int btif_shutdown(btif_t bi)
{
	/* XXX: have no length field. */
	unsigned char frame[] = {BTIF_CMD_POWER_OFF, 0x00, 0x00};

	return slip_send(bi->slip, frame, sizeof frame);
}

int btif_factory_reset(btif_t bi)
{
	unsigned char frame[] = {BTIF_CMD_RECOVER_CONF, 0x00, 0x00};
	return slip_send(bi->slip, frame, sizeof frame);
}

