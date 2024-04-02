// SPDX-License-Identifier: GPL-2.0
/*
 * google_bcl_core.c Google bcl core driver
 *
 * Copyright (c) 2022, Google LLC. All rights reserved.
 *
 */

#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/thermal.h>
#include <dt-bindings/interrupt-controller/zuma.h>
#include <linux/regulator/pmic_class.h>
#include <soc/google/odpm.h>
#include <soc/google/exynos-cpupm.h>
#include <soc/google/exynos-pm.h>
#include <soc/google/exynos-pmu-if.h>
#include <soc/google/bcl.h>
#if IS_ENABLED(CONFIG_DEBUG_FS)
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif

static const struct platform_device_id google_id_table[] = {
	{.name = "google_mitigation",},
	{},
};

static const unsigned int xclkout_source[] = {
	XCLKOUT_SOURCE_CPU0,
	XCLKOUT_SOURCE_CPU1,
	XCLKOUT_SOURCE_CPU2,
	XCLKOUT_SOURCE_TPU,
	XCLKOUT_SOURCE_GPU
};

static void update_irq_end_times(struct bcl_device *bcl_dev, int id);

static int zone_read_temp(void *data, int *val)
{
	struct bcl_zone *zone = data;

	*val = zone->bcl_cur_lvl;
	zone->bcl_prev_lvl = *val;
	return 0;
}

static enum BCL_BATT_IRQ id_to_ind(int id)
{
	switch (id) {
	case UVLO1:
		return UVLO1_IRQ_BIN;
	case UVLO2:
		return UVLO2_IRQ_BIN;
	case BATOILO:
		return BATOILO_IRQ_BIN;
	}
	return MAX_BCL_BATT_IRQ;
}

static void bin_incr_ifpmic(struct bcl_device *bcl_dev, enum BCL_BATT_IRQ batt,
				enum CONCURRENT_PWRWARN_IRQ pwrwarn, ktime_t end_time)
{
	ktime_t time_delta;
	if (bcl_dev->ifpmic_irq_bins[batt][pwrwarn].start_time == 0)
		return;

	time_delta = ktime_sub(end_time, bcl_dev->ifpmic_irq_bins[batt][pwrwarn].start_time);
	if (ktime_compare(time_delta, DELTA_10MS) < 0)
		atomic_inc(&bcl_dev->ifpmic_irq_bins[batt][pwrwarn].lt_5ms_count);
	else if (ktime_compare(time_delta, DELTA_50MS) < 0)
		atomic_inc(&bcl_dev->ifpmic_irq_bins[batt][pwrwarn].bt_5ms_10ms_count);
	else
		atomic_inc(&bcl_dev->ifpmic_irq_bins[batt][pwrwarn].gt_10ms_count);

	bcl_dev->ifpmic_irq_bins[batt][pwrwarn].start_time = 0;
}

/*
 * Track UVLO1/UVLO2/BATOILO IRQ starting times, and any PWRWARN events
 * happening at the same time as the UVLO1/UVLO2/BATOILO IRQ.
 */
static void update_irq_start_times(struct bcl_device *bcl_dev, int id)
{
	/* Check if it is a input IRQ */
	ktime_t start_time = ktime_get();
	enum BCL_BATT_IRQ irq_ind = id_to_ind(id);
	if (bcl_dev->ifpmic_irq_bins[irq_ind][NONE_BCL_BIN].start_time != 0)
		update_irq_end_times(bcl_dev, id);
	if (irq_ind == MAX_BCL_BATT_IRQ)
		return;

	bcl_dev->ifpmic_irq_bins[irq_ind][NONE_BCL_BIN].start_time = start_time;
	if (bcl_dev->sub_pwr_warn_triggered[bcl_dev->rffe_channel])
		bcl_dev->ifpmic_irq_bins[irq_ind][MMWAVE_BCL_BIN].start_time = start_time;
	if (bcl_dev->main_pwr_warn_triggered[bcl_dev->rffe_channel])
		bcl_dev->ifpmic_irq_bins[irq_ind][RFFE_BCL_BIN].start_time = start_time;
}

static void update_irq_end_times(struct bcl_device *bcl_dev, int id)
{
	ktime_t end_time;
	int irq_ind = -1;
	int i;
	bool pwrwarn_irq_triggered;

	end_time = ktime_get();
	irq_ind = id_to_ind(id);
	if (irq_ind == MAX_BCL_BATT_IRQ)
		return;

	for (i = 0; i < MAX_CONCURRENT_PWRWARN_IRQ; i++) {
		switch (i) {
		case NONE_BCL_BIN:
			pwrwarn_irq_triggered = true;
			break;
		case MMWAVE_BCL_BIN:
			pwrwarn_irq_triggered =
			    bcl_dev->sub_pwr_warn_triggered[bcl_dev->rffe_channel];
			break;
		case RFFE_BCL_BIN:
			pwrwarn_irq_triggered =
			    bcl_dev->main_pwr_warn_triggered[bcl_dev->rffe_channel];
			break;
		}
		if (pwrwarn_irq_triggered)
			bin_incr_ifpmic(bcl_dev, irq_ind, i, end_time);
	}
}

static void pwrwarn_update_start_time(struct bcl_device *bcl_dev,
					int id, struct irq_duration_stats *bins,
					bool *pwr_warn_triggered,
					enum CONCURRENT_PWRWARN_IRQ bin_ind)
{
	ktime_t start_time;
	bool is_rf = bcl_dev->rffe_channel == id;

	if (bins[id].start_time != 0)
		return;

	start_time = ktime_get();
	if (is_rf && pwr_warn_triggered[id]) {
		if (bcl_dev->ifpmic_irq_bins[UVLO1_IRQ_BIN][NONE_BCL_BIN].start_time != 0)
			bcl_dev->ifpmic_irq_bins[UVLO1_IRQ_BIN][bin_ind].start_time =
				start_time;
		if (bcl_dev->ifpmic_irq_bins[UVLO2_IRQ_BIN][NONE_BCL_BIN].start_time != 0)
			bcl_dev->ifpmic_irq_bins[UVLO2_IRQ_BIN][bin_ind].start_time =
				start_time;
		if (bcl_dev->ifpmic_irq_bins[BATOILO_IRQ_BIN][NONE_BCL_BIN].start_time != 0)
			bcl_dev->ifpmic_irq_bins[BATOILO_IRQ_BIN][bin_ind].start_time =
				start_time;
	}
	bins[id].start_time = start_time;
}

static void pwrwarn_update_end_time(struct bcl_device *bcl_dev, int id,
                                    struct irq_duration_stats *bins,
                                    enum CONCURRENT_PWRWARN_IRQ bin_ind)
{
	ktime_t end_time;
	ktime_t time_delta;
	int i;
	bool is_rf = bcl_dev->rffe_channel == id;

	end_time = ktime_get();
	if (is_rf) {
		for (i = 0; i < MAX_BCL_BATT_IRQ; i++)
			if (bcl_dev->ifpmic_irq_bins[i][bin_ind].start_time != 0)
				bin_incr_ifpmic(bcl_dev, i, bin_ind, end_time);
	}

	if (bins[id].start_time == 0)
		return;

	time_delta = ktime_sub(end_time, bins[id].start_time);
	if (ktime_compare(time_delta, DELTA_10MS) < 0)
		atomic_inc(&(bins[id].lt_5ms_count));
	else if (ktime_compare(time_delta, DELTA_50MS) < 0)
		atomic_inc(&(bins[id].bt_5ms_10ms_count));
	else
		atomic_inc(&(bins[id].gt_10ms_count));
	bins[id].start_time = 0;
}

static struct power_supply *google_get_power_supply(struct bcl_device *bcl_dev)
{
	static struct power_supply *psy[2];
	static struct power_supply *batt_psy;
	int err = 0;

	batt_psy = NULL;
	err = power_supply_get_by_phandle_array(bcl_dev->device->of_node, "google,power-supply",
						psy, ARRAY_SIZE(psy));
	if (err > 0)
		batt_psy = psy[0];
	return batt_psy;
}

static void ocpsmpl_read_stats(struct bcl_device *bcl_dev,
			       struct ocpsmpl_stats *dst, struct power_supply *psy)
{
	union power_supply_propval ret = {0};
	int err = 0;

	if (!psy)
		return;
	dst->_time = ktime_to_ms(ktime_get());
	err = power_supply_get_property(psy, POWER_SUPPLY_PROP_CAPACITY, &ret);
	if (err < 0)
		dst->capacity = -1;
	else {
		dst->capacity = ret.intval;
		bcl_dev->batt_psy_initialized = true;
	}
	err = power_supply_get_property(psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &ret);
	if (err < 0)
		dst->voltage = -1;
	else {
		dst->voltage = ret.intval;
		bcl_dev->batt_psy_initialized = true;
	}

}

