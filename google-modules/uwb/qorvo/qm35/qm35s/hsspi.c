// SPDX-License-Identifier: GPL-2.0

/*
 * This file is part of the QM35 UCI stack for linux.
 *
 * Copyright (c) 2021 Qorvo US, Inc.
 *
 * This software is provided under the GNU General Public License, version 2
 * (GPLv2), as well as under a Qorvo commercial license.
 *
 * You may choose to use this software under the terms of the GPLv2 License,
 * version 2 ("GPLv2"), as published by the Free Software Foundation.
 * You should have received a copy of the GPLv2 along with this program.  If
 * not, see <http://www.gnu.org/licenses/>.
 *
 * This program is distributed under the GPLv2 in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GPLv2 for more
 * details.
 *
 * If you cannot meet the requirements of the GPLv2, you may not use this
 * software for any purpose without first obtaining a commercial license from
 * Qorvo.
 * Please contact Qorvo to inquire about licensing terms.
 *
 * QM35 HSSPI Protocol
 */

#include <linux/kernel.h>
#include <linux/delay.h>

#include "qm35-trace.h"
#include "hsspi.h"

/* STC HOST flags */
#define STC_HOST_WR BIT(7)
#define STC_HOST_PRD BIT(6)
#define STC_HOST_RD BIT(5)

/* STC SOC flags */
#define STC_SOC_ODW BIT(7)
#define STC_SOC_OA BIT(6)
#define STC_SOC_RDY BIT(5)
#define STC_SOC_ERR BIT(4)

#define SS_READY_TIMEOUT_MS (250)

#define MAX_SUCCESSIVE_ERRORS (5)
#define CHIP_RESET_RETRY (3)
#define SPI_CS_SETUP_DELAY_US (5)

#ifdef HSSPI_MANUAL_CS_SETUP
#define HSSPI_MANUAL_CS_SETUP_US SPI_CS_SETUP_DELAY_US
#endif

extern int spi_speed_hz;

struct hsspi_work {
	struct list_head list;
	enum hsspi_work_type type;
	union {
		struct {
			struct hsspi_block *blk;
			struct hsspi_layer *layer;
		} tx;
		struct completion *completion;
	};
};

int test_sleep_after_ss_ready_us = 0;

static inline bool layer_id_is_valid(struct hsspi *hsspi, u8 ul)
{
	return (ul < ARRAY_SIZE(hsspi->layers));
}

/**
 * get_work() - get a work from the list
 *
 * @hsspi: &struct hsspi
 *
 * Return: a &struct hsspi_work
 * The work can be:
 *  - a TX work enqueued by hsspi_send
 *  - a COMPLETION work used for synchronization (always allocated on stack)
 */
static struct hsspi_work *get_work(struct hsspi *hsspi)
{
	struct hsspi_work *hw;

	spin_lock(&hsspi->lock);

	hw = list_first_entry_or_null(&hsspi->work_list, struct hsspi_work,
				      list);
	if (hw)
		list_del(&hw->list);

	spin_unlock(&hsspi->lock);

	trace_hsspi_get_work(&hsspi->spi->dev, hw ? hw->type : -1);
	return hw;
}

/**
 * is_txrx_waiting() - is there something to do
 *
 * @hsspi: &struct hsspi
 *
 * Return: True if there is a TX work available or if the SS_IRQ flag
 * is set. False otherwise.
 */
static bool is_txrx_waiting(struct hsspi *hsspi)
{
	enum hsspi_state state;
	bool is_empty;

	spin_lock(&hsspi->lock);

	is_empty = list_empty(&hsspi->work_list);
	state = hsspi->state;

	spin_unlock(&hsspi->lock);

	trace_hsspi_is_txrx_waiting(&hsspi->spi->dev, is_empty, state);
	/*
	 * There is no work in the list but we must check SS_IRQ to
	 * know if we need to make an empty TX transfer with PRE_READ
	 * flag set.
	 */
	return !is_empty || ((state == HSSPI_RUNNING) &&
			     test_bit(HSSPI_FLAGS_SS_IRQ, hsspi->flags));
}

/**
 * hsspi_wait_ss_ready() - waits for ss_ready to be up
 *
 * @hsspi: &struct hsspi
 *
 * Return: 0 if ss_ready is up and EAGAIN if not.
 */
