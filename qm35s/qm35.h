/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __QM35_H___
#define __QM35_H___

#include <linux/gpio.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/spinlock.h>
#include <linux/miscdevice.h>

#include "uci_ioctls.h"
#include "hsspi.h"
#include "hsspi_uci.h"
#include "hsspi_coredump.h"
#include "hsspi_log.h"
#include "debug.h"

#define FWUPDATER_SPI_SPEED_HZ 20000000
#define DEFAULT_SPI_CLOCKRATE 3000000

#define DEBUG_CERTIFICATE_SIZE 2560
#define QM_RESET_LOW_MS 2
/*
 * value found using a SALAE
 */
#define QM_BOOT_MS 450
#define QM_BEFORE_RESET_MS 450

#define DRV_VERSION "7.2.11-rc3"

struct regulator;

/**
 * struct qm35_ctx - QM35 driver context
 *
 */
struct qm35_ctx {
	unsigned int state;
	struct miscdevice uci_dev;
	struct spi_device *spi;
	struct gpio_desc *gpio_csn;
	struct gpio_desc *gpio_reset;
	struct gpio_desc *gpio_ss_rdy;
	struct gpio_desc *gpio_ss_irq;
	struct gpio_desc *gpio_exton;
	struct gpio_desc *gpio_wakeup;
	int ss_rdy_irq;
	spinlock_t lock;
	bool out_data_wait;
	bool out_active;
	bool soc_error;
	struct hsspi hsspi;
	struct uci_layer uci_layer;
	struct coredump_layer coredump_layer;
	struct log_layer log_layer;
	struct debug debug;
	struct regulator *vdd1;
	struct regulator *vdd2;
	struct regulator *vdd3;
	struct regulator *vdd4;
	bool regulators_enabled;
	bool log_qm_traces;
	struct sscd_desc *sscd;

	/* qmrom support */
	struct wait_queue_head qmrom_wq_ready;
	bool qmrom_qm_ready;
	bool flashing;
};

static inline unsigned int qm35_get_state(struct qm35_ctx *qm35_hdl)
{
	return qm35_hdl->state;
}

static inline void qm35_set_state(struct qm35_ctx *qm35_hdl, int state)
{
	unsigned long flags;

	spin_lock_irqsave(&qm35_hdl->lock, flags);
	qm35_hdl->state = state;
	spin_unlock_irqrestore(&qm35_hdl->lock, flags);
}

static inline int qm35_reset(struct qm35_ctx *qm35_hdl, int timeout_ms,
			     bool run)
{
	if (qm35_hdl->gpio_reset) {
		qm35_set_state(qm35_hdl, QM35_CTRL_STATE_RESET);
		gpiod_set_value(qm35_hdl->gpio_reset, 1);
		if (!run)
			return 0;
		usleep_range(timeout_ms * 1000UL, timeout_ms * 1000UL);
		gpiod_set_value(qm35_hdl->gpio_reset, 0);
		qm35_set_state(qm35_hdl, QM35_CTRL_STATE_UNKNOWN);
		return 0;
	}

	return -ENODEV;
}

int qm35_reset_sync(struct qm35_ctx *qm35_hdl);

int qm_get_dev_id(struct qm35_ctx *qm35_hdl, uint16_t *dev_id);
int qm_get_soc_id(struct qm35_ctx *qm35_hdl, uint8_t *soc_id);

void qm35_hsspi_start(struct qm35_ctx *qm35_hdl);
void qm35_hsspi_stop(struct qm35_ctx *qm35_hdl);

void qmrom_set_fwname(const char *name);

#endif /* __QM35_H___ */