static void update_tz(struct bcl_zone *zone, int idx, bool triggered)
{
	if (triggered)
		zone->bcl_cur_lvl = zone->bcl_lvl + THERMAL_HYST_LEVEL;
	else
		zone->bcl_cur_lvl = 0;
	if (zone->tz && (zone->bcl_prev_lvl != zone->bcl_cur_lvl))
		thermal_zone_device_update(zone->tz, THERMAL_EVENT_UNSPECIFIED);
}

static irqreturn_t irq_handler(int irq, void *data)
{
	struct bcl_zone *zone = data;
	struct bcl_device *bcl_dev;
	u8 idx;
	int gpio_level;
	u8 irq_val = 0;

	if (!zone || !zone->parent)
		return IRQ_HANDLED;

	idx = zone->idx;
	bcl_dev = zone->parent;
	if (!bcl_dev->ready)
		return IRQ_HANDLED;

	gpio_level = gpio_get_value(zone->bcl_pin);

	if (idx >= UVLO2 && idx <= BATOILO) {
		bcl_cb_get_irq(bcl_dev, &irq_val);
		if (irq_val == 0)
			goto exit;
		idx = irq_val;
		zone = bcl_dev->zone[idx];
	}
	if (gpio_level == zone->polarity)
		mod_delayed_work(system_highpri_wq, &zone->irq_triggered_work, 0);
	else
		mod_delayed_work(system_highpri_wq, &zone->irq_untriggered_work, 0);
exit:
	return IRQ_HANDLED;
}

static void google_warn_work(struct work_struct *work)
{
	struct bcl_zone *zone = container_of(work, struct bcl_zone, irq_work.work);
	struct bcl_device *bcl_dev;
	int gpio_level;
	int idx;

	idx = zone->idx;
	bcl_dev = zone->parent;

	if (zone->irq_type == IF_PMIC && idx != BATOILO) {
		zone->disabled = true;
		disable_irq(zone->bcl_irq);
	}
	gpio_level = gpio_get_value(zone->bcl_pin);
	if (gpio_level != zone->polarity) {
		zone->bcl_cur_lvl = 0;
		if (zone->bcl_qos)
			google_bcl_qos_update(zone, false);
		if (zone->irq_type == IF_PMIC) {
			bcl_cb_clr_irq(bcl_dev);
			update_irq_end_times(bcl_dev, idx);
		}
	} else {
		zone->bcl_cur_lvl = zone->bcl_lvl + THERMAL_HYST_LEVEL;
		mod_delayed_work(system_unbound_wq, &zone->irq_work,
				 msecs_to_jiffies(THRESHOLD_DELAY_MS));
	}
	if (zone->tz)
		thermal_zone_device_update(zone->tz, THERMAL_EVENT_UNSPECIFIED);
	if (zone->irq_type == IF_PMIC && idx != BATOILO) {
		zone->disabled = false;
		enable_irq(zone->bcl_irq);
	}
	if (zone->irq_type != IF_PMIC && bcl_dev->irq_delay != 0) {
		if (!zone->disabled) {
			zone->disabled = true;
			disable_irq(zone->bcl_irq);
			mod_delayed_work(system_unbound_wq, &zone->enable_irq_work,
					 msecs_to_jiffies(bcl_dev->irq_delay));
		}
	}
}

static int google_bcl_set_soc(void *data, int low, int high)
{
	struct bcl_device *bcl_dev = data;

	if (high == bcl_dev->trip_high_temp)
		return 0;

	mutex_lock(&bcl_dev->state_trans_lock);
	bcl_dev->trip_low_temp = low;
	bcl_dev->trip_high_temp = high;
	schedule_delayed_work(&bcl_dev->soc_work, 0);

	mutex_unlock(&bcl_dev->state_trans_lock);
	return 0;
}

static int google_bcl_read_soc(void *data, int *val)
{
	struct bcl_device *bcl_dev = data;
	union power_supply_propval ret = {
		0,
	};
	int err = 0;

	*val = 100;
	if (!bcl_dev->batt_psy)
		bcl_dev->batt_psy = google_get_power_supply(bcl_dev);
	if (bcl_dev->batt_psy) {
		err = power_supply_get_property(bcl_dev->batt_psy,
						POWER_SUPPLY_PROP_CAPACITY, &ret);
		if (err < 0) {
			dev_err(bcl_dev->device, "battery percentage read error:%d\n", err);
			return err;
		}
		bcl_dev->batt_psy_initialized = true;
		*val = 100 - ret.intval;
	}
	pr_debug("soc:%d\n", *val);

	return err;
}

static void google_bcl_evaluate_soc(struct work_struct *work)
{
	int battery_percentage_reverse;
	struct bcl_device *bcl_dev = container_of(work, struct bcl_device,
						  soc_work.work);

	if (google_bcl_read_soc(bcl_dev, &battery_percentage_reverse))
		return;

	mutex_lock(&bcl_dev->state_trans_lock);
	if ((battery_percentage_reverse < bcl_dev->trip_high_temp) &&
		(battery_percentage_reverse > bcl_dev->trip_low_temp))
		goto eval_exit;

	bcl_dev->trip_val = battery_percentage_reverse;
	mutex_unlock(&bcl_dev->state_trans_lock);
	if (!bcl_dev->soc_tz) {
		bcl_dev->soc_tz =
				thermal_zone_of_sensor_register(bcl_dev->device,
								PMIC_SOC, bcl_dev,
								&bcl_dev->soc_tz_ops);
		if (IS_ERR(bcl_dev->soc_tz)) {
			dev_err(bcl_dev->device, "soc TZ register failed. err:%ld\n",
				PTR_ERR(bcl_dev->soc_tz));
			return;
		}
	}
	if (!IS_ERR(bcl_dev->soc_tz))
		thermal_zone_device_update(bcl_dev->soc_tz, THERMAL_EVENT_UNSPECIFIED);
	return;
eval_exit:
	mutex_unlock(&bcl_dev->state_trans_lock);
}

static int battery_supply_callback(struct notifier_block *nb,
				   unsigned long event, void *data)
{
	struct power_supply *psy = data;
	struct bcl_device *bcl_dev = container_of(nb, struct bcl_device, psy_nb);
	struct power_supply *bcl_psy;

	if (!bcl_dev)
		return NOTIFY_OK;

	bcl_psy = bcl_dev->batt_psy;

	if (!bcl_psy || event != PSY_EVENT_PROP_CHANGED)
		return NOTIFY_OK;

	if (!strcmp(psy->desc->name, bcl_psy->desc->name))
		schedule_delayed_work(&bcl_dev->soc_work, 0);

	return NOTIFY_OK;
}

static int google_bcl_remove_thermal(struct bcl_device *bcl_dev)
{
	int i = 0;
	struct device *dev;

	power_supply_unreg_notifier(&bcl_dev->psy_nb);
	dev = bcl_dev->main_dev;
	for (i = 0; i < TRIGGERED_SOURCE_MAX; i++) {
		if (i > SOFT_OCP_WARN_TPU)
			dev = bcl_dev->sub_dev;
		if (!bcl_dev->zone[i])
			continue;
		if (bcl_dev->zone[i]->tz)
			thermal_zone_of_sensor_unregister(dev, bcl_dev->zone[i]->tz);
		cancel_delayed_work(&bcl_dev->zone[i]->irq_work);
		cancel_delayed_work(&bcl_dev->zone[i]->irq_triggered_work);
		cancel_delayed_work(&bcl_dev->zone[i]->irq_untriggered_work);
		cancel_delayed_work(&bcl_dev->zone[i]->enable_irq_work);
	}

	return 0;
}

static int google_bcl_init_clk_div(struct bcl_device *bcl_dev, int idx,
				   unsigned int value)
{
	void __iomem *addr;

	if (!bcl_dev)
		return -EIO;
	switch (idx) {
	case SUBSYSTEM_TPU:
	case SUBSYSTEM_GPU:
	case SUBSYSTEM_AUR:
		return -EIO;
	case SUBSYSTEM_CPU0:
	case SUBSYSTEM_CPU1:
	case SUBSYSTEM_CPU2:
		addr = bcl_dev->core_conf[idx].base_mem + CLKDIVSTEP;
		break;
	}
	mutex_lock(&bcl_dev->ratio_lock);
	__raw_writel(value, addr);
	mutex_unlock(&bcl_dev->ratio_lock);

	return 0;
}

int google_bcl_register_ifpmic(struct bcl_device *bcl_dev,
			       const struct bcl_ifpmic_ops *pmic_ops)
{
	if (!bcl_dev)
		return -EIO;

	if (!pmic_ops || !pmic_ops->cb_get_vdroop_ok ||
	    !pmic_ops->cb_uvlo_read || !pmic_ops->cb_uvlo_write ||
	    !pmic_ops->cb_batoilo_read || !pmic_ops->cb_batoilo_write ||
	    !pmic_ops->cb_get_irq || !pmic_ops->cb_clr_irq)
		return -EINVAL;

	bcl_dev->pmic_ops = pmic_ops;

	return 0;
}
EXPORT_SYMBOL_GPL(google_bcl_register_ifpmic);