static int hsspi_wait_ss_ready(struct hsspi *hsspi)
{
	int ret;
	unsigned long flags;

	hsspi->waiting_ss_rdy = true;

	/**
	 * The following block allows to handle the case where
	 * the HSSPI_FLAGS_SS_READY is set but the SOC went into
	 * sleep (S3 or S4) and the exton interrupt has not been
	 * caught yet.
	 *
	 * In that case, the SOC is not ready and this function
	 * would return 0 immediately leading to a potential
	 * comms issue if we initiate the transfer when the SOC
	 * is preparing its SPI xfer.
	 *
	 * So, if this happens, HSSPI_FLAGS_SS_BUSY will be low
	 * and depending on the ss-rdy gpio state, we know if
	 * the SOC is ready. If not, we'll wait for the timeout
	 * (and wake the SOC if necessary before waiting).
	 */
	spin_lock_irqsave(&hsspi->flags_lock, flags);
	if (!test_bit(HSSPI_FLAGS_SS_BUSY, hsspi->flags)) {
		/* The ss_ready went low, so the fw is not busy anymore,
		 * if the ss_ready is high, we can proceed, else,
		 * either the fw went to sleep or crashed, in any case
		 * we need to wait for it to be ready again.
		 */
		clear_bit(HSSPI_FLAGS_SS_READY, hsspi->flags);
		spin_unlock_irqrestore(&hsspi->flags_lock, flags);
		if (gpiod_get_value(hsspi->gpio_ss_rdy)) {
			return 0;
		}
	} else {
		spin_unlock_irqrestore(&hsspi->flags_lock, flags);
	}

	/* Check if the QM went to sleep and wake it up if it did */
	if (!gpiod_get_value(hsspi->gpio_exton)) {
		hsspi->wakeup(hsspi);
	}

	ret = wait_event_interruptible_timeout(
		hsspi->wq_ready,
		test_and_clear_bit(HSSPI_FLAGS_SS_READY, hsspi->flags),
		msecs_to_jiffies(SS_READY_TIMEOUT_MS));
	if (ret == 0) {
		dev_warn(&hsspi->spi->dev,
			 "timed out waiting for ss_ready(%d)\n",
			 test_bit(HSSPI_FLAGS_SS_READY, hsspi->flags));
		return -EAGAIN;
	}
	if (ret < 0) {
		dev_err(&hsspi->spi->dev,
			"Error %d while waiting for ss_ready\n", ret);
		return ret;
	}
	/* WA: QM35 C0 have a very short (<100ns) ss_ready toggle
	 * in the ROM code when waking up from S4. If we transfer immediately,
	 * the ROM code will enter its command mode and we'll end up
	 * communicating with the ROM code instead of the firmware.
	 */
	if (!gpiod_get_value(hsspi->gpio_ss_rdy))
		return -EAGAIN;

	hsspi->waiting_ss_rdy = false;
	return 0;
}

#ifdef HSSPI_MANUAL_CS_SETUP
int hsspi_set_cs_level(struct spi_device *spi, int level)
{
	struct spi_transfer xfer[] = {
		{
			.cs_change = !level,
			.speed_hz = 1000000,
		},
	};

	return spi_sync_transfer(spi, xfer, ARRAY_SIZE(xfer));
}
#endif

/**
 * spi_xfer() - Single SPI transfer
 *
 * @hsspi: &struct hsspi
 * @tx: tx payload
 * @rx: rx payload
 * @length: payload length
 */
