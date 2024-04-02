/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __UCI_IOCTLS_H___
#define __UCI_IOCTLS_H___

#include <asm/ioctl.h>

#define UCI_DEV_NAME "uci"
#define UCI_IOC_TYPE 'U'

#define QM35_CTRL_RESET _IOR(UCI_IOC_TYPE, 1, unsigned int)
#define QM35_CTRL_GET_STATE _IOR(UCI_IOC_TYPE, 2, unsigned int)
#define QM35_CTRL_FW_UPLOAD _IOR(UCI_IOC_TYPE, 3, unsigned int)
#define QM35_CTRL_POWER _IOW(UCI_IOC_TYPE, 4, unsigned int)

/* qm35 states */
enum { QM35_CTRL_STATE_UNKNOWN = 0x0000,
       QM35_CTRL_STATE_OFF = 0x0001,
       QM35_CTRL_STATE_RESET = 0x0002,
       QM35_CTRL_STATE_COREDUMP = 0x0004,
       QM35_CTRL_STATE_READY = 0x0008,
       QM35_CTRL_STATE_FW_DOWNLOADING = 0x0010,
       QM35_CTRL_STATE_UCI_APP = 0x0020,
};

#endif /* __UCI_IOCTLS_H___ */