struct bcl_device *google_retrieve_bcl_handle(void)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct bcl_device *bcl_dev;

	np = of_find_node_by_name(NULL, "google,mitigation");
	if (!np)
		return NULL;
	pdev = of_find_device_by_node(np);
	if (!pdev)
		return NULL;
	bcl_dev = platform_get_drvdata(pdev);
	if (!bcl_dev)
		return NULL;

	return bcl_dev;
}
EXPORT_SYMBOL_GPL(google_retrieve_bcl_handle);

static int google_init_ratio(struct bcl_device *data, enum SUBSYSTEM_SOURCE idx)
{
	void __iomem *addr;

	if (!data)
		return -ENOMEM;

	if (!bcl_is_subsystem_on(subsystem_pmu[idx]))
		return -EIO;

	if (idx < SUBSYSTEM_TPU)
		return -EIO;

	mutex_lock(&data->ratio_lock);
	if (idx != SUBSYSTEM_AUR) {
		addr = data->core_conf[idx].base_mem + CLKDIVSTEP_CON_HEAVY;
		__raw_writel(data->core_conf[idx].con_heavy, addr);
		addr = data->core_conf[idx].base_mem + CLKDIVSTEP_CON_LIGHT;
		__raw_writel(data->core_conf[idx].con_light, addr);
		addr = data->core_conf[idx].base_mem + VDROOP_FLT;
		__raw_writel(data->core_conf[idx].vdroop_flt, addr);
	}
	addr = data->core_conf[idx].base_mem + CLKDIVSTEP;
	__raw_writel(data->core_conf[idx].clkdivstep, addr);
	addr = data->core_conf[idx].base_mem + CLKOUT;
	__raw_writel(data->core_conf[idx].clk_out, addr);
	data->core_conf[idx].clk_stats = __raw_readl(data->core_conf[idx].base_mem +
						     clk_stats_offset[idx]);
	mutex_unlock(&data->ratio_lock);

	return 0;
}

int google_init_tpu_ratio(struct bcl_device *data)
{
	return google_init_ratio(data, SUBSYSTEM_TPU);
}
EXPORT_SYMBOL_GPL(google_init_tpu_ratio);

int google_init_gpu_ratio(struct bcl_device *data)
{
	return google_init_ratio(data, SUBSYSTEM_GPU);
}
EXPORT_SYMBOL_GPL(google_init_gpu_ratio);

int google_init_aur_ratio(struct bcl_device *data)
{
	return google_init_ratio(data, SUBSYSTEM_AUR);
}
EXPORT_SYMBOL_GPL(google_init_aur_ratio);

unsigned int google_get_db(struct bcl_device *data, enum MPMM_SOURCE index)
{
	void __iomem *addr;
	unsigned int reg;

	if (!data)
		return -ENOMEM;
	if (!data->sysreg_cpucl0) {
		dev_err(data->device, "Error in sysreg_cpucl0\n");
		return -ENOMEM;
	}

	if (index == MID)
		addr = data->sysreg_cpucl0 + CLUSTER0_MID_DISPBLOCK;
	else if (index == BIG)
		addr = data->sysreg_cpucl0 + CLUSTER0_BIG_DISPBLOCK;
	else
		return -EINVAL;

	mutex_lock(&data->sysreg_lock);
	reg = __raw_readl(addr);
	mutex_unlock(&data->sysreg_lock);

	return reg;
}
EXPORT_SYMBOL_GPL(google_get_db);