static int spi_xfer(struct hsspi *hsspi, const void *tx, void *rx,
		    size_t length)
{
	struct spi_transfer xfers[2] = {
		{
			.tx_buf = hsspi->host,
			.rx_buf = hsspi->soc,
			.len = sizeof(*(hsspi->host)),
			.speed_hz = spi_speed_hz,
		},
		{
			.tx_buf = tx,
			.rx_buf = rx,
			.len = length,
			.speed_hz = spi_speed_hz,
		},
	};
	int ret, retry = 5;

	hsspi->soc->flags = 0;
	hsspi->soc->ul = 0;
	hsspi->soc->length = 0;

	do {
		ret = hsspi_wait_ss_ready(hsspi);
		if (ret < 0) {
			continue;
		}

		if (test_sleep_after_ss_ready_us > 0)
			usleep_range(test_sleep_after_ss_ready_us,
				     test_sleep_after_ss_ready_us + 1);

		hsspi_set_spi_slave_busy(hsspi);
#ifdef HSSPI_MANUAL_CS_SETUP
		hsspi_set_cs_level(hsspi->spi, 0);
		udelay(HSSPI_MANUAL_CS_SETUP_US);
#endif
		ret = spi_sync_transfer(hsspi->spi, xfers, length ? 2 : 1);

		trace_hsspi_spi_xfer(&hsspi->spi->dev, hsspi->host, hsspi->soc,
				     ret);

		if (ret) {
			dev_err(&hsspi->spi->dev, "spi_sync_transfer: %d\n",
				ret);
			continue;
		}

		if (!(hsspi->soc->flags & STC_SOC_RDY) ||
		    (hsspi->soc->flags & 0x0f)) {
			hsspi->wakeup(hsspi);
			ret = -EAGAIN;
			continue;
		}

		/* All looks good! */
		break;
	} while ((ret == -EAGAIN) && (--retry > 0));

	if (!(hsspi->soc->flags & STC_SOC_RDY) || (hsspi->soc->flags & 0x0f)) {
		dev_err(&hsspi->spi->dev, "FW not ready (flags %#02x)\n",
			hsspi->soc->flags);
	}

	hsspi->waiting_ss_rdy = false;
	return ret;
}

static bool check_soc_flag(const struct device *dev, const char *func_name,
			   u8 soc_flags, bool is_tx)
{
	u8 expected;
	bool res;

	expected = is_tx ? 0x0 : STC_SOC_OA;
	res = (soc_flags & (STC_SOC_ERR | STC_SOC_OA)) == expected;

	if (!res) {
		dev_warn(dev, "%s: bad soc flags %#hhx, expected %#hhx\n",
			 func_name, soc_flags, expected);
	}
	return res;
}

/**
 * hsspi_rx() - request data from the QM35 on the HSSPI
 *
 * @hsspi: &struct hsspi
 * @ul: upper layer id
 * @length: length of data requested
 */
static int hsspi_rx(struct hsspi *hsspi, u8 ul, u16 length)
{
	struct hsspi_layer *layer;
	struct hsspi_block *blk;
	int ret;

	hsspi->host->flags = STC_HOST_RD;
	hsspi->host->ul = ul;
	hsspi->host->length = length;

	if (layer_id_is_valid(hsspi, ul)) {
		spin_lock(&hsspi->lock);
		layer = hsspi->layers[ul];
		spin_unlock(&hsspi->lock);
	} else
		layer = NULL;

	blk = layer ? layer->ops->get(layer, length) : NULL;
	if (blk)
		ret = spi_xfer(hsspi, NULL, blk->data, blk->size);
	else
		ret = spi_xfer(hsspi, NULL, NULL, 0);

	if (ret)
		goto out;

	if (!check_soc_flag(&hsspi->spi->dev, __func__, hsspi->soc->flags,
			    false)) {
		ret = -1;
	}

	if ((hsspi->soc->ul != ul) || (hsspi->soc->length != length)) {
		dev_warn(&hsspi->spi->dev,
			 "%s: received %hhu %hu but expecting %hhu %hu\n",
			 __func__, hsspi->soc->ul, hsspi->soc->length, ul,
			 length);
		ret = -1;
	}

	if (!(hsspi->soc->flags & STC_SOC_ODW) &&
	    test_and_clear_bit(HSSPI_FLAGS_SS_IRQ, hsspi->flags))
		hsspi->odw_cleared(hsspi);

out:
	if (blk)
		layer->ops->received(layer, blk, ret);

	return ret;
}

/**
 * hsspi_tx() - send a hsspi block to the QM35 on the HSSPI
 *
 * @hsspi: &struct hsspi
 * @layer: &struct hsspi_layer
 * @blk: &struct hsspi_block
 *
 * It also adds PRD flag if SS_IRQ is set. Therefore it will try a RX
 * transfer accordingly.
 */
