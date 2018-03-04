#ifndef __BTIF_H___
#define __BTIF_H___

#ifdef __cplusplus
extern "C" {
#endif

/* 
 * See slip.h.
 *
 * slip reader/writer base object.
 */
struct slip_rdwr;

/* max frame size */
#define BTIF_MTU  (1024)

typedef struct btif * btif_t;

/*
 * open dev tty
 */
int btif_tty_open(const char *dev);

/*
 * open sock
 */
int btif_sock_open(const char *ifdev);

/*
 * Create BT-U131A object.
 *
 * dev: uart device name, such as: ttymxc1 or /dev/ttymxc1
 *
 * return: btif object or NULL
 */
btif_t btif_new(const char *dev);

/*
 * Create BT-U131A object.
 *
 * dev: netif device name, such as: sl0
 *
 * return: btif object or NULL
 */
btif_t btif_sock_new(const char *ifdev);

/*
 * Create a BT-U131A object with reader/writer
 */
btif_t btif_generic_new(struct slip_rdwr *srd, int noesc);

/* 
 * Close BT-U131A device.
 */
void   btif_free(btif_t);

/*
 * Get lowlevel file descriptor.
 */
int btif_get_fd(btif_t bi);

/*
 * set command handler.
 *
 * process command sent by device.
 */
void btif_notify_handler(btif_t, void (*cmd)(void *arg, unsigned char cmd, unsigned char *cmdbuf, int cmdlen), void *arg);

/*
 * send BT command.
 *
 * bi:   the btif_t object, returned by btif_new().
 * cbuf: command data.
 * clen: command data len.
 *
 * return: 0 on success, < 0 on error.
 */
int btif_cmd(btif_t bi, unsigned char cmd, unsigned char *cbuf, int clen);

/*
 * call btif_step periodic or call when fd readable.
 */
int btif_step(btif_t bi);

/* concreate api */
struct btif_vtbl {
	void (*on_work_mode)(struct btif_vtbl *, unsigned char mode);
	void (*on_ptton)(struct btif_vtbl *, int acquire, int allow_sending);
	void (*on_pttoff)(struct btif_vtbl *, int acquire, int num_len, unsigned char *numbers);
	void (*on_dial)(struct btif_vtbl *, int len, unsigned char *data);
	void (*on_ringing)(struct btif_vtbl *, int len, unsigned char *data);
	void (*on_offhook)(struct btif_vtbl *, int allow_speaking, int allow_ptt);
	void (*on_onhook)(struct btif_vtbl *, int reason);
};

/*
 * set concreate event table.
 */
void btif_set_vtbl(btif_t bi, struct btif_vtbl *vi);

/* commands */
#define BTIF_CMD_RECOVER_CONF    (0x06)
#define BTIF_CMD_POWER_OFF       (0x07)
#define BTIF_CMD_REBOOT          (0x08)
#define BTIF_CMD_SET_POWER       (0x10)
#define BTIF_CMD_SET_VOL         (0x11)
#define BTIF_CMD_SQUELCH         (0x12)
#define BTIF_CMD_WORK_MODE       (0x13)
#define BTIF_CMD_QUERY_PARAM     (0x14)
#define BTIF_CMD_SET_ID          (0x15)
#define BTIF_CMD_CHANNEL         (0x16)
#define BTIF_CMD_GROUP           (0x17)
#define BTIF_CMD_SET_CFREQ       (0x18)
#define BTIF_CMD_SET_DFREQ       (0x19)
#define BTIF_CMD_SET_CRYPT_TYPE  (0x1A)
#define BTIF_CMD_OFFHOOK         (0x1B)
#define BTIF_CMD_ALARM           (0x1C)
#define BTIF_CMD_CDEV_STATUS     (0x20)
#define BTIF_CMD_NET_TYPE        (0x21)
#define BTIF_CMD_SIG_STRENGTH    (0x23)
#define BTIF_CMD_CID_INDICATOR   (0x24)
#define BTIF_CMD_CODEC_INDICATOR (0x25)
#define BTIF_CMD_MODULE_STATUS   (0x26)
#define BTIF_CMD_MODULE_PARAM    (0x27)
#define BTIF_CMD_REG_STATUS      (0x30)
#define BTIF_CMD_SERVICE_STATUS  (0x31)
#define BTIF_CMD_REG_DROP        (0x32)
#define BTIF_CMD_REG_AGAIN       (0x33)
#define BTIF_CMD_DIALOUT         (0x40)
#define BTIF_CMD_DIALOUT_EX      (0x50)
#define BTIF_CMD_ENV_TORT        (0x41)
#define BTIF_CMD_DIAL_CONFIRM    (0x42)
#define BTIF_CMD_RINGING         (0x43)
#define BTIF_CMD_USER_OFFHOOK    (0x44)
#define BTIF_CMD_UOH_CONFIRM     (0x45)
#define BTIF_CMD_PTT_ON          (0x46)
#define BTIF_CMD_PN_CONFIRM      (0x47)
#define BTIF_CMD_PTT_OFF         (0x48)
#define BTIF_CMD_PF_CONFIRM      (0x53)
#define BTIF_CMD_TX_INDICTOR     (0x49)
#define BTIF_CMD_HOOK_ON         (0x4A)
#define BTIF_CMD_ON_HOOK         (0x4B)
#define BTIF_CMD_NOH_CONNECT     (0x4C)
#define BTIF_CMD_MUTE            (0x4D)
#define BTIF_CMD_DIN_CONFIRM     (0x51)
#define BTIF_CMD_DIN_RINGING     (0x52)
#define BTIF_CMD_CHANNEL_BUSY    (0x56)
#define BTIF_CMD_RING_QUEUING    (0x59)
#define BTIF_CMD_MSG_SEND        (0x60)
#define BTIF_CMD_MST_CONFIRM     (0x61)
#define BTIF_CMD_MSG_RECEPT      (0x62)
#define BTIF_CMD_MSG_REPORT      (0x63)
#define BTIF_CMD_SET_DYN_GROUP   (0x70)
#define BTIF_CMD_DEL_DYN_GROUP   (0x71)
#define BTIF_CMD_RDES_INDICTOR   (0x86)
#define BTIF_CMD_PWD_INPUT_NOTIFY (0x87)
#define BTIF_CMD_PWD_INPUT       (0x88)
#define BTIF_CMD_PWD_CONFIRM     (0x89)
#define BTIF_CMD_PWD_MODIFY      (0x8A)
#define BTIF_CMD_MODIFY_CONFIRM  (0x8B)
#define BTIF_CMD_MODULE_ID       (0x8C)
#define BTIF_CMD_SWITCH          (0x8D)
#define BTIF_CMD_KEEP_CONNECTING (0x8E)

/* set power */
#define BTIF_PWR_LOW  0x00
#define BTIF_PWR_MED  0x01
#define BTIF_PWR_HI   0x02
#define btif_set_power(bi, type)    ({unsigned char c = type; btif_cmd(bi, BTIF_CMD_SET_POWER, &c, 1);})

/* 
 * set volume.
 *
 * vol <- [0, 0]
 */
#define btif_set_vol(bi, vol)       ({unsigned char v = vol;  btif_cmd(bi, BTIF_CMD_SET_VOL, &v, 1);})

/*
 * set squelch limite.
 *
 * parm <- [0, 0xff].
 */
#define btif_set_squelch(bi, parm)  ({unsigned char p = parm; btif_cmd(bi, BTIF_CMD_SQUELCH, &p, 1);})

/*
 * get module parameters.
 */
#define btif_get_parameters(bi)   btif_cmd(bi, BTIF_CMD_QUERY_PARAM, NULL, 0)

/*
 * set id code.
 *
 * id_type: BTIF_ID_XXX.
 * id:  id code.
 * len: length in bytes.
 */
#define BTIF_ID_380M_MODE 0x00
#define BTIF_ID_400M_MODE 0x01
int btif_set_id(btif_t bi, unsigned char id_type, unsigned char *id, int len);

/*
 * set channel id.
 *
 * channel <- [0x01, 0x0A].
 */
#define btif_set_channel(bi, channel) ({unsigned char c = channel; btif_cmd(bi, BTIF_CMD_CHANNEL, &c, 1);})

/*
 * set group number.
 */
int btif_set_group_number(btif_t bi, unsigned char *numbers, int len);

/*
 * set net cfreq
 */
int btif_set_cfreq(btif_t bi, unsigned char *freq, int len);

/*
 * set direct net freq.
 */
int btif_set_dfreq(btif_t bi, unsigned char *freq, int len);

/* set crypto type */
#define BTIF_CRYPTO_CLEAR  0x00
#define BTIF_CRYPTO_SECRET 0x01
#define btif_set_crypto_type(bi, type) ({unsigned char t = (type); btif_cmd(bi, BTIF_CMD_SET_CRYPT_TYPE, &t, 1);})

/*
 * work mode.
 */
#define BTIF_WM_D380M  0x00
#define BTIF_WM_C380M  0x01
#define BTIF_WM_O400M  0x02
#define BTIF_WM_I400M  0x03
#define btif_set_work_mode(bi, mode) ({unsigned char m = (mode); btif_cmd(bi, BTIF_CMD_WORK_MODE, &m, 1);})

/*
 * dialing
 */
#define BTIF_DIAL_PRIO_NORMAL 0x00
#define BTIF_DIAL_PRIO_EMG    0x03
#define BTIF_DIAL_PRIO_ALARM  0x04

#define BTIF_CALL_TYPE_UC    0x00
#define BTIF_CALL_TYPE_MC    0x01
#define BTIF_CALL_TYPE_BC    0x03

#define BTIF_DIAL_HALFDUPLEX  0x00
#define BTIF_DIAL_FULLDUPLEX  0x01

#define BTIF_DIAL_MODE_PTT    0x00
#define BTIF_DIAL_MODE_NORMAL 0x01
int btif_dialout_ex(btif_t bi, unsigned char prio, unsigned char secret, unsigned char call_type, unsigned char duplex,
		unsigned char hook_mode, unsigned char dial_mode, unsigned char num_len, unsigned char *numbers);

/* dial unicast */
static int __attribute__((unused)) btif_dial_uc(btif_t bi, int ptt, unsigned char num_len, unsigned char *numbers)
{
	int r;

	if (ptt) {
		r = btif_dialout_ex(bi, BTIF_DIAL_PRIO_NORMAL, 0x00, 
				BTIF_DIAL_HALFDUPLEX, BTIF_CALL_TYPE_UC,
				0x01, BTIF_DIAL_MODE_PTT, num_len, numbers);
	} else {
		r = btif_dialout_ex(bi, BTIF_DIAL_PRIO_NORMAL, 0x00, 
				BTIF_DIAL_FULLDUPLEX, BTIF_CALL_TYPE_UC,
				0x01, BTIF_DIAL_MODE_NORMAL, num_len, numbers);
	}
	return r;
}

static int __attribute__((unused)) btif_dial_hookless_uc(btif_t bi, int ptt, unsigned char num_len, unsigned char *numbers)
{
	int r;

	if (ptt) {
		r =  btif_dialout_ex(bi, BTIF_DIAL_PRIO_NORMAL, 0x00,
				BTIF_DIAL_HALFDUPLEX, BTIF_CALL_TYPE_UC,
				0x00, BTIF_DIAL_MODE_PTT, num_len, numbers);
	} else {
		r =  btif_dialout_ex(bi, BTIF_DIAL_PRIO_NORMAL, 0x00,
				BTIF_DIAL_FULLDUPLEX, BTIF_CALL_TYPE_UC,
				0x00, BTIF_DIAL_MODE_NORMAL, num_len, numbers);
	}
	return r;
}

static int __attribute__((unused)) btif_dial_mc(btif_t bi, int ptt, unsigned char num_len, unsigned char *numbers)
{
	int r;

	if (ptt) {
		r =  btif_dialout_ex(bi, BTIF_DIAL_PRIO_NORMAL, 0x00,
				BTIF_DIAL_HALFDUPLEX, BTIF_CALL_TYPE_MC,
				0x00, BTIF_DIAL_MODE_PTT, num_len, numbers);
	} else {
		r =  btif_dialout_ex(bi, BTIF_DIAL_PRIO_NORMAL, 0x00,
				BTIF_DIAL_FULLDUPLEX, BTIF_CALL_TYPE_MC,
				0x00, BTIF_DIAL_MODE_NORMAL, num_len, numbers);
	}
	return r;
}

/* ptt off */
#define btif_ptt_off(bi)  btif_cmd(bi, BTIF_CMD_PTT_OFF, NULL, 0)
/* ptt on */
#define btif_ptt_on(bi)  btif_cmd(bi, BTIF_CMD_PTT_ON,  NULL, 0)

/* off hook */
static int __attribute__((unused)) btif_off_hook(btif_t bi, int ptt)
{
	unsigned char c = ptt ? 0x00 : 0x01;

	return btif_cmd(bi, BTIF_CMD_USER_OFFHOOK , &c, 1);
}

/* on hook */
#define btif_on_hook(bi)  btif_cmd(bi, BTIF_CMD_HOOK_ON, NULL, 0)

/*
 * reboot module.
 */
int btif_reboot(btif_t bi);

/*
 * shutdown.
 */
int btif_shutdown(btif_t bi);

/*
 * reset to default config.
 */
int btif_factory_reset(btif_t bi);
#ifdef __cplusplus
}
#endif
#endif