int google_set_db(struct bcl_device *data, unsigned int value, enum MPMM_SOURCE index)
{
	void __iomem *addr;

	if (!data)
		return -ENOMEM;
	if (!data->sysreg_cpucl0) {
		dev_err(data->device, "Error in sysreg_cpucl0\n");
		return -ENOMEM;
	}

	if (index == MID)
		addr = data->sysreg_cpucl0 + CLUSTER0_MID_DISPBLOCK;
	else if (index == BIG)
		addr = data->sysreg_cpucl0 + CLUSTER0_BIG_DISPBLOCK;
	else
		return -EINVAL;

	mutex_lock(&data->sysreg_lock);
	__raw_writel(value, addr);
	mutex_unlock(&data->sysreg_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(google_set_db);

static void google_enable_irq_work(struct work_struct *work)
{
	struct bcl_zone *zone = container_of(work, struct bcl_zone, enable_irq_work.work);

	if (!zone)
		return;

	zone->disabled = false;
	enable_irq(zone->bcl_irq);
}

static void google_irq_triggered_work(struct work_struct *work)
{
	struct bcl_zone *zone = container_of(work, struct bcl_zone, irq_triggered_work.work);
	struct bcl_device *bcl_dev;
	int idx;

	if (zone->bcl_qos)
		google_bcl_qos_update(zone, true);

	idx = zone->idx;
	bcl_dev = zone->parent;

	if (zone->irq_type == IF_PMIC)
		update_irq_start_times(bcl_dev, idx);

	if (idx == BATOILO)
		gpio_set_value(bcl_dev->modem_gpio2_pin, 1);

	if (bcl_dev->batt_psy_initialized) {
		atomic_inc(&zone->bcl_cnt);
		ocpsmpl_read_stats(bcl_dev, &zone->bcl_stats, bcl_dev->batt_psy);
		update_tz(zone, idx, true);
	}
	mod_delayed_work(system_unbound_wq, &zone->irq_work, msecs_to_jiffies(THRESHOLD_DELAY_MS));
	if (zone->irq_type == IF_PMIC)
		bcl_cb_clr_irq(bcl_dev);
}

static void google_irq_untriggered_work(struct work_struct *work)
{
	struct bcl_zone *zone = container_of(work, struct bcl_zone, irq_untriggered_work.work);
	struct bcl_device *bcl_dev;
	int idx;

	if (!zone || !zone->parent)
		return;

	idx = zone->idx;
	bcl_dev = zone->parent;

	/* IRQ falling edge */
	if (zone->irq_type == IF_PMIC)
		bcl_cb_clr_irq(bcl_dev);
	if (zone->bcl_qos)
		google_bcl_qos_update(zone, false);
	if (zone->irq_type == IF_PMIC)
		update_irq_end_times(bcl_dev, idx);
	if (idx == BATOILO)
		gpio_set_value(bcl_dev->modem_gpio2_pin, 0);

	update_tz(zone, idx, false);
}

static int google_bcl_register_zone(struct bcl_device *bcl_dev, int idx, const char *devname,
				    u32 intr_flag, int pin, int lvl, int irq, int type)
{
	int ret = 0;
	struct bcl_zone *zone;

	if (!bcl_dev)
		return -ENOMEM;

	zone = devm_kzalloc(bcl_dev->device, sizeof(struct bcl_zone), GFP_KERNEL);

	if (!zone)
		return -ENOMEM;

	zone->idx = idx;
	zone->bcl_pin = pin;
	zone->bcl_irq = irq;
	zone->bcl_cur_lvl = 0;
	zone->bcl_prev_lvl = 0;
	zone->bcl_lvl = lvl;
	zone->parent = bcl_dev;
	zone->irq_type = type;
	atomic_set(&zone->bcl_cnt, 0);
	if (idx == SMPL_WARN) {
		irq_set_status_flags(zone->bcl_irq, IRQ_DISABLE_UNLAZY);
		zone->polarity = 0;
	} else
		zone->polarity = 1;
	if (idx != BATOILO) {
		ret = devm_request_threaded_irq(bcl_dev->device, zone->bcl_irq, NULL, irq_handler,
						intr_flag | IRQF_ONESHOT, devname, zone);

		if (ret < 0) {
			dev_err(zone->device, "Failed to request IRQ: %d: %d\n", irq, ret);
			devm_kfree(bcl_dev->device, zone);
			return ret;
		}
		zone->disabled = true;
		disable_irq(zone->bcl_irq);
	}
	INIT_DELAYED_WORK(&zone->irq_work, google_warn_work);
	INIT_DELAYED_WORK(&zone->irq_triggered_work, google_irq_triggered_work);
	INIT_DELAYED_WORK(&zone->irq_untriggered_work, google_irq_untriggered_work);
	INIT_DELAYED_WORK(&zone->enable_irq_work, google_enable_irq_work);
	zone->tz_ops.get_temp = zone_read_temp;
	zone->tz = thermal_zone_of_sensor_register(bcl_dev->device, idx, zone, &zone->tz_ops);
	if (IS_ERR(zone->tz))
		dev_err(zone->device, "TZ register failed. %d, err:%ld\n", idx, PTR_ERR(zone->tz));
	else {
		thermal_zone_device_enable(zone->tz);
		thermal_zone_device_update(zone->tz, THERMAL_DEVICE_UP);
	}
	bcl_dev->zone[idx] = zone;
	return ret;
}

static void main_pwrwarn_irq_work(struct work_struct *work)
{
	struct bcl_device *bcl_dev = container_of(work, struct bcl_device,
						  main_pwr_irq_work.work);
	bool revisit_needed = false;
	int i;
	u32 micro_unit[ODPM_CHANNEL_MAX];
	u32 measurement;

	mutex_lock(&bcl_dev->main_odpm->lock);

	odpm_get_raw_lpf_values(bcl_dev->main_odpm, S2MPG1415_METER_CURRENT, micro_unit);
	for (i = 0; i < METER_CHANNEL_MAX; i++) {
		measurement = micro_unit[i] >> LPF_CURRENT_SHIFT;
		bcl_dev->main_pwr_warn_triggered[i] = (measurement > bcl_dev->main_setting[i]);
		if (!revisit_needed)
			revisit_needed = bcl_dev->main_pwr_warn_triggered[i];
		if ((!revisit_needed) && (i == bcl_dev->rffe_channel))
			gpio_set_value(bcl_dev->modem_gpio1_pin, 0);
		if (!bcl_dev->main_pwr_warn_triggered[i])
			pwrwarn_update_end_time(bcl_dev, i, bcl_dev->pwrwarn_main_irq_bins,
						RFFE_BCL_BIN);
		else
			pwrwarn_update_start_time(bcl_dev, i, bcl_dev->pwrwarn_main_irq_bins,
							bcl_dev->main_pwr_warn_triggered,
							RFFE_BCL_BIN);
	}

	mutex_unlock(&bcl_dev->main_odpm->lock);

	if (revisit_needed)
		mod_delayed_work(system_unbound_wq, &bcl_dev->main_pwr_irq_work,
				 msecs_to_jiffies(PWRWARN_DELAY_MS));
}

static void sub_pwrwarn_irq_work(struct work_struct *work)
{
	struct bcl_device *bcl_dev = container_of(work, struct bcl_device,
						  sub_pwr_irq_work.work);
	bool revisit_needed = false;
	int i;
	u32 micro_unit[ODPM_CHANNEL_MAX];
	u32 measurement;

	mutex_lock(&bcl_dev->sub_odpm->lock);

	odpm_get_raw_lpf_values(bcl_dev->sub_odpm, S2MPG1415_METER_CURRENT, micro_unit);
	for (i = 0; i < METER_CHANNEL_MAX; i++) {
		measurement = micro_unit[i] >> LPF_CURRENT_SHIFT;
		bcl_dev->sub_pwr_warn_triggered[i] = (measurement > bcl_dev->sub_setting[i]);
		if (!revisit_needed)
			revisit_needed = bcl_dev->sub_pwr_warn_triggered[i];
		if ((!revisit_needed) && (i == bcl_dev->rffe_channel))
			gpio_set_value(bcl_dev->modem_gpio1_pin, 0);
		if (!bcl_dev->sub_pwr_warn_triggered[i])
			pwrwarn_update_end_time(bcl_dev, i, bcl_dev->pwrwarn_sub_irq_bins,
						MMWAVE_BCL_BIN);
		else
			pwrwarn_update_start_time(bcl_dev, i, bcl_dev->pwrwarn_sub_irq_bins,
							bcl_dev->sub_pwr_warn_triggered,
							MMWAVE_BCL_BIN);
	}

	mutex_unlock(&bcl_dev->sub_odpm->lock);

	if (revisit_needed)
		mod_delayed_work(system_unbound_wq, &bcl_dev->sub_pwr_irq_work,
				 msecs_to_jiffies(PWRWARN_DELAY_MS));
}

static irqreturn_t sub_pwr_warn_irq_handler(int irq, void *data)
{
	struct bcl_device *bcl_dev = data;
	int i;

	mutex_lock(&bcl_dev->sub_odpm->lock);

	for (i = 0; i < METER_CHANNEL_MAX; i++) {
		if (bcl_dev->sub_pwr_warn_irq[i] == irq) {
			bcl_dev->sub_pwr_warn_triggered[i] = 1;
			/* Check for Modem MMWAVE */
			if (i == bcl_dev->rffe_channel)
				gpio_set_value(bcl_dev->modem_gpio1_pin, 1);

			/* Setup Timer to clear the triggered */
			mod_delayed_work(system_unbound_wq, &bcl_dev->sub_pwr_irq_work,
					 msecs_to_jiffies(PWRWARN_DELAY_MS));
			pwrwarn_update_start_time(bcl_dev, i, bcl_dev->pwrwarn_sub_irq_bins,
							bcl_dev->sub_pwr_warn_triggered,
							MMWAVE_BCL_BIN);
			break;
		}
	}

	mutex_unlock(&bcl_dev->sub_odpm->lock);

	return IRQ_HANDLED;
}

static irqreturn_t main_pwr_warn_irq_handler(int irq, void *data)
{
	struct bcl_device *bcl_dev = data;
	int i;

	mutex_lock(&bcl_dev->main_odpm->lock);

	for (i = 0; i < METER_CHANNEL_MAX; i++) {
		if (bcl_dev->main_pwr_warn_irq[i] == irq) {
			bcl_dev->main_pwr_warn_triggered[i] = 1;
			/* Check for Modem RFFE */
			if (i == bcl_dev->rffe_channel)
				gpio_set_value(bcl_dev->modem_gpio1_pin, 1);

			/* Setup Timer to clear the triggered */
			mod_delayed_work(system_unbound_wq, &bcl_dev->main_pwr_irq_work,
					 msecs_to_jiffies(PWRWARN_DELAY_MS));
			pwrwarn_update_start_time(bcl_dev, i, bcl_dev->pwrwarn_main_irq_bins,
							bcl_dev->main_pwr_warn_triggered,
							RFFE_BCL_BIN);
			break;
		}
	}

	mutex_unlock(&bcl_dev->main_odpm->lock);

	return IRQ_HANDLED;
}

static int google_set_sub_pmic(struct bcl_device *bcl_dev)
{
	struct s2mpg15_platform_data *pdata_sub;
	struct s2mpg15_dev *sub_dev = NULL;
	struct device_node *p_np;
	struct device_node *np = bcl_dev->device->of_node;
	struct i2c_client *i2c;
	u8 val = 0;
	int ret, i, rail_i;

	INIT_DELAYED_WORK(&bcl_dev->sub_pwr_irq_work, sub_pwrwarn_irq_work);

	p_np = of_parse_phandle(np, "google,sub-power", 0);
	if (p_np) {
		i2c = of_find_i2c_device_by_node(p_np);
		if (!i2c) {
			dev_err(bcl_dev->device, "Cannot find sub-power I2C\n");
			return -ENODEV;
		}
		sub_dev = i2c_get_clientdata(i2c);
	}
	of_node_put(p_np);
	if (!sub_dev) {
		dev_err(bcl_dev->device, "SUB PMIC device not found\n");
		return -ENODEV;
	}
	pdata_sub = dev_get_platdata(sub_dev->dev);
	bcl_dev->sub_odpm = pdata_sub->meter;
	for (i = 0; i < METER_CHANNEL_MAX; i++) {
		rail_i = bcl_dev->sub_odpm->channels[i].rail_i;
		bcl_dev->sub_rail_names[i] = bcl_dev->sub_odpm->chip.rails[rail_i].schematic_name;
	}
	bcl_dev->sub_irq_base = pdata_sub->irq_base;
	bcl_dev->sub_pmic_i2c = sub_dev->pmic;
	bcl_dev->sub_meter_i2c = sub_dev->meter;
	bcl_dev->sub_dev = sub_dev->dev;
	if (pmic_read(CORE_PMIC_SUB, bcl_dev, SUB_CHIPID, &val)) {
		dev_err(bcl_dev->device, "Failed to read PMIC chipid.\n");
		return -ENODEV;
	}
	pmic_read(CORE_PMIC_SUB, bcl_dev, S2MPG15_PM_OFFSRC1, &val);
	dev_info(bcl_dev->device, "SUB OFFSRC1 : %#x\n", val);
	bcl_dev->sub_offsrc1 = val;
	pmic_read(CORE_PMIC_SUB, bcl_dev, S2MPG15_PM_OFFSRC2, &val);
	dev_info(bcl_dev->device, "SUB OFFSRC2 : %#x\n", val);
	bcl_dev->sub_offsrc2 = val;
	pmic_write(CORE_PMIC_SUB, bcl_dev, S2MPG15_PM_OFFSRC1, 0);
	pmic_write(CORE_PMIC_SUB, bcl_dev, S2MPG15_PM_OFFSRC2, 0);

	ret = google_bcl_register_zone(bcl_dev, OCP_WARN_GPU, "GPU_OCP_IRQ",
				      IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				      pdata_sub->b2_ocp_warn_pin,
				      GPU_UPPER_LIMIT - THERMAL_HYST_LEVEL -
				      (pdata_sub->b2_ocp_warn_lvl * GPU_STEP),
				      gpio_to_irq(pdata_sub->b2_ocp_warn_pin),
				      CORE_SUB_PMIC);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: GPU\n");
		return -ENODEV;
	}
	ret = google_bcl_register_zone(bcl_dev, SOFT_OCP_WARN_GPU, "SOFT_GPU_OCP_IRQ",
				      IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				      pdata_sub->b2_soft_ocp_warn_pin,
				      GPU_UPPER_LIMIT - THERMAL_HYST_LEVEL -
				      (pdata_sub->b2_soft_ocp_warn_lvl * GPU_STEP),
				      gpio_to_irq(pdata_sub->b2_soft_ocp_warn_pin),
				      CORE_SUB_PMIC);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: SOFT_GPU\n");
		return -ENODEV;
	}
	for (i = 0; i < S2MPG1415_METER_CHANNEL_MAX; i++) {
		bcl_dev->sub_pwr_warn_irq[i] =
				bcl_dev->sub_irq_base + S2MPG15_IRQ_PWR_WARN_CH0_INT5 + i;
		ret = devm_request_threaded_irq(bcl_dev->device, bcl_dev->sub_pwr_warn_irq[i],
						NULL, sub_pwr_warn_irq_handler, 0,
						bcl_dev->sub_rail_names[i], bcl_dev);
		if (ret < 0) {
			dev_err(bcl_dev->device, "Failed to request PWR_WARN_CH%d IRQ: %d: %d\n",
				i, bcl_dev->sub_pwr_warn_irq[i], ret);
		}
	}

	return 0;
}