static int hsspi_tx(struct hsspi *hsspi, struct hsspi_layer *layer,
		    struct hsspi_block *blk)
{
	int ret;

	hsspi->host->flags = STC_HOST_WR;
	hsspi->host->ul = layer->id;
	hsspi->host->length = blk->length;

	if (test_bit(HSSPI_FLAGS_SS_IRQ, hsspi->flags))
		hsspi->host->flags |= STC_HOST_PRD;

	ret = spi_xfer(hsspi, blk->data, NULL, blk->size);

	layer->ops->sent(layer, blk, ret);

	if (ret)
		return ret;

	/* Ignore tx check flags */
	check_soc_flag(&hsspi->spi->dev, __func__, hsspi->soc->flags, true);

	if ((hsspi->host->flags & STC_HOST_PRD) &&
	    (hsspi->soc->flags & STC_SOC_ODW))
		return hsspi_rx(hsspi, hsspi->soc->ul, hsspi->soc->length);

	return ret;
}

/**
 * hsspi_pre_read() - send a PRE_READ with no TX and do RX
 *
 * @hsspi: &struct hsspi
 *
 * This function is a particular case of hsspi_tx with no layer or
 * blk.
 *
 */
static int hsspi_pre_read(struct hsspi *hsspi)
{
	int ret;

	hsspi->host->flags = STC_HOST_PRD;
	hsspi->host->ul = 0;
	hsspi->host->length = 0;

	ret = spi_xfer(hsspi, NULL, NULL, 0);
	if (ret)
		return ret;

	/* Ignore pre-read check flags */
	check_soc_flag(&hsspi->spi->dev, __func__, hsspi->soc->flags, true);

	if (hsspi->soc->flags & STC_SOC_ODW)
		return hsspi_rx(hsspi, hsspi->soc->ul, hsspi->soc->length);
	else
		/* Pre-read error. Maybe FW is a little late to setup HSSPI header. */
		return -1;
}

/**
 * hsspi_thread_fn() - the thread that manage all SPI transfers
 * @data: the &struct hsspi
 *
 */
static int hsspi_thread_fn(void *data)
{
	struct hsspi *hsspi = data;
	static int successive_errors;
	int reset_cnt = 0;

	successive_errors = 0;
	while (1) {
		struct hsspi_work *hw;
		int ret;

		ret = wait_event_interruptible(hsspi->wq,
					       is_txrx_waiting(hsspi) ||
						       kthread_should_stop());
		if (ret)
			return ret;

		if (kthread_should_stop())
			break;

		hw = get_work(hsspi);
		if (hw) {
			if (hw->type == HSSPI_WORK_COMPLETION) {
				complete(hw->completion);
				/* on the stack no need to free */
				continue;
			} else if (hw->type == HSSPI_WORK_TX) {
				ret = hsspi_tx(hsspi, hw->tx.layer, hw->tx.blk);
				kfree(hw);
			} else {
				dev_err(&hsspi->spi->dev,
					"unknown hsspi_work type: %d\n",
					hw->type);
				continue;
			}
		} else
			/* If there is no work, we are here because
			 * SS_IRQ is set.
			 */
			ret = hsspi_pre_read(hsspi);

		if (ret) {
			successive_errors++;

			if (successive_errors > MAX_SUCCESSIVE_ERRORS) {
				dev_err(&hsspi->spi->dev,
					"Max successive errors %d reached, likely entered ROM code...\n",
					successive_errors);

				/* When the device reboots, the ROM code might raise
				 * ss_ready; if a SPI transfer is requested, the AP
				 * will initiate the SPI xfer and the ROM code will
				 * enter its command mode infinite loop...
				 * No choice but rebooting the device.
				 */
				hsspi->reset_qm35(hsspi);
				successive_errors = 0;
				reset_cnt++;
				if (reset_cnt >= CHIP_RESET_RETRY) {
					spin_lock(&hsspi->lock);
					hsspi->state = HSSPI_STOPPED;
					spin_unlock(&hsspi->lock);
					reset_cnt = 0;
					dev_err(&hsspi->spi->dev,
						"No response from FW after multiple resets. Stopping HSSPI.\n");
				}
			}
		} else {
			successive_errors = 0;
			reset_cnt = 0;
		}
	}
	return 0;
}