static int get_idx_from_tz(struct bcl_device *bcl_dev, const char *name)
{
	int i;
	struct bcl_zone *zone;

	for (i = 0; i < TRIGGERED_SOURCE_MAX; i++) {
		zone = bcl_dev->zone[i];
		if (!zone)
			continue;
		if (!strcmp(name, zone->tz->type))
			return i;
	}
	return -EINVAL;
}

static void google_bcl_parse_qos(struct bcl_device *bcl_dev)
{
	struct device_node *np = bcl_dev->device->of_node;
	struct device_node *child;
	struct device_node *p_np;
	int idx;

	/* parse qos */
	p_np = of_get_child_by_name(np, "freq_qos");
	if (!p_np)
		return;
	for_each_child_of_node(p_np, child) {
		idx = get_idx_from_tz(bcl_dev, child->name);
		if (idx < 0)
			continue;
		bcl_dev->zone[idx]->bcl_qos = devm_kzalloc(bcl_dev->device,
		                                           sizeof(struct qos_throttle_limit),
		                                           GFP_KERNEL);
		bcl_dev->zone[idx]->bcl_qos->throttle = false;
		if (of_property_read_u32(child, "cpucl0",
					 &bcl_dev->zone[idx]->bcl_qos->cpu0_limit) != 0)
			bcl_dev->zone[idx]->bcl_qos->cpu0_limit = INT_MAX;
		if (of_property_read_u32(child, "cpucl1",
					 &bcl_dev->zone[idx]->bcl_qos->cpu1_limit) != 0)
			bcl_dev->zone[idx]->bcl_qos->cpu1_limit = INT_MAX;
		if (of_property_read_u32(child, "cpucl2",
					 &bcl_dev->zone[idx]->bcl_qos->cpu2_limit) != 0)
			bcl_dev->zone[idx]->bcl_qos->cpu2_limit = INT_MAX;
		if (of_property_read_u32(child, "gpu",
					 &bcl_dev->zone[idx]->bcl_qos->gpu_limit) != 0)
			bcl_dev->zone[idx]->bcl_qos->gpu_limit = INT_MAX;
		if (of_property_read_u32(child, "tpu",
					 &bcl_dev->zone[idx]->bcl_qos->tpu_limit) != 0)
			bcl_dev->zone[idx]->bcl_qos->tpu_limit = INT_MAX;
	}
}

static void google_set_intf_pmic_work(struct work_struct *work)
{
	struct bcl_device *bcl_dev = container_of(work, struct bcl_device, init_work.work);
	int ret = 0, i;
	unsigned int uvlo1_lvl, uvlo2_lvl, batoilo_lvl;
	u8 irq_val;

	if (!bcl_dev->intf_pmic_i2c)
		goto retry_init_work;
	if (IS_ERR_OR_NULL(bcl_dev->pmic_ops) || IS_ERR_OR_NULL(bcl_dev->pmic_ops->cb_uvlo_read))
		goto retry_init_work;
	if (bcl_cb_uvlo1_read(bcl_dev, &uvlo1_lvl) < 0)
		goto retry_init_work;
	if (bcl_cb_uvlo2_read(bcl_dev, &uvlo2_lvl) < 0)
		goto retry_init_work;
	if (bcl_cb_batoilo_read(bcl_dev, &batoilo_lvl) < 0)
		goto retry_init_work;
	if (bcl_cb_get_irq(bcl_dev, &irq_val) < 0)
		goto retry_init_work;
	if (bcl_cb_clr_irq(bcl_dev) < 0)
		goto retry_init_work;

	bcl_dev->batt_psy = google_get_power_supply(bcl_dev);
	bcl_dev->soc_tz = thermal_zone_of_sensor_register(bcl_dev->device, PMIC_SOC, bcl_dev,
							  &bcl_dev->soc_tz_ops);
	bcl_dev->soc_tz_ops.get_temp = google_bcl_read_soc;
	bcl_dev->soc_tz_ops.set_trips = google_bcl_set_soc;
	if (IS_ERR(bcl_dev->soc_tz)) {
		dev_err(bcl_dev->device, "soc TZ register failed. err:%ld\n",
			PTR_ERR(bcl_dev->soc_tz));
		ret = PTR_ERR(bcl_dev->soc_tz);
		bcl_dev->soc_tz = NULL;
	} else {
		bcl_dev->psy_nb.notifier_call = battery_supply_callback;
		ret = power_supply_reg_notifier(&bcl_dev->psy_nb);
		if (ret < 0)
			dev_err(bcl_dev->device,
				"soc notifier registration error. defer. err:%d\n", ret);
		thermal_zone_device_update(bcl_dev->soc_tz, THERMAL_DEVICE_UP);
	}
	bcl_dev->batt_psy_initialized = false;

	ret = google_bcl_register_zone(bcl_dev, UVLO1, "UVLO1",
				      IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				      bcl_dev->vdroop1_pin,
				      VD_BATTERY_VOLTAGE - uvlo1_lvl - THERMAL_HYST_LEVEL,
				      gpio_to_irq(bcl_dev->vdroop1_pin),
				      IF_PMIC);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: UVLO1\n");
		return;
	}
	ret = google_bcl_register_zone(bcl_dev, UVLO2, "UVLO2",
				      IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				      bcl_dev->vdroop2_pin,
				      VD_BATTERY_VOLTAGE - uvlo2_lvl - THERMAL_HYST_LEVEL,
				      gpio_to_irq(bcl_dev->vdroop2_pin),
				      IF_PMIC);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: UVLO2\n");
		return;
	}
	ret = google_bcl_register_zone(bcl_dev, BATOILO, "BATOILO",
				      IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				      bcl_dev->vdroop2_pin,
				      batoilo_lvl - THERMAL_HYST_LEVEL,
				      gpio_to_irq(bcl_dev->vdroop2_pin),
				      IF_PMIC);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: BATOILO\n");
		return;
	}

	bcl_dev->ready = true;
	google_bcl_parse_qos(bcl_dev);
	if (google_bcl_setup_qos(bcl_dev) != 0) {
		dev_err(bcl_dev->device, "Cannot Initiate QOS\n");
		bcl_dev->ready = false;
	}

	if (!bcl_dev->ready)
		return;

	for (i = 0; i < TRIGGERED_SOURCE_MAX; i++) {
		if (bcl_dev->zone[i] && (i != BATOILO)) {
			bcl_dev->zone[i]->disabled = false;
			enable_irq(bcl_dev->zone[i]->bcl_irq);
		}
	}

	return;

retry_init_work:
	schedule_delayed_work(&bcl_dev->init_work, msecs_to_jiffies(THERMAL_DELAY_INIT_MS));
}

static int google_set_intf_pmic(struct bcl_device *bcl_dev)
{
	int ret = 0;
	u8 val;
	struct device_node *p_np;
	struct device_node *np = bcl_dev->device->of_node;
	struct i2c_client *i2c;
	struct s2mpg14_platform_data *pdata_main;
	p_np = of_parse_phandle(np, "google,charger", 0);
	if (p_np) {
		i2c = of_find_i2c_device_by_node(p_np);
		if (!i2c) {
			dev_err(bcl_dev->device, "Cannot find Charger I2C\n");
			return -ENODEV;
		}
		bcl_dev->intf_pmic_i2c = i2c;
	}
	of_node_put(p_np);
	if (!bcl_dev->intf_pmic_i2c) {
		dev_err(bcl_dev->device, "Interface PMIC device not found\n");
		return -ENODEV;
	}

	pdata_main = dev_get_platdata(bcl_dev->main_dev);
	INIT_DELAYED_WORK(&bcl_dev->soc_work, google_bcl_evaluate_soc);
	if (pmic_read(CORE_PMIC_MAIN, bcl_dev, MAIN_CHIPID, &val)) {
		dev_err(bcl_dev->device, "Failed to read MAIN chipid.\n");
		return -ENODEV;
	}
	ret = google_bcl_register_zone(bcl_dev, PMIC_120C, "PMIC_120C",
				      IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				      0,
				      PMIC_120C_UPPER_LIMIT - THERMAL_HYST_LEVEL,
				      pdata_main->irq_base + INT3_120C,
				      CORE_MAIN_PMIC);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: PMIC_120C\n");
		return -ENODEV;
	}
	ret = google_bcl_register_zone(bcl_dev, PMIC_140C, "PMIC_140C",
				      IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				      0,
				      PMIC_140C_UPPER_LIMIT - THERMAL_HYST_LEVEL,
				      pdata_main->irq_base + INT3_140C,
				      CORE_MAIN_PMIC);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: PMIC_140C\n");
		return -ENODEV;
	}
	ret = google_bcl_register_zone(bcl_dev, PMIC_OVERHEAT, "PMIC_OVERHEAT",
				      IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				      0,
				      PMIC_OVERHEAT_UPPER_LIMIT - THERMAL_HYST_LEVEL,
				      pdata_main->irq_base + INT3_TSD,
				      CORE_MAIN_PMIC);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: PMIC_OVERHEAT\n");
		return -ENODEV;
	}

	return 0;
}

static int google_set_main_pmic(struct bcl_device *bcl_dev)
{
	struct s2mpg14_platform_data *pdata_main;
	struct s2mpg14_dev *main_dev = NULL;
	u8 val;
	struct device_node *p_np;
	struct device_node *np = bcl_dev->device->of_node;
	struct i2c_client *i2c;
	int ret, i, rail_i;

	INIT_DELAYED_WORK(&bcl_dev->main_pwr_irq_work, main_pwrwarn_irq_work);

	p_np = of_parse_phandle(np, "google,main-power", 0);
	if (p_np) {
		i2c = of_find_i2c_device_by_node(p_np);
		if (!i2c) {
			dev_err(bcl_dev->device, "Cannot find main-power I2C\n");
			return -ENODEV;
		}
		main_dev = i2c_get_clientdata(i2c);
	}
	of_node_put(p_np);
	if (!main_dev) {
		dev_err(bcl_dev->device, "Main PMIC device not found\n");
		return -ENODEV;
	}
	pdata_main = dev_get_platdata(main_dev->dev);
	bcl_dev->main_odpm = pdata_main->meter;
	for (i = 0; i < METER_CHANNEL_MAX; i++) {
		rail_i = bcl_dev->main_odpm->channels[i].rail_i;
		bcl_dev->main_rail_names[i] = bcl_dev->main_odpm->chip.rails[rail_i].schematic_name;
	}
	bcl_dev->main_irq_base = pdata_main->irq_base;
	bcl_dev->main_pmic_i2c = main_dev->pmic;
	bcl_dev->main_meter_i2c = main_dev->meter;
	bcl_dev->main_dev = main_dev->dev;
	/* clear MAIN information every boot */
	/* see b/215371539 */
	pmic_read(CORE_PMIC_MAIN, bcl_dev, S2MPG14_PM_OFFSRC1, &val);
	dev_info(bcl_dev->device, "MAIN OFFSRC1 : %#x\n", val);
	bcl_dev->main_offsrc1 = val;
	pmic_read(CORE_PMIC_MAIN, bcl_dev, S2MPG14_PM_OFFSRC2, &val);
	dev_info(bcl_dev->device, "MAIN OFFSRC2 : %#x\n", val);
	bcl_dev->main_offsrc2 = val;
	pmic_read(CORE_PMIC_MAIN, bcl_dev, S2MPG14_PM_PWRONSRC, &val);
	dev_info(bcl_dev->device, "MAIN PWRONSRC: %#x\n", val);
	bcl_dev->pwronsrc = val;
	pmic_write(CORE_PMIC_MAIN, bcl_dev, S2MPG14_PM_OFFSRC1, 0);
	pmic_write(CORE_PMIC_MAIN, bcl_dev, S2MPG14_PM_OFFSRC2, 0);
	pmic_write(CORE_PMIC_MAIN, bcl_dev, S2MPG14_PM_PWRONSRC, 0);

	ret = google_bcl_register_zone(bcl_dev, SMPL_WARN, "SMPL_WARN_IRQ",
				      IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
				      pdata_main->smpl_warn_pin,
				      SMPL_BATTERY_VOLTAGE -
				      (pdata_main->smpl_warn_lvl *
				       SMPL_STEP + SMPL_LOWER_LIMIT),
				      gpio_to_irq(pdata_main->smpl_warn_pin),
				      CORE_MAIN_PMIC);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: SMPL_WARN\n");
		return -ENODEV;
	}
	ret = google_bcl_register_zone(bcl_dev, OCP_WARN_CPUCL1, "CPU1_OCP_IRQ",
				      IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				      pdata_main->b3_ocp_warn_pin,
				      CPU1_UPPER_LIMIT - THERMAL_HYST_LEVEL -
				      (pdata_main->b3_ocp_warn_lvl * CPU1_STEP),
				      gpio_to_irq(pdata_main->b3_ocp_warn_pin),
				      CORE_MAIN_PMIC);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: CPUCL1\n");
		return -ENODEV;
	}
	ret = google_bcl_register_zone(bcl_dev, OCP_WARN_CPUCL2, "CPU2_OCP_IRQ",
				      IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				      pdata_main->b2_ocp_warn_pin,
				      CPU2_UPPER_LIMIT - THERMAL_HYST_LEVEL -
				      (pdata_main->b2_ocp_warn_lvl * CPU2_STEP),
				      gpio_to_irq(pdata_main->b2_ocp_warn_pin),
				      CORE_MAIN_PMIC);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: CPUCL2\n");
		return -ENODEV;
	}
	ret = google_bcl_register_zone(bcl_dev, SOFT_OCP_WARN_CPUCL1, "SOFT_CPU1_OCP_IRQ",
				      IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				      pdata_main->b3_soft_ocp_warn_pin,
				      CPU1_UPPER_LIMIT - THERMAL_HYST_LEVEL -
				      (pdata_main->b3_soft_ocp_warn_lvl * CPU1_STEP),
				      gpio_to_irq(pdata_main->b3_soft_ocp_warn_pin),
				      CORE_MAIN_PMIC);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: SOFT_CPUCL1\n");
		return -ENODEV;
	}
	ret = google_bcl_register_zone(bcl_dev, SOFT_OCP_WARN_CPUCL2, "SOFT_CPU2_OCP_IRQ",
				      IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				      pdata_main->b2_soft_ocp_warn_pin,
				      CPU2_UPPER_LIMIT - THERMAL_HYST_LEVEL -
				      (pdata_main->b2_soft_ocp_warn_lvl * CPU2_STEP),
				      gpio_to_irq(pdata_main->b2_soft_ocp_warn_pin),
				      CORE_MAIN_PMIC);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: SOFT_CPUCL2\n");
		return -ENODEV;
	}
	ret = google_bcl_register_zone(bcl_dev, OCP_WARN_TPU, "TPU_OCP_IRQ",
				      IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				      pdata_main->b7_ocp_warn_pin,
				      TPU_UPPER_LIMIT - THERMAL_HYST_LEVEL -
				      (pdata_main->b7_ocp_warn_lvl * TPU_STEP),
				      gpio_to_irq(pdata_main->b7_ocp_warn_pin),
				      CORE_MAIN_PMIC);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: TPU\n");
		return -ENODEV;
	}
	ret = google_bcl_register_zone(bcl_dev, SOFT_OCP_WARN_TPU, "SOFT_TPU_OCP_IRQ",
				      IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				      pdata_main->b7_soft_ocp_warn_pin,
				      TPU_UPPER_LIMIT - THERMAL_HYST_LEVEL -
				      (pdata_main->b7_soft_ocp_warn_lvl * TPU_STEP),
				      gpio_to_irq(pdata_main->b7_soft_ocp_warn_pin),
				      CORE_MAIN_PMIC);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: SOFT_TPU\n");
		return -ENODEV;
	}
	for (i = 0; i < S2MPG1415_METER_CHANNEL_MAX; i++) {
		bcl_dev->main_pwr_warn_irq[i] = bcl_dev->main_irq_base
				+ S2MPG14_IRQ_PWR_WARN_CH0_INT6 + i;
		ret = devm_request_threaded_irq(bcl_dev->device, bcl_dev->main_pwr_warn_irq[i],
						NULL, main_pwr_warn_irq_handler, 0,
						bcl_dev->main_rail_names[i], bcl_dev);
		if (ret < 0) {
			dev_err(bcl_dev->device, "Failed to request PWR_WARN_CH%d IRQ: %d: %d\n",
				i, bcl_dev->main_pwr_warn_irq[i], ret);
		}
	}


	return 0;

}