int hsspi_init(struct hsspi *hsspi, struct spi_device *spi)
{
	memset(hsspi, 0, sizeof(*hsspi));

	spin_lock_init(&hsspi->lock);
	spin_lock_init(&hsspi->flags_lock);
	INIT_LIST_HEAD(&hsspi->work_list);

	hsspi->state = HSSPI_STOPPED;
	hsspi->spi = spi;

	init_waitqueue_head(&hsspi->wq);
	init_waitqueue_head(&hsspi->wq_ready);

	hsspi->host = kmalloc(sizeof(*(hsspi->host)), GFP_KERNEL | GFP_DMA);
	hsspi->soc = kmalloc(sizeof(*(hsspi->soc)), GFP_KERNEL | GFP_DMA);

	hsspi->thread = kthread_create(hsspi_thread_fn, hsspi, "hsspi");
	if (IS_ERR(hsspi->thread))
		return PTR_ERR(hsspi->thread);

	wake_up_process(hsspi->thread);

	dev_info(&hsspi->spi->dev, "HSSPI initialized\n");
	return 0;
}

void hsspi_set_gpios(struct hsspi *hsspi, struct gpio_desc *gpio_ss_rdy,
		     struct gpio_desc *gpio_exton)
{
	hsspi->gpio_ss_rdy = gpio_ss_rdy;
	hsspi->gpio_exton = gpio_exton;
}

int hsspi_deinit(struct hsspi *hsspi)
{
	int i;

	spin_lock(&hsspi->lock);

	if (hsspi->state == HSSPI_RUNNING) {
		spin_unlock(&hsspi->lock);
		return -EBUSY;
	}

	for (i = 0; i < ARRAY_SIZE(hsspi->layers); i++) {
		if (!hsspi->layers[i])
			continue;

		dev_err(&hsspi->spi->dev,
			"HSSPI upper layer '%s' not unregistered\n",
			hsspi->layers[i]->name);
		spin_unlock(&hsspi->lock);
		return -EBUSY;
	}

	spin_unlock(&hsspi->lock);

	kthread_stop(hsspi->thread);

	kfree(hsspi->host);
	kfree(hsspi->soc);

	dev_info(&hsspi->spi->dev, "HSSPI uninitialized\n");
	return 0;
}

int hsspi_register(struct hsspi *hsspi, struct hsspi_layer *layer)
{
	int ret = 0;

	if (!layer_id_is_valid(hsspi, layer->id))
		return -EINVAL;

	spin_lock(&hsspi->lock);

	if (hsspi->layers[layer->id])
		ret = -EBUSY;
	else
		hsspi->layers[layer->id] = layer;

	spin_unlock(&hsspi->lock);

	if (ret) {
		dev_err(&hsspi->spi->dev, "%s: '%s' ret: %d\n", __func__,
			layer->name, ret);
		return ret;
	}

	dev_dbg(&hsspi->spi->dev, "HSSPI upper layer '%s' registered\n",
		layer->name);
	return 0;
}

int hsspi_unregister(struct hsspi *hsspi, struct hsspi_layer *layer)
{
	DECLARE_COMPLETION_ONSTACK(complete);
	struct hsspi_work complete_work = {
		.type = HSSPI_WORK_COMPLETION,
		.completion = &complete,
	};
	int ret = 0;

	if (!layer_id_is_valid(hsspi, layer->id))
		return -EINVAL;

	spin_lock(&hsspi->lock);

	if (hsspi->layers[layer->id] == layer) {
		hsspi->layers[layer->id] = NULL;

		list_add_tail(&complete_work.list, &hsspi->work_list);
	} else
		ret = -EINVAL;

	spin_unlock(&hsspi->lock);

	if (ret) {
		dev_err(&hsspi->spi->dev, "%s: '%s' ret: %d\n", __func__,
			layer->name, ret);
		return ret;
	}

	wake_up_interruptible(&hsspi->wq);

	/* when completed there is no more reference to layer in the
	 * work_list or in the hsspi_thread_fn
	 */
	wait_for_completion(&complete);

	dev_dbg(&hsspi->spi->dev, "HSSPI upper layer '%s' unregistered\n",
		layer->name);
	return 0;
}

void hsspi_set_spi_slave_busy(struct hsspi *hsspi)
{
	set_bit(HSSPI_FLAGS_SS_BUSY, hsspi->flags);
}

void hsspi_set_spi_slave_ready(struct hsspi *hsspi)
{
	unsigned long flags;
	spin_lock_irqsave(&hsspi->flags_lock, flags);
	clear_bit(HSSPI_FLAGS_SS_BUSY, hsspi->flags);
	set_bit(HSSPI_FLAGS_SS_READY, hsspi->flags);
	spin_unlock_irqrestore(&hsspi->flags_lock, flags);

	wake_up_interruptible(&hsspi->wq_ready);
}

void hsspi_set_spi_slave_ready_irq(struct hsspi *hsspi)
{
	spin_lock(&hsspi->flags_lock);
	clear_bit(HSSPI_FLAGS_SS_BUSY, hsspi->flags);
	set_bit(HSSPI_FLAGS_SS_READY, hsspi->flags);
	spin_unlock(&hsspi->flags_lock);

	wake_up_interruptible(&hsspi->wq_ready);
}

void hsspi_clear_spi_slave_ready(struct hsspi *hsspi)
{
	clear_bit(HSSPI_FLAGS_SS_READY, hsspi->flags);
}

void hsspi_set_output_data_waiting(struct hsspi *hsspi)
{
	set_bit(HSSPI_FLAGS_SS_IRQ, hsspi->flags);

	wake_up_interruptible(&hsspi->wq);
}

int hsspi_init_block(struct hsspi_block *blk, u16 length)
{
	void *data;

	data = krealloc(blk->data, length, GFP_KERNEL | GFP_DMA);
	if (!data)
		return -ENOMEM;

	blk->data = data;
	blk->length = length;
	blk->size = length;

	return 0;
}

void hsspi_deinit_block(struct hsspi_block *blk)
{
	kfree(blk->data);
	blk->data = NULL;
}

int hsspi_send(struct hsspi *hsspi, struct hsspi_layer *layer,
	       struct hsspi_block *blk)
{
	struct hsspi_work *tx_work;
	int ret = 0;

	if (!layer || !blk)
		return -EINVAL;

	if (!layer_id_is_valid(hsspi, layer->id))
		return -EINVAL;

	tx_work = kzalloc(sizeof(*tx_work), GFP_KERNEL);
	if (!tx_work)
		return -ENOMEM;

	tx_work->type = HSSPI_WORK_TX;
	tx_work->tx.blk = blk;
	tx_work->tx.layer = layer;

	spin_lock(&hsspi->lock);

	if (hsspi->state == HSSPI_RUNNING) {
		if (hsspi->layers[layer->id] == layer)
			list_add_tail(&tx_work->list, &hsspi->work_list);
		else
			ret = -EINVAL;
	} else
		ret = -EAGAIN;

	spin_unlock(&hsspi->lock);

	if (ret) {
		kfree(tx_work);
		dev_err(&hsspi->spi->dev, "%s: %d\n", __func__, ret);
		return ret;
	}

	wake_up_interruptible(&hsspi->wq);

	dev_dbg(&hsspi->spi->dev, "send %d bytes on HSSPI '%s' layer\n",
		blk->length, layer->name);
	return 0;
}

void hsspi_start(struct hsspi *hsspi)
{
	spin_lock(&hsspi->lock);

	hsspi->state = HSSPI_RUNNING;

	spin_unlock(&hsspi->lock);

	wake_up_interruptible(&hsspi->wq);

	dev_dbg(&hsspi->spi->dev, "HSSPI started\n");
}

void hsspi_stop(struct hsspi *hsspi)
{
	DECLARE_COMPLETION_ONSTACK(complete);
	struct hsspi_work complete_work = {
		.type = HSSPI_WORK_COMPLETION,
		.completion = &complete,
	};

	spin_lock(&hsspi->lock);

	hsspi->state = HSSPI_STOPPED;

	list_add_tail(&complete_work.list, &hsspi->work_list);

	spin_unlock(&hsspi->lock);

	wake_up_interruptible(&hsspi->wq);

	wait_for_completion(&complete);

	dev_dbg(&hsspi->spi->dev, "HSSPI stopped\n");
}