extern const struct attribute_group *mitigation_groups[];

static int google_init_fs(struct bcl_device *bcl_dev)
{
	bcl_dev->mitigation_dev = pmic_subdevice_create(NULL, mitigation_groups,
							bcl_dev, "mitigation");
	if (IS_ERR(bcl_dev->mitigation_dev))
		return -ENODEV;

	return 0;
}

static void google_bcl_enable_vdroop_irq(struct bcl_device *bcl_dev)
{
	void __iomem *gpio_alive;
	unsigned int reg;

	gpio_alive = ioremap(GPIO_ALIVE_BASE, SZ_4K);
	reg = __raw_readl(gpio_alive + GPA9_CON);
	reg |= 0xFF0000;
	__raw_writel(0xFFFFF22, gpio_alive + GPA9_CON);
}

static int google_bcl_init_instruction(struct bcl_device *bcl_dev)
{
	if (!bcl_dev)
		return -EIO;

	bcl_dev->core_conf[SUBSYSTEM_CPU0].base_mem = devm_ioremap(bcl_dev->device,
	                                                           CPUCL0_BASE, SZ_8K);
	if (!bcl_dev->core_conf[SUBSYSTEM_CPU0].base_mem) {
		dev_err(bcl_dev->device, "cpu0_mem ioremap failed\n");
		return -EIO;
	}
	bcl_dev->core_conf[SUBSYSTEM_CPU1].base_mem = devm_ioremap(bcl_dev->device,
	                                                           CPUCL1_BASE, SZ_8K);
	if (!bcl_dev->core_conf[SUBSYSTEM_CPU1].base_mem) {
		dev_err(bcl_dev->device, "cpu1_mem ioremap failed\n");
		return -EIO;
	}
	bcl_dev->core_conf[SUBSYSTEM_CPU2].base_mem = devm_ioremap(bcl_dev->device,
	                                                           CPUCL2_BASE, SZ_8K);
	if (!bcl_dev->core_conf[SUBSYSTEM_CPU2].base_mem) {
		dev_err(bcl_dev->device, "cpu2_mem ioremap failed\n");
		return -EIO;
	}
	bcl_dev->core_conf[SUBSYSTEM_TPU].base_mem = devm_ioremap(bcl_dev->device,
	                                                          TPU_BASE, SZ_8K);
	if (!bcl_dev->core_conf[SUBSYSTEM_TPU].base_mem) {
		dev_err(bcl_dev->device, "tpu_mem ioremap failed\n");
		return -EIO;
	}
	bcl_dev->core_conf[SUBSYSTEM_GPU].base_mem = devm_ioremap(bcl_dev->device,
	                                                          G3D_BASE, SZ_8K);
	if (!bcl_dev->core_conf[SUBSYSTEM_GPU].base_mem) {
		dev_err(bcl_dev->device, "gpu_mem ioremap failed\n");
		return -EIO;
	}
	bcl_dev->core_conf[SUBSYSTEM_AUR].base_mem = devm_ioremap(bcl_dev->device,
	                                                          AUR_BASE, SZ_8K);
	if (!bcl_dev->core_conf[SUBSYSTEM_AUR].base_mem) {
		dev_err(bcl_dev->device, "aur_mem ioremap failed\n");
		return -EIO;
	}
	bcl_dev->sysreg_cpucl0 = devm_ioremap(bcl_dev->device, SYSREG_CPUCL0_BASE, SZ_8K);
	if (!bcl_dev->sysreg_cpucl0) {
		dev_err(bcl_dev->device, "sysreg_cpucl0 ioremap failed\n");
		return -EIO;
	}

	mutex_init(&bcl_dev->state_trans_lock);
	mutex_init(&bcl_dev->ratio_lock);
	google_bcl_enable_vdroop_irq(bcl_dev);

	bcl_dev->base_add_mem[SUBSYSTEM_CPU0] = devm_ioremap(bcl_dev->device, ADD_CPUCL0, SZ_128);
	if (!bcl_dev->base_add_mem[SUBSYSTEM_CPU0]) {
		dev_err(bcl_dev->device, "cpu0_add_mem ioremap failed\n");
		return -EIO;
	}

	bcl_dev->base_add_mem[SUBSYSTEM_CPU1] = devm_ioremap(bcl_dev->device, ADD_CPUCL1, SZ_128);
	if (!bcl_dev->base_add_mem[SUBSYSTEM_CPU1]) {
		dev_err(bcl_dev->device, "cpu1_add_mem ioremap failed\n");
		return -EIO;
	}

	bcl_dev->base_add_mem[SUBSYSTEM_CPU2] = devm_ioremap(bcl_dev->device, ADD_CPUCL2, SZ_128);
	if (!bcl_dev->base_add_mem[SUBSYSTEM_CPU2]) {
		dev_err(bcl_dev->device, "cpu2_add_mem ioremap failed\n");
		return -EIO;
	}

	bcl_dev->base_add_mem[SUBSYSTEM_TPU] = devm_ioremap(bcl_dev->device, ADD_TPU, SZ_128);
	if (!bcl_dev->base_add_mem[SUBSYSTEM_TPU]) {
		dev_err(bcl_dev->device, "tpu_add_mem ioremap failed\n");
		return -EIO;
	}

	bcl_dev->base_add_mem[SUBSYSTEM_GPU] = devm_ioremap(bcl_dev->device, ADD_G3D, SZ_128);
	if (!bcl_dev->base_add_mem[SUBSYSTEM_GPU]) {
		dev_err(bcl_dev->device, "gpu_add_mem ioremap failed\n");
		return -EIO;
	}

	bcl_dev->base_add_mem[SUBSYSTEM_AUR] = devm_ioremap(bcl_dev->device, ADD_AUR, SZ_128);
	if (!bcl_dev->base_add_mem[SUBSYSTEM_AUR]) {
		dev_err(bcl_dev->device, "aur_add_mem ioremap failed\n");
		return -EIO;
	}
	return 0;
}

u64 settings_to_current(struct bcl_device *bcl_dev, int pmic, int idx, u32 setting)
{
	int rail_i;
	s2mpg1415_meter_muxsel muxsel;
	struct odpm_info *info;
	u64 raw_unit;
	u32 resolution;

	if (!bcl_dev)
		return 0;
	if (pmic == CORE_PMIC_MAIN)
		info = bcl_dev->main_odpm;
	else
		info = bcl_dev->sub_odpm;

	if (!info)
		return 0;

	rail_i = info->channels[idx].rail_i;
	muxsel = info->chip.rails[rail_i].mux_select;
	if ((strstr(bcl_dev->main_rail_names[idx], "VSYS") != NULL) ||
		(strstr(bcl_dev->sub_rail_names[idx], "VSYS") != NULL)) {
			resolution = (u32) VSHUNT_MULTIPLIER * ((u64)EXTERNAL_RESOLUTION_VSHUNT) /
					info->chip.rails[rail_i].shunt_uohms;
	} else {
		if (pmic == CORE_PMIC_MAIN)
			resolution = s2mpg14_muxsel_to_current_resolution(muxsel);
		else
			resolution = s2mpg15_muxsel_to_current_resolution(muxsel);
	}
	raw_unit = (u64)setting * resolution;
	raw_unit = raw_unit * MILLI_TO_MICRO;
	return (u32)_IQ30_to_int(raw_unit);
}

static void google_bcl_parse_dtree(struct bcl_device *bcl_dev)
{
	int ret, i = 0;
	struct device_node *np = bcl_dev->device->of_node;
	struct device_node *child;
	struct device_node *p_np;
	u32 val;
	int read;

	if (!bcl_dev) {
		dev_err(bcl_dev->device, "Cannot parse device tree\n");
		return;
	}
	ret = of_property_read_u32(np, "tpu_con_heavy", &val);
	bcl_dev->core_conf[SUBSYSTEM_TPU].con_heavy = ret ? 0 : val;
	ret = of_property_read_u32(np, "tpu_con_light", &val);
	bcl_dev->core_conf[SUBSYSTEM_TPU].con_light = ret ? 0 : val;
	ret = of_property_read_u32(np, "gpu_con_heavy", &val);
	bcl_dev->core_conf[SUBSYSTEM_GPU].con_heavy = ret ? 0 : val;
	ret = of_property_read_u32(np, "gpu_con_light", &val);
	bcl_dev->core_conf[SUBSYSTEM_GPU].con_light = ret ? 0 : val;
	ret = of_property_read_u32(np, "gpu_clkdivstep", &val);
	bcl_dev->core_conf[SUBSYSTEM_GPU].clkdivstep = ret ? 0 : val;
	ret = of_property_read_u32(np, "tpu_clkdivstep", &val);
	bcl_dev->core_conf[SUBSYSTEM_TPU].clkdivstep = ret ? 0 : val;
	ret = of_property_read_u32(np, "aur_clkdivstep", &val);
	bcl_dev->core_conf[SUBSYSTEM_AUR].clkdivstep = ret ? 0 : val;
	ret = of_property_read_u32(np, "cpu2_clkdivstep", &val);
	bcl_dev->core_conf[SUBSYSTEM_CPU2].clkdivstep = ret ? 0 : val;
	ret = of_property_read_u32(np, "cpu1_clkdivstep", &val);
	bcl_dev->core_conf[SUBSYSTEM_CPU1].clkdivstep = ret ? 0 : val;
	ret = of_property_read_u32(np, "cpu0_clkdivstep", &val);
	bcl_dev->core_conf[SUBSYSTEM_CPU0].clkdivstep = ret ? 0 : val;
	ret = of_property_read_u32(np, "irq_enable_delay", &val);
	bcl_dev->irq_delay = ret ? IRQ_ENABLE_DELAY_MS : val;
	bcl_dev->vdroop1_pin = of_get_gpio(np, 0);
	bcl_dev->vdroop2_pin = of_get_gpio(np, 1);
	bcl_dev->modem_gpio1_pin = of_get_gpio(np, 2);
	bcl_dev->modem_gpio2_pin = of_get_gpio(np, 3);
	ret = of_property_read_u32(np, "rffe_channel", &val);
	bcl_dev->rffe_channel = ret ? 11 : val;
	ret = of_property_read_u32(np, "cpu0_cluster", &val);
	bcl_dev->cpu0_cluster = ret ? CPU0_CLUSTER_MIN : val;
	ret = of_property_read_u32(np, "cpu1_cluster", &val);
	bcl_dev->cpu1_cluster = ret ? CPU1_CLUSTER_MIN : val;
	ret = of_property_read_u32(np, "cpu2_cluster", &val);
	bcl_dev->cpu2_cluster = ret ? CPU2_CLUSTER_MIN : val;

	/* parse ODPM main limit */
	p_np = of_get_child_by_name(np, "main_limit");
	if (p_np) {
		for_each_child_of_node(p_np, child) {
			of_property_read_u32(child, "setting", &read);
			if (i < METER_CHANNEL_MAX) {
				bcl_dev->main_setting[i] = read;
				meter_write(CORE_PMIC_MAIN, bcl_dev,
				            S2MPG14_METER_PWR_WARN0 + i, read);
				bcl_dev->main_limit[i] =
				    settings_to_current(bcl_dev, CORE_PMIC_MAIN, i,
				                        read << LPF_CURRENT_SHIFT);
				i++;
			}
		}
	}

	/* parse ODPM sub limit */
	p_np = of_get_child_by_name(np, "sub_limit");
	i = 0;
	if (p_np) {
		for_each_child_of_node(p_np, child) {
			of_property_read_u32(child, "setting", &read);
			if (i < METER_CHANNEL_MAX) {
				bcl_dev->sub_setting[i] = read;
				meter_write(CORE_PMIC_SUB, bcl_dev,
				            S2MPG15_METER_PWR_WARN0 + i, read);
				bcl_dev->sub_limit[i] =
				    settings_to_current(bcl_dev, CORE_PMIC_SUB, i,
				                        read << LPF_CURRENT_SHIFT);
				i++;
			}
		}
	}

	if (bcl_disable_power(SUBSYSTEM_CPU2)) {
		if (google_bcl_init_clk_div(bcl_dev, SUBSYSTEM_CPU2,
					    bcl_dev->core_conf[SUBSYSTEM_CPU2].clkdivstep) != 0)
			dev_err(bcl_dev->device, "CPU2 Address is NULL\n");
		bcl_enable_power(SUBSYSTEM_CPU2);
	}
	if (bcl_disable_power(SUBSYSTEM_CPU1)) {
		if (google_bcl_init_clk_div(bcl_dev, SUBSYSTEM_CPU1,
					    bcl_dev->core_conf[SUBSYSTEM_CPU1].clkdivstep) != 0)
			dev_err(bcl_dev->device, "CPU1 Address is NULL\n");
		bcl_enable_power(SUBSYSTEM_CPU1);
	}
	if (google_bcl_init_clk_div(bcl_dev, SUBSYSTEM_CPU0,
	                            bcl_dev->core_conf[SUBSYSTEM_CPU0].clkdivstep) != 0)
		dev_err(bcl_dev->device, "CPU0 Address is NULL\n");
}

static int google_bcl_configure_modem(struct bcl_device *bcl_dev)
{
	struct pinctrl *modem_pinctrl;
	struct pinctrl_state *batoilo_pinctrl_state, *rffe_pinctrl_state;
	int ret;

	modem_pinctrl = devm_pinctrl_get(bcl_dev->device);
	if (IS_ERR_OR_NULL(modem_pinctrl)) {
		dev_err(bcl_dev->device, "Cannot find modem_pinctrl!\n");
		return -EINVAL;
	}
	batoilo_pinctrl_state = pinctrl_lookup_state(modem_pinctrl, "bcl-batoilo-modem");
	if (IS_ERR_OR_NULL(batoilo_pinctrl_state)) {
		dev_err(bcl_dev->device, "batoilo: pinctrl lookup state failed!\n");
		return -EINVAL;
	}
	rffe_pinctrl_state = pinctrl_lookup_state(modem_pinctrl, "bcl-rffe-modem");
	if (IS_ERR_OR_NULL(rffe_pinctrl_state)) {
		dev_err(bcl_dev->device, "rffe: pinctrl lookup state failed!\n");
		return -EINVAL;
	}
	ret = pinctrl_select_state(modem_pinctrl, batoilo_pinctrl_state);
	if (ret < 0) {
		dev_err(bcl_dev->device, "batoilo: pinctrl select state failed!!\n");
		return -EINVAL;
	}
	ret = pinctrl_select_state(modem_pinctrl, rffe_pinctrl_state);
	if (ret < 0) {
		dev_err(bcl_dev->device, "rffe: pinctrl select state failed!!\n");
		return -EINVAL;
	}
	return 0;
}

static int google_bcl_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct bcl_device *bcl_dev;

	bcl_dev = devm_kzalloc(&pdev->dev, sizeof(*bcl_dev), GFP_KERNEL);
	if (!bcl_dev)
		return -ENOMEM;
	bcl_dev->device = &pdev->dev;

	mutex_init(&bcl_dev->sysreg_lock);
	INIT_DELAYED_WORK(&bcl_dev->init_work, google_set_intf_pmic_work);
	platform_set_drvdata(pdev, bcl_dev);

	bcl_dev->ready = false;
	ret = google_bcl_init_instruction(bcl_dev);
	if (ret < 0)
		goto bcl_soc_probe_exit;

	google_set_main_pmic(bcl_dev);
	google_set_sub_pmic(bcl_dev);
	google_bcl_parse_dtree(bcl_dev);
	google_bcl_configure_modem(bcl_dev);

	ret = google_init_fs(bcl_dev);
	if (ret < 0)
		goto bcl_soc_probe_exit;
	if (google_set_intf_pmic(bcl_dev) < 0)
		goto bcl_soc_probe_exit;
	schedule_delayed_work(&bcl_dev->init_work, msecs_to_jiffies(THERMAL_DELAY_INIT_MS));
	bcl_dev->enabled = true;
	google_init_debugfs(bcl_dev);

	return 0;

bcl_soc_probe_exit:
	google_bcl_remove_thermal(bcl_dev);
	return ret;
}

static int google_bcl_remove(struct platform_device *pdev)
{
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	pmic_device_destroy(bcl_dev->mitigation_dev->devt);
	debugfs_remove_recursive(bcl_dev->debug_entry);
	google_bcl_remove_thermal(bcl_dev);
	if (bcl_dev->ready)
		google_bcl_remove_qos(bcl_dev);

	return 0;
}

static const struct of_device_id match_table[] = {
	{ .compatible = "google,google-bcl"},
	{},
};

static struct platform_driver google_bcl_driver = {
	.probe  = google_bcl_probe,
	.remove = google_bcl_remove,
	.id_table = google_id_table,
	.driver = {
		.name           = "google_mitigation",
		.owner          = THIS_MODULE,
		.of_match_table = match_table,
	},
};

module_platform_driver(google_bcl_driver);

MODULE_SOFTDEP("pre: i2c-acpm");
MODULE_DESCRIPTION("Google Battery Current Limiter");
MODULE_AUTHOR("George Lee <geolee@google.com>");
MODULE_LICENSE("GPL");
