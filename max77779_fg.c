/*
 * Fuel gauge driver for Maxim 77779
 *
 * Copyright (C) 2023 Google Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": %s " fmt, __func__

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/time.h>

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h> /* register_chrdev, unregister_chrdev */
#include <linux/seq_file.h> /* seq_read, seq_lseek, single_release */
#include "gbms_power_supply.h"
#include "google_bms.h"
#include "max77779_fg.h"

#include <linux/debugfs.h>

#define MAX77779_FG_TPOR_MS 800

#define MAX77779_FG_TICLR_MS 500
#define MAX77779_FG_I2C_DRIVER_NAME "max_fg_irq"
#define MAX77779_FG_DELAY_INIT_MS 1000
#define FULLCAPNOM_STABILIZE_CYCLES 5

#define BHI_IMPEDANCE_SOC_LO		50
#define BHI_IMPEDANCE_SOC_HI		55
#define BHI_IMPEDANCE_TEMP_LO		250
#define BHI_IMPEDANCE_TEMP_HI		300
#define BHI_IMPEDANCE_CYCLE_CNT		5
#define BHI_IMPEDANCE_TIMERH		50 /* 7*24 / 3.2hr */

enum max77779_fg_command_bits {
	MAX77779_FG_COMMAND_HARDWARE_RESET = 0x000F,
};

/* Capacity Estimation */
struct gbatt_capacity_estimation {
	const struct max17x0x_reg *bcea;
	struct mutex batt_ce_lock;
	struct delayed_work settle_timer;
	int cap_tsettle;
	int cap_filt_length;
	int estimate_state;
	bool cable_in;
	int delta_cc_sum;
	int delta_vfsoc_sum;
	int cap_filter_count;
	int start_cc;
	int start_vfsoc;
};

#define DEFAULT_BATTERY_ID		0
#define DEFAULT_BATTERY_ID_RETRIES	20
#define DUMMY_BATTERY_ID		170

#define ESTIMATE_DONE		2
#define ESTIMATE_PENDING	1
#define ESTIMATE_NONE		0

#define CE_CAP_FILTER_COUNT	0
#define CE_DELTA_CC_SUM_REG	1
#define CE_DELTA_VFSOC_SUM_REG	2

#define CE_FILTER_COUNT_MAX	15

#define BHI_CAP_FCN_COUNT	3

#define DEFAULT_STATUS_CHARGE_MA	100

/* No longer used in 79, used for taskperiod re-scaling in 59 */
#define MAX77779_LSB 1

#define MAX77779_FG_NDGB_ADDRESS 0x37

#pragma pack(1)
struct max77779_fg_eeprom_history {
	u16 tempco;
	u16 rcomp0;
	u8 timerh;
	unsigned fullcapnom:10;
	unsigned fullcaprep:10;
	unsigned mixsoc:6;
	unsigned vfsoc:6;
	unsigned maxvolt:4;
	unsigned minvolt:4;
	unsigned maxtemp:4;
	unsigned mintemp:4;
	unsigned maxchgcurr:4;
	unsigned maxdischgcurr:4;
};
#pragma pack()

struct max77779_fg_chip {
	struct device *dev;
	struct i2c_client *primary;
	struct i2c_client *secondary;

	int gauge_type;	/* -1 not present, 0=max1720x, 1=max1730x */
	struct max17x0x_regmap regmap;
	struct max17x0x_regmap regmap_debug;
	struct power_supply *psy;
	struct delayed_work init_work;
	struct device_node *batt_node;

	u16 devname;

	/* config */
	void *model_data;
	struct mutex model_lock;
	struct delayed_work model_work;
	int model_next_update;
	/* also used to restore model state from permanent storage */
	u16 reg_prop_capacity_raw;
	bool model_state_valid;	/* state read from persistent */
	int model_reload;
	bool model_ok;		/* model is running */

	int fake_battery;

	u16 RSense;
	u16 RConfig;

	int batt_id;
	int batt_id_defer_cnt;
	int cycle_count;
	int cycle_count_offset;
	u16 eeprom_cycle;

	bool init_complete;
	bool resume_complete;
	bool irq_disabled;
	u16 health_status;
	int fake_capacity;
	int previous_qh;
	int current_capacity;
	int prev_charge_status;
	char serial_number[30];
	bool offmode_charger;
	bool por;

	unsigned int debug_irq_none_cnt;

	/* Capacity Estimation */
	struct gbatt_capacity_estimation cap_estimate;
	struct logbuffer *ce_log;

	/* debug interface, register to read or write */
	u32 debug_reg_address;
	u32 debug_dbg_reg_address;

	/* dump data to logbuffer periodically */
	struct logbuffer *monitor_log;
	u16 pre_repsoc;

	struct power_supply_desc max77779_fg_psy_desc;

	int bhi_fcn_count;
	int bhi_acim;

	/* battery current criteria for report status charge */
	u32 status_charge_threshold_ma;
};

static irqreturn_t max77779_fg_irq_thread_fn(int irq, void *obj);
static int max77779_fg_set_next_update(struct max77779_fg_chip *chip);
static int max77779_fg_monitor_log_data(struct max77779_fg_chip *chip, bool force_log);

static bool max77779_fg_reglog_init(struct max77779_fg_chip *chip)
{
	chip->regmap.reglog = devm_kzalloc(chip->dev, sizeof(*chip->regmap.reglog), GFP_KERNEL);

	return chip->regmap.reglog;
}

/* TODO: b/285191823 - Validate all conversion helper functions */
/* ------------------------------------------------------------------------- */

static inline int reg_to_percentage(u16 val)
{
	/* LSB: 1/256% */
	return val >> 8;
}

static inline int reg_to_twos_comp_int(u16 val)
{
	/* Convert u16 to twos complement  */
	return -(val & 0x8000) + (val & 0x7FFF);
}

static inline int reg_to_micro_amp(s16 val, u16 rsense)
{
	/* LSB: 1.5625μV/RSENSE ; Rsense LSB is 2μΩ */
	return div_s64((s64) val * 156250, rsense);
}

static inline int reg_to_deci_deg_cel(s16 val)
{
	/* LSB: 1/256°C */
	return div_s64((s64) val * 10, 256);
}

static inline int reg_to_resistance_micro_ohms(s16 val, u16 rsense)
{
	/* LSB: 1/4096 Ohm */
	return div_s64((s64) val * 1000 * rsense, 4096);
}

static inline int reg_to_cycles(u32 val)
{
	/* LSB: 1% of one cycle */
	return DIV_ROUND_CLOSEST(val, 100);
}

static inline int reg_to_seconds(s16 val)
{
	/* LSB: 5.625 seconds */
	return DIV_ROUND_CLOSEST((int) val * 5625, 1000);
}

static inline int reg_to_vempty(u16 val)
{
	return ((val >> 7) & 0x1FF) * 20;
}

static inline int reg_to_vrecovery(u16 val)
{
	return (val & 0x7F) * 40;
}

static inline int reg_to_capacity_uah(u16 val, struct max77779_fg_chip *chip)
{
	return reg_to_micro_amp_h(val, chip->RSense, MAX77779_LSB);
}

static inline int reg_to_time_hr(u16 val, struct max77779_fg_chip *chip)
{

	return (val * 32) / 10;
}

/* log ----------------------------------------------------------------- */

static int format_battery_history_entry(char *temp, int size, int page_size, u16 *line)
{
	int length = 0, i;

	for (i = 0; i < page_size; i++) {
		length += scnprintf(temp + length,
			size - length, "%04x ",
			line[i]);
	}

	if (length > 0)
		temp[--length] = 0;
	return length;
}

/*
 * Removed the following properties:
 *   POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG
 *   POWER_SUPPLY_PROP_TIME_TO_FULL_AVG
 *   POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
 *   POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
 * Need to keep the number of properies under UEVENT_NUM_ENVP (minus # of
 * standard uevent variables).
 */
static enum power_supply_property max77779_fg_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CAPACITY,		/* replace with _RAW */
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,	/* used from gbattery */
	POWER_SUPPLY_PROP_CURRENT_AVG,		/* candidate for tier switch */
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
};

/* ------------------------------------------------------------------------- */

static ssize_t offmode_charger_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct power_supply *psy = container_of(dev, struct power_supply, dev);
	struct max77779_fg_chip *chip = power_supply_get_drvdata(psy);

	return scnprintf(buf, PAGE_SIZE, "%hhd\n", chip->offmode_charger);
}

static ssize_t offmode_charger_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct power_supply *psy = container_of(dev, struct power_supply, dev);
	struct max77779_fg_chip *chip = power_supply_get_drvdata(psy);

	if (kstrtobool(buf, &chip->offmode_charger))
		return -EINVAL;

	return count;
}

static DEVICE_ATTR_RW(offmode_charger);

int max77779_fg_usr_lock(const struct max17x0x_regmap *map, bool enabled)
{
	int ret, i;
	const u16 code = enabled ? 0x111 : 0;

	/* Requires write twice */
	for (i = 0; i < 2; i++) {
		ret = REGMAP_WRITE(map, MAX77779_FG_USR, code);
		if (ret)
			return ret;
	}
	return 0;
}

/* TODO: b/283487421 - Add NDGB reg write function */
int max77779_fg_register_write(const struct max17x0x_regmap *map,
																			unsigned int reg, u16 value, bool verify)
{
	int ret;

	/* TODO: b/285938678 - Lock/unlock specific register areas around transactions */
	ret = max77779_fg_usr_lock(map, false);
	if (ret) {
		pr_err("Failed to unlock ret=%d\n", ret);
		return ret;
	}

	if (verify)
		ret = REGMAP_WRITE_VERIFY(map, reg, value);
	else
		ret = REGMAP_WRITE(map, reg, value);
	if (ret) {
		pr_err("Failed to write reg verify=%d ret=%d\n", verify, ret);
		max77779_fg_usr_lock(map, true);
		return ret;
	}

	ret = max77779_fg_usr_lock(map, true);
	if (ret)
		pr_err("Failed to lock ret=%d\n", ret);

	return ret;
}

/*
 * force is true when changing the model via debug props.
 * NOTE: call holding model_lock
 */
static int max77779_fg_model_reload(struct max77779_fg_chip *chip, bool force)
{
	const bool disabled = chip->model_reload == MAX77779_LOAD_MODEL_DISABLED;
	const bool pending = chip->model_reload != MAX77779_LOAD_MODEL_IDLE;
	int version_now, version_load;

	pr_debug("model_reload=%d force=%d pending=%d disabled=%d\n",
		 chip->model_reload, force, pending, disabled);

	if (!force && (pending || disabled))
		return -EEXIST;

	version_now = max77779_model_read_version(chip->model_data);
	version_load = max77779_fg_model_version(chip->model_data);

	if (!force && version_now == version_load)
		return -EEXIST;

	/* REQUEST -> IDLE or set to the number of retries */
	dev_info(chip->dev, "Schedule Load FG Model, ID=%d, ver:%d->%d\n",
		 chip->batt_id, version_now, version_load);

	chip->model_reload = MAX77779_LOAD_MODEL_REQUEST;
	chip->model_ok = false;
	mod_delayed_work(system_wq, &chip->model_work, 0);

	return 0;
}

/* resistance and impedance ------------------------------------------------ */

static int max77779_fg_read_resistance_avg(struct max77779_fg_chip *chip)
{
	u16 ravg;
	int ret = 0;

	ret = gbms_storage_read(GBMS_TAG_RAVG, &ravg, sizeof(ravg));
	if (ret < 0)
		return ret;

	return reg_to_resistance_micro_ohms(ravg, chip->RSense);
}

static int max77779_fg_read_resistance_raw(struct max77779_fg_chip *chip)
{
	u16 data;
	int ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_RSlow, &data);
	if (ret < 0)
		return ret;

	return data;
}

static int max77779_fg_read_resistance(struct max77779_fg_chip *chip)
{
	int rslow;

	rslow = max77779_fg_read_resistance_raw(chip);
	if (rslow < 0)
		return rslow;

	return reg_to_resistance_micro_ohms(rslow, chip->RSense);
}


/* ----------------------------------------------------------------------- */

static ssize_t max77779_fg_model_state_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct power_supply *psy = container_of(dev, struct power_supply, dev);
	struct max77779_fg_chip *chip = power_supply_get_drvdata(psy);
	ssize_t len = 0;

	if (!chip->model_data)
		return -EINVAL;

	mutex_lock(&chip->model_lock);
	len += scnprintf(&buf[len], PAGE_SIZE, "ModelNextUpdate: %d\n",
			 chip->model_next_update);
	len += max77779_model_state_cstr(&buf[len], PAGE_SIZE - len,
				       chip->model_data);
	mutex_unlock(&chip->model_lock);

	return len;
}

static ssize_t gmsr_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buff)
{
	struct power_supply *psy = container_of(dev, struct power_supply, dev);
	struct max77779_fg_chip *chip = power_supply_get_drvdata(psy);
	ssize_t len = 0;

	mutex_lock(&chip->model_lock);
	len = max77779_gmsr_state_cstr(&buff[len], PAGE_SIZE);
	mutex_unlock(&chip->model_lock);

	return len;
}

static DEVICE_ATTR_RO(gmsr);

/* Was POWER_SUPPLY_PROP_RESISTANCE_ID */
static ssize_t resistance_id_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buff)
{
	struct power_supply *psy = container_of(dev, struct power_supply, dev);
	struct max77779_fg_chip *chip = power_supply_get_drvdata(psy);

	return scnprintf(buff, PAGE_SIZE, "%d\n", chip->batt_id);
}

static DEVICE_ATTR_RO(resistance_id);

/* Was POWER_SUPPLY_PROP_RESISTANCE */
static ssize_t resistance_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buff)
{
	struct power_supply *psy = container_of(dev, struct power_supply, dev);
	struct max77779_fg_chip *chip = power_supply_get_drvdata(psy);

	return scnprintf(buff, PAGE_SIZE, "%d\n", max77779_fg_read_resistance(chip));
}

static DEVICE_ATTR_RO(resistance);

/* lsb 1/256, race with max77779_fg_model_work()  */
static int max77779_fg_get_capacity_raw(struct max77779_fg_chip *chip, u16 *data)
{
	return REGMAP_READ(&chip->regmap, chip->reg_prop_capacity_raw, data);
}

static int max77779_fg_get_battery_soc(struct max77779_fg_chip *chip)
{
	u16 data;
	int capacity, err;

	if (chip->fake_capacity >= 0 && chip->fake_capacity <= 100)
		return chip->fake_capacity;

	err = REGMAP_READ(&chip->regmap, MAX77779_FG_RepSOC, &data);
	if (err)
		return err;
	capacity = reg_to_percentage(data);

	if (capacity == 100 && chip->offmode_charger)
		chip->fake_capacity = 100;

	return capacity;
}

static int max77779_fg_get_battery_vfsoc(struct max77779_fg_chip *chip)
{
	u16 data;
	int capacity, err;


	err = REGMAP_READ(&chip->regmap, MAX77779_FG_VFSOC, &data);
	if (err)
		return err;
	capacity = reg_to_percentage(data);

	return capacity;
}

static void max77779_fg_prime_battery_qh_capacity(struct max77779_fg_chip *chip,
					       int status)
{
	u16  mcap = 0, data = 0;

	(void)REGMAP_READ(&chip->regmap, MAX77779_FG_MixCap, &mcap);
	chip->current_capacity = mcap;

	(void)REGMAP_READ(&chip->regmap, MAX77779_FG_QH, &data);
	chip->previous_qh = reg_to_twos_comp_int(data);
}

/* NOTE: the gauge doesn't know if we are current limited to */
static int max77779_fg_get_battery_status(struct max77779_fg_chip *chip)
{
	u16 data = 0;
	int current_now, current_avg, ichgterm, vfsoc, soc, fullsocthr;
	int status = POWER_SUPPLY_STATUS_UNKNOWN, err;

	err = REGMAP_READ(&chip->regmap, MAX77779_FG_Current, &data);
	if (err)
		return -EIO;
	current_now = -reg_to_micro_amp(data, chip->RSense);

	err = REGMAP_READ(&chip->regmap, MAX77779_FG_AvgCurrent, &data);
	if (err)
		return -EIO;
	current_avg = -reg_to_micro_amp(data, chip->RSense);

	if (chip->status_charge_threshold_ma) {
		ichgterm = chip->status_charge_threshold_ma * 1000;
	} else {
		err = REGMAP_READ(&chip->regmap, MAX77779_FG_IChgTerm, &data);
		if (err)
			return -EIO;
		ichgterm = reg_to_micro_amp(data, chip->RSense);
	}

	err = REGMAP_READ(&chip->regmap, MAX77779_FG_FullSocThr, &data);
	if (err)
		return -EIO;
	fullsocthr = reg_to_percentage(data);

	soc = max77779_fg_get_battery_soc(chip);
	if (soc < 0)
		return -EIO;

	vfsoc = max77779_fg_get_battery_vfsoc(chip);
	if (vfsoc < 0)
		return -EIO;

	if (current_avg > -ichgterm && current_avg <= 0) {

		if (soc >= fullsocthr) {
			const bool needs_prime = (chip->prev_charge_status ==
						  POWER_SUPPLY_STATUS_CHARGING);

			status = POWER_SUPPLY_STATUS_FULL;
			if (needs_prime)
				max77779_fg_prime_battery_qh_capacity(chip,
								   status);
		} else {
			status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		}

	} else if (current_now >= -ichgterm)  {
		status = POWER_SUPPLY_STATUS_DISCHARGING;
	} else {
		status = POWER_SUPPLY_STATUS_CHARGING;
		if (chip->prev_charge_status == POWER_SUPPLY_STATUS_DISCHARGING
		    && current_avg  < -ichgterm)
			max77779_fg_prime_battery_qh_capacity(chip, status);
	}

	if (status != chip->prev_charge_status)
		dev_dbg(chip->dev, "s=%d->%d c=%d avg_c=%d ichgt=%d vfsoc=%d soc=%d fullsocthr=%d\n",
				    chip->prev_charge_status,
				    status, current_now, current_avg,
				    ichgterm, vfsoc, soc, fullsocthr);

	chip->prev_charge_status = status;

	return status;
}

static int max77779_fg_get_battery_health(struct max77779_fg_chip *chip)
{
	/* For health report what ever was recently alerted and clear it */
	/* TODO: 284191042 - Remove this and move to chargin stats */

	if (chip->health_status & MAX77779_FG_Status_Vmx_MASK) {
		chip->health_status &= MAX77779_FG_Status_Vmx_CLEAR;
		return POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	}

	if ((chip->health_status & MAX77779_FG_Status_Tmn_MASK) &&
	    (chip->RConfig & MAX77779_FG_Config_TS_MASK)) {
		chip->health_status &= MAX77779_FG_Status_Tmn_CLEAR;
		return POWER_SUPPLY_HEALTH_COLD;
	}

	if ((chip->health_status & MAX77779_FG_Status_Tmx_MASK) &&
	    (chip->RConfig & MAX77779_FG_Config_TS_MASK)) {
		chip->health_status &= MAX77779_FG_Status_Tmx_CLEAR;
		return POWER_SUPPLY_HEALTH_HOT;
	}

	return POWER_SUPPLY_HEALTH_GOOD;
}

static int max77779_fg_update_battery_qh_based_capacity(struct max77779_fg_chip *chip)
{
	u16 data;
	int current_qh, err = 0;

	err = REGMAP_READ(&chip->regmap, MAX77779_FG_QH, &data);
	if (err)
		return err;

	current_qh = reg_to_twos_comp_int(data);

	/* QH value accumulates as battery charges */
	chip->current_capacity -= (chip->previous_qh - current_qh);
	chip->previous_qh = current_qh;

	return 0;
}

#define EEPROM_CC_OVERFLOW_BIT	BIT(15)
#define MAXIM_CYCLE_COUNT_RESET 655
static void max77779_fg_restore_battery_cycle(struct max77779_fg_chip *chip)
{
	int ret = 0;
	u16 eeprom_cycle, reg_cycle;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_Cycles, &reg_cycle);
	if (ret < 0) {
		dev_info(chip->dev, "Fail to read reg %#x (%d)",
				MAX77779_FG_Cycles, ret);
		return;
	}

	ret = gbms_storage_read(GBMS_TAG_CNHS, &chip->eeprom_cycle,
				sizeof(chip->eeprom_cycle));
	if (ret < 0) {
		dev_info(chip->dev, "Fail to read eeprom cycle count (%d)", ret);
		return;
	}

	if (chip->eeprom_cycle == 0xFFFF) { /* empty storage */
		reg_cycle /= 2;	/* save half value to record over 655 cycles case */
		ret = gbms_storage_write(GBMS_TAG_CNHS, &reg_cycle, sizeof(reg_cycle));
		if (ret < 0)
			dev_info(chip->dev, "Fail to write eeprom cycle (%d)", ret);
		else
			chip->eeprom_cycle = reg_cycle;
		return;
	}

	if (chip->eeprom_cycle & EEPROM_CC_OVERFLOW_BIT)
		chip->cycle_count_offset = MAXIM_CYCLE_COUNT_RESET;

	eeprom_cycle = (chip->eeprom_cycle & 0x7FFF) << 1;
	dev_info(chip->dev, "reg_cycle:%d, eeprom_cycle:%d, update:%c",
		 reg_cycle, eeprom_cycle, eeprom_cycle > reg_cycle ? 'Y' : 'N');
	if (eeprom_cycle > reg_cycle) {
		ret = MAX77779_FG_REGMAP_WRITE_VERIFY(&chip->regmap, MAX77779_FG_Cycles,
																			eeprom_cycle);
		if (ret < 0)
			dev_warn(chip->dev, "fail to update cycles (%d)", ret);
	}
}

static u16 max77779_fg_save_battery_cycle(const struct max77779_fg_chip *chip,
				       u16 reg_cycle)
{
	int ret = 0;
	u16 eeprom_cycle = chip->eeprom_cycle;

	if (chip->por || reg_cycle == 0)
		return eeprom_cycle;

	/* save half value to record over 655 cycles case */
	reg_cycle /= 2;

	/* Over 655 cycles */
	if (reg_cycle < eeprom_cycle)
		reg_cycle |= EEPROM_CC_OVERFLOW_BIT;

	if (reg_cycle <= eeprom_cycle)
		return eeprom_cycle;

	ret = gbms_storage_write(GBMS_TAG_CNHS, &reg_cycle,
				sizeof(reg_cycle));
	if (ret < 0) {
		dev_info(chip->dev, "Fail to write %d eeprom cycle count (%d)", reg_cycle, ret);
	} else {
		dev_info(chip->dev, "update saved cycle:%d -> %d\n", eeprom_cycle, reg_cycle);
		eeprom_cycle = reg_cycle;
	}

	return eeprom_cycle;
}

#define MAX17201_HIST_CYCLE_COUNT_OFFSET	0x4
#define MAX17201_HIST_TIME_OFFSET		0xf

/* WA for cycle count reset.
 * max17201 fuel gauge rolls over the cycle count to 0 and burns
 * an history entry with 0 cycles when the cycle count exceeds
 * 655. This code workaround the issue adding 655 to the cycle
 * count if the fuel gauge history has an entry with 0 cycles and
 * non 0 time-in-field.
 */
static int max77779_fg_get_cycle_count_offset(struct max77779_fg_chip *chip)
{
	int offset = 0;

	if (chip->eeprom_cycle & EEPROM_CC_OVERFLOW_BIT)
		offset = MAXIM_CYCLE_COUNT_RESET;

	return offset;
}

static int max77779_fg_get_cycle_count(struct max77779_fg_chip *chip)
{
	int err, cycle_count;
	u16 reg_cycle;

	/*
	 * Corner case: battery under 3V hit POR without irq.
	 * cycles reset in this situation, incorrect data
	 */
	if (chip->por)
		return -ECANCELED;

	err = REGMAP_READ(&chip->regmap, MAX77779_FG_Cycles, &reg_cycle);
	if (err < 0)
		return err;

	cycle_count = reg_to_cycles((u32)reg_cycle);
	if ((chip->cycle_count == -1) ||
	    ((cycle_count + chip->cycle_count_offset) < chip->cycle_count))
		chip->cycle_count_offset =
			max77779_fg_get_cycle_count_offset(chip);

	chip->eeprom_cycle = max77779_fg_save_battery_cycle(chip, reg_cycle);

	chip->cycle_count = cycle_count + chip->cycle_count_offset;

	if (chip->model_ok && reg_cycle >= chip->model_next_update) {
		err = max77779_fg_set_next_update(chip);
		if (err < 0)
			dev_err(chip->dev, "%s cannot set next update (%d)\n",
				 __func__, err);
	}

	return chip->cycle_count;
}

/* TODO: 284191528 - Add these batt_ce functions to common max file */
/* Capacity Estimation functions*/
static int batt_ce_regmap_read(struct max17x0x_regmap *map,
			       const struct max17x0x_reg *bcea,
			       u32 reg, u16 *data)
{
	int err;
	u16 val;

	if (!bcea)
		return -EINVAL;

	err = REGMAP_READ(map, bcea->map[reg], &val);
	if (err)
		return err;

	switch(reg) {
	case CE_DELTA_CC_SUM_REG:
	case CE_DELTA_VFSOC_SUM_REG:
		*data = val;
		break;
	case CE_CAP_FILTER_COUNT:
		val = val & 0x0F00;
		*data = val >> 8;
		break;
	default:
		break;
	}

	return err;
}

static int batt_ce_regmap_write(struct max17x0x_regmap *map,
				const struct max17x0x_reg *bcea,
				u32 reg, u16 data)
{
	int err = -EINVAL;
	u16 val;

	if (!bcea)
		return -EINVAL;

	switch(reg) {
	case CE_DELTA_CC_SUM_REG:
	case CE_DELTA_VFSOC_SUM_REG:
		err = MAX77779_FG_REGMAP_WRITE(map, bcea->map[reg], data);
		break;
	case CE_CAP_FILTER_COUNT:
		err = REGMAP_READ(map, bcea->map[reg], &val);
		if (err)
			return err;
		val = val & 0xF0FF;
		if (data > CE_FILTER_COUNT_MAX)
			val = val | 0x0F00;
		else
			val = val | (data << 8);
		err = MAX77779_FG_REGMAP_WRITE(map, bcea->map[reg], val);
		break;
	default:
		break;
	}

	return err;
}

static void batt_ce_dump_data(const struct gbatt_capacity_estimation *cap_esti,
			      struct logbuffer *log)
{
	logbuffer_log(log, "cap_filter_count: %d"
			    " start_cc: %d"
			    " start_vfsoc: %d"
			    " delta_cc_sum: %d"
			    " delta_vfsoc_sum: %d"
			    " state: %d"
			    " cable: %d",
			    cap_esti->cap_filter_count,
			    cap_esti->start_cc,
			    cap_esti->start_vfsoc,
			    cap_esti->delta_cc_sum,
			    cap_esti->delta_vfsoc_sum,
			    cap_esti->estimate_state,
			    cap_esti->cable_in);
}

static int batt_ce_load_data(struct max17x0x_regmap *map,
			     struct gbatt_capacity_estimation *cap_esti)
{
	u16 data;
	const struct max17x0x_reg *bcea = cap_esti->bcea;

	cap_esti->estimate_state = ESTIMATE_NONE;
	if (batt_ce_regmap_read(map, bcea, CE_DELTA_CC_SUM_REG, &data) == 0)
		cap_esti->delta_cc_sum = data;
	else
		cap_esti->delta_cc_sum = 0;

	if (batt_ce_regmap_read(map, bcea, CE_DELTA_VFSOC_SUM_REG, &data) == 0)
		cap_esti->delta_vfsoc_sum = data;
	else
		cap_esti->delta_vfsoc_sum = 0;

	if (batt_ce_regmap_read(map, bcea, CE_CAP_FILTER_COUNT, &data) == 0)
		cap_esti->cap_filter_count = data;
	else
		cap_esti->cap_filter_count = 0;
	return 0;
}

/* call holding &cap_esti->batt_ce_lock */
static void batt_ce_store_data(struct max17x0x_regmap *map,
			       struct gbatt_capacity_estimation *cap_esti)
{
	if (cap_esti->cap_filter_count <= CE_FILTER_COUNT_MAX) {
		batt_ce_regmap_write(map, cap_esti->bcea,
					  CE_CAP_FILTER_COUNT,
					  cap_esti->cap_filter_count);
	}

	batt_ce_regmap_write(map, cap_esti->bcea,
				  CE_DELTA_VFSOC_SUM_REG,
				  cap_esti->delta_vfsoc_sum);
	batt_ce_regmap_write(map, cap_esti->bcea,
				  CE_DELTA_CC_SUM_REG,
				  cap_esti->delta_cc_sum);
}

/* call holding &cap_esti->batt_ce_lock */
static void batt_ce_stop_estimation(struct gbatt_capacity_estimation *cap_esti,
				   int reason)
{
	cap_esti->estimate_state = reason;
	cap_esti->start_vfsoc = 0;
	cap_esti->start_cc = 0;
}

static int batt_ce_full_estimate(struct gbatt_capacity_estimation *ce)
{
	return (ce->cap_filter_count > 0) && (ce->delta_vfsoc_sum > 0) ?
		ce->delta_cc_sum / ce->delta_vfsoc_sum : -1;
}

/* Measure the deltaCC, deltaVFSOC and CapacityFiltered */
static void batt_ce_capacityfiltered_work(struct work_struct *work)
{
	struct max77779_fg_chip *chip = container_of(work, struct max77779_fg_chip,
					    cap_estimate.settle_timer.work);
	struct gbatt_capacity_estimation *cap_esti = &chip->cap_estimate;
	int settle_cc = 0, settle_vfsoc = 0;
	int delta_cc = 0, delta_vfsoc = 0;
	int cc_sum = 0, vfsoc_sum = 0;
	bool valid_estimate = false;
	int rc = 0;
	int data;

	mutex_lock(&cap_esti->batt_ce_lock);

	/* race with disconnect */
	if (!cap_esti->cable_in ||
	    cap_esti->estimate_state != ESTIMATE_PENDING) {
		goto exit;
	}

	rc = max77779_fg_update_battery_qh_based_capacity(chip);
	if (rc < 0)
		goto ioerr;

	settle_cc = reg_to_micro_amp_h(chip->current_capacity, chip->RSense, MAX77779_LSB);

	data = max77779_fg_get_battery_vfsoc(chip);
	if (data < 0)
		goto ioerr;

	settle_vfsoc = data;
	settle_cc = settle_cc / 1000;
	delta_cc = settle_cc - cap_esti->start_cc;
	delta_vfsoc = settle_vfsoc - cap_esti->start_vfsoc;

	if ((delta_cc > 0) && (delta_vfsoc > 0)) {

		cc_sum = delta_cc + cap_esti->delta_cc_sum;
		vfsoc_sum = delta_vfsoc + cap_esti->delta_vfsoc_sum;

		if (cap_esti->cap_filter_count >= cap_esti->cap_filt_length) {
			const int filter_divisor = cap_esti->cap_filt_length;

			cc_sum -= cap_esti->delta_cc_sum/filter_divisor;
			vfsoc_sum -= cap_esti->delta_vfsoc_sum/filter_divisor;
		}

		cap_esti->cap_filter_count++;
		cap_esti->delta_cc_sum = cc_sum;
		cap_esti->delta_vfsoc_sum = vfsoc_sum;
		/* batt_ce_store_data(&chip->regmap_nvram, &chip->cap_estimate); */

		valid_estimate = true;
	}

ioerr:
	batt_ce_stop_estimation(cap_esti, ESTIMATE_DONE);

exit:
	logbuffer_log(chip->ce_log,
		"valid=%d settle[cc=%d, vfsoc=%d], delta[cc=%d,vfsoc=%d] ce[%d]=%d",
		valid_estimate,
		settle_cc, settle_vfsoc, delta_cc, delta_vfsoc,
		cap_esti->cap_filter_count,
		batt_ce_full_estimate(cap_esti));

	mutex_unlock(&cap_esti->batt_ce_lock);

	/* force to update uevent to framework side. */
	if (valid_estimate)
		power_supply_changed(chip->psy);
}

/*
 * batt_ce_init(): estimate_state = ESTIMATE_NONE
 * batt_ce_start(): estimate_state = ESTIMATE_NONE -> ESTIMATE_PENDING
 * batt_ce_capacityfiltered_work(): ESTIMATE_PENDING->ESTIMATE_DONE
 */
static int batt_ce_start(struct gbatt_capacity_estimation *cap_esti,
			 int cap_tsettle_ms)
{
	mutex_lock(&cap_esti->batt_ce_lock);

	/* Still has cable and estimate is not pending or cancelled */
	if (!cap_esti->cable_in || cap_esti->estimate_state != ESTIMATE_NONE)
		goto done;

	pr_info("EOC: Start the settle timer\n");
	cap_esti->estimate_state = ESTIMATE_PENDING;
	schedule_delayed_work(&cap_esti->settle_timer,
		msecs_to_jiffies(cap_tsettle_ms));

done:
	mutex_unlock(&cap_esti->batt_ce_lock);
	return 0;
}

static int batt_ce_init(struct gbatt_capacity_estimation *cap_esti,
			struct max77779_fg_chip *chip)
{
	int rc, vfsoc;

	rc = max77779_fg_update_battery_qh_based_capacity(chip);
	if (rc < 0)
		return -EIO;

	vfsoc = max77779_fg_get_battery_vfsoc(chip);
	if (vfsoc < 0)
		return -EIO;

	cap_esti->start_vfsoc = vfsoc;
	cap_esti->start_cc = reg_to_micro_amp_h(chip->current_capacity,
						chip->RSense, MAX77779_LSB) / 1000;
	/* Capacity Estimation starts only when the state is NONE */
	cap_esti->estimate_state = ESTIMATE_NONE;
	return 0;
}

/* TODO b/284191528 - Add to common code file */
/* ------------------------------------------------------------------------- */
static int max77779_fg_health_write_ai(u16 act_impedance, u16 act_timerh)
{
	int ret;

	ret = gbms_storage_write(GBMS_TAG_ACIM, &act_impedance, sizeof(act_impedance));
	if (ret < 0)
		return -EIO;

	ret = gbms_storage_write(GBMS_TAG_THAS, &act_timerh, sizeof(act_timerh));
	if (ret < 0)
		return -EIO;

	return ret;
}

/* TODO b/284191528 - Add to common driver */
/* call holding chip->model_lock */
static int max77779_fg_check_impedance(struct max77779_fg_chip *chip, u16 *th)
{
	struct max17x0x_regmap *map = &chip->regmap;
	int soc, temp, cycle_count, ret;
	u16 data, timerh;

	if (!chip->model_state_valid)
		return -EAGAIN;

	soc = max77779_fg_get_battery_soc(chip);
	if (soc < BHI_IMPEDANCE_SOC_LO || soc > BHI_IMPEDANCE_SOC_HI)
		return -EAGAIN;

	ret = REGMAP_READ(map, MAX77779_FG_Temp, &data);
	if (ret < 0)
		return -EIO;

	temp = reg_to_deci_deg_cel(data);
	if (temp < BHI_IMPEDANCE_TEMP_LO || temp > BHI_IMPEDANCE_TEMP_HI)
		return -EAGAIN;

	cycle_count = max77779_fg_get_cycle_count(chip);
	if (cycle_count < 0)
		return -EINVAL;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_TimerH, &timerh);
	if (ret < 0 || timerh == 0)
		return -EINVAL;

	/* wait for a few cyles and time in field before validating the value */
	if (cycle_count < BHI_IMPEDANCE_CYCLE_CNT || timerh < BHI_IMPEDANCE_TIMERH)
		return -ENODATA;

	*th = timerh;
	return 0;
}

/* TODO b/284191528 - Add to common code file */
/* will return error if the value is not valid  */
static int max77779_fg_health_get_ai(struct max77779_fg_chip *chip)
{
	u16 act_impedance, act_timerh;
	int ret;

	if (chip->bhi_acim != 0)
		return chip->bhi_acim;

	/* read both and recalculate for compatibility */
	ret = gbms_storage_read(GBMS_TAG_ACIM, &act_impedance, sizeof(act_impedance));
	if (ret < 0)
		return -EIO;

	ret = gbms_storage_read(GBMS_TAG_THAS, &act_timerh, sizeof(act_timerh));
	if (ret < 0)
		return -EIO;

	/* need to get starting impedance (if qualified) */
	if (act_impedance == 0xffff || act_timerh == 0xffff)
		return -EINVAL;

	/* not zero, not negative */
	chip->bhi_acim = reg_to_resistance_micro_ohms(act_impedance, chip->RSense);;

	/* TODO: corrrect impedance with timerh */

	dev_info(chip->dev, "%s: chip->bhi_acim =%d act_impedance=%x act_timerh=%x\n",
		 __func__, chip->bhi_acim, act_impedance, act_timerh);

	return chip->bhi_acim;
}

/* TODO b/284191528 - Add to common code file */
/* will return negative if the value is not qualified */
static int max77779_fg_health_read_impedance(struct max77779_fg_chip *chip)
{
	u16 timerh;
	int ret;

	ret = max77779_fg_check_impedance(chip, &timerh);
	if (ret < 0)
		return -EINVAL;

	return max77779_fg_read_resistance(chip);
}

/* in hours */
static int max77779_fg_get_age(struct max77779_fg_chip *chip)
{
	u16 timerh;
	int ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_TimerH, &timerh);
	if (ret < 0 || timerh == 0)
		return -ENODATA;

	return reg_to_time_hr(timerh, chip);
}

/* TODO b/284191528 - Add to common code file */
#define MAX_HIST_FULLCAP	0x3FF
static int max77779_fg_get_fade_rate(struct max77779_fg_chip *chip)
{
	struct max77779_fg_eeprom_history hist = { 0 };
	int bhi_fcn_count = chip->bhi_fcn_count;
	int ret, ratio, i, fcn_sum = 0;
	u16 hist_idx;

	ret = gbms_storage_read(GBMS_TAG_HCNT, &hist_idx, sizeof(hist_idx));
	if (ret < 0) {
		dev_err(chip->dev, "failed to get history index (%d)\n", ret);
		return -EIO;
	}

	dev_info(chip->dev, "%s: hist_idx=%d\n", __func__, hist_idx);

	/* no fade for new battery (less than 30 cycles) */
	if (hist_idx < bhi_fcn_count)
		return 0;

	while (hist_idx >= BATT_MAX_HIST_CNT && bhi_fcn_count > 1) {
		hist_idx--;
		bhi_fcn_count--;
		if (bhi_fcn_count == 1) {
			hist_idx = BATT_MAX_HIST_CNT - 1;
			break;
		}
	}

	for (i = bhi_fcn_count; i ; i--, hist_idx--) {
		ret = gbms_storage_read_data(GBMS_TAG_HIST, &hist,
					     sizeof(hist), hist_idx);

		dev_info(chip->dev, "%s: idx=%d hist.fc=%d (%x) ret=%d\n", __func__,
			hist_idx, hist.fullcapnom, hist.fullcapnom, ret);

		if (ret < 0)
			return -EINVAL;

		/* hist.fullcapnom = fullcapnom * 800 / designcap */
		fcn_sum += hist.fullcapnom;
	}

	/* convert from max77779_fg_eeprom_history to percent */
	ratio = fcn_sum / (bhi_fcn_count * 8);
	if (ratio > 100)
		ratio = 100;

	return 100 - ratio;
}


static int max77779_fg_get_property(struct power_supply *psy,
				 enum power_supply_property psp,
				 union power_supply_propval *val)
{
	struct max77779_fg_chip *chip = (struct max77779_fg_chip *)
					power_supply_get_drvdata(psy);
	struct max17x0x_regmap *map = &chip->regmap;
	int rc, err = 0;
	u16 data = 0;
	int idata;

	mutex_lock(&chip->model_lock);

	pm_runtime_get_sync(chip->dev);
	if (!chip->init_complete || !chip->resume_complete) {
		pm_runtime_put_sync(chip->dev);
		mutex_unlock(&chip->model_lock);
		return -EAGAIN;
	}
	pm_runtime_put_sync(chip->dev);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		err = max77779_fg_get_battery_status(chip);
		if (err < 0)
			break;

		/*
		 * Capacity estimation must run only once.
		 * NOTE: this is a getter with a side effect
		 */
		val->intval = err;
		if (err == POWER_SUPPLY_STATUS_FULL)
			batt_ce_start(&chip->cap_estimate,
				      chip->cap_estimate.cap_tsettle);
		/* return data ok */
		err = 0;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = max77779_fg_get_battery_health(chip);
		break;
	case GBMS_PROP_CAPACITY_RAW:
		err = max77779_fg_get_capacity_raw(chip, &data);
		if (err == 0)
			val->intval = (int)data;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		idata = max77779_fg_get_battery_soc(chip);
		if (idata < 0) {
			err = idata;
			break;
		}

		val->intval = idata;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		err = max77779_fg_update_battery_qh_based_capacity(chip);
		if (err < 0)
			break;

		val->intval = reg_to_capacity_uah(chip->current_capacity, chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		/*
		 * Snap charge_full to DESIGNCAP during early charge cycles to
		 * prevent large fluctuations in FULLCAPNOM. MAX77779_FG_Cycles LSB
		 * is 1%
		 */
		err = max77779_fg_get_cycle_count(chip);
		if (err < 0)
			break;

		/* err is cycle_count */
		if (err <= FULLCAPNOM_STABILIZE_CYCLES)
			err = REGMAP_READ(map, MAX77779_FG_DesignCap, &data);
		else
			err = REGMAP_READ(map, MAX77779_FG_FullCapNom, &data);

		if (err == 0)
			val->intval = reg_to_capacity_uah(data, chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		err = REGMAP_READ(map, MAX77779_FG_DesignCap, &data);
		if (err == 0)
			val->intval = reg_to_capacity_uah(data, chip);
		break;
	/* current is positive value when flowing to device */
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		err = REGMAP_READ(map, MAX77779_FG_AvgCurrent, &data);
		if (err == 0)
			val->intval = -reg_to_micro_amp(data, chip->RSense);
		break;
	/* current is positive value when flowing to device */
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		err = REGMAP_READ(map, MAX77779_FG_Current, &data);
		if (err == 0)
			val->intval = -reg_to_micro_amp(data, chip->RSense);
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		err = max77779_fg_get_cycle_count(chip);
		if (err < 0)
			break;
		/* err is cycle_count */
		val->intval = err;
		/* return data ok */
		err = 0;
		break;
	case POWER_SUPPLY_PROP_PRESENT:

		if (chip->fake_battery != -1) {
			val->intval = chip->fake_battery;
		} else if (chip->gauge_type == -1) {
			val->intval = 0;
		} else {

			err = REGMAP_READ(map, MAX77779_FG_Status, &data);
			if (err < 0)
				break;

			/* BST is 0 when the battery is present */
			val->intval = !(data & MAX77779_FG_Status_Bst_MASK);
			if (!val->intval)
				break;

			/* chip->por prevent garbage in cycle count */
			chip->por = (data & MAX77779_FG_Status_PONR_MASK) != 0;
			if (chip->por && chip->model_ok &&
			    chip->model_reload != MAX77779_LOAD_MODEL_REQUEST) {
				/* trigger reload model and clear of POR */
				mutex_unlock(&chip->model_lock);
				max77779_fg_irq_thread_fn(-1, chip);
				return err;
			}
		}
		break;
	case POWER_SUPPLY_PROP_TEMP:
		err = REGMAP_READ(map, MAX77779_FG_Temp, &data);
		if (err < 0)
			break;

		val->intval = reg_to_deci_deg_cel(data);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		err = REGMAP_READ(map, MAX77779_FG_TTE, &data);
		if (err == 0)
			val->intval = reg_to_seconds(data);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
		err = REGMAP_READ(map, MAX77779_FG_TTF, &data);
		if (err == 0)
			val->intval = reg_to_seconds(data);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		val->intval = -1;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		err = REGMAP_READ(map, MAX77779_FG_AvgVCell, &data);
		if (err == 0)
			val->intval = reg_to_micro_volt(data);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		/* LSB: 20mV */
		err = REGMAP_READ(map, MAX77779_FG_MaxMinVolt, &data);
		if (err == 0)
			val->intval = ((data >> 8) & 0xFF) * 20000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		/* LSB: 20mV */
		err = REGMAP_READ(map, MAX77779_FG_MaxMinVolt, &data);
		if (err == 0)
			val->intval = (data & 0xFF) * 20000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		err = REGMAP_READ(map, MAX77779_FG_VCell, &data);
		if (err == 0)
			val->intval = reg_to_micro_volt(data);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		rc = REGMAP_READ(map, MAX77779_FG_VFOCV, &data);
		if (rc == -EINVAL) {
			val->intval = -1;
			break;
		}
		val->intval = reg_to_micro_volt(data);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		val->strval = chip->serial_number;
		break;
	case GBMS_PROP_HEALTH_ACT_IMPEDANCE:
		val->intval = max77779_fg_health_get_ai(chip);
		break;
	case GBMS_PROP_HEALTH_IMPEDANCE:
		val->intval = max77779_fg_health_read_impedance(chip);
		break;
	case GBMS_PROP_RESISTANCE:
		val->intval = max77779_fg_read_resistance(chip);
		break;
	case GBMS_PROP_RESISTANCE_RAW:
		val->intval = max77779_fg_read_resistance_raw(chip);
		break;
	case GBMS_PROP_RESISTANCE_AVG:
		val->intval = max77779_fg_read_resistance_avg(chip);
		break;
	case GBMS_PROP_BATTERY_AGE:
		val->intval = max77779_fg_get_age(chip);
		break;
	case GBMS_PROP_CHARGE_FULL_ESTIMATE:
		val->intval = batt_ce_full_estimate(&chip->cap_estimate);
		break;
	case GBMS_PROP_CAPACITY_FADE_RATE:
		val->intval = max77779_fg_get_fade_rate(chip);
		break;
	default:
		err = -EINVAL;
		break;
	}

	if (err < 0)
		pr_debug("error %d reading prop %d\n", err, psp);

	mutex_unlock(&chip->model_lock);
	return err;
}

/* needs mutex_lock(&chip->model_lock); */
static int max77779_fg_health_update_ai(struct max77779_fg_chip *chip, int impedance)
{
	const u16 act_impedance = impedance / 100;
	unsigned int rcell = 0xffff;
	u16 timerh = 0xffff;
	int ret;

	if (impedance) {

		/* mOhms to reg */
		rcell = (impedance * 4096) / (1000 * chip->RSense);
		if (rcell > 0xffff) {
			pr_err("value=%d, rcell=%d out of bounds\n", impedance, rcell);
			return -ERANGE;
		}

		ret = REGMAP_READ(&chip->regmap, MAX77779_FG_TimerH, &timerh);
		if (ret < 0 || timerh == 0)
			return -EIO;
	}

	ret = max77779_fg_health_write_ai(act_impedance, timerh);
	if (ret == 0)
		chip->bhi_acim = 0;

	return ret;
}

static int max77779_fg_set_property(struct power_supply *psy,
				 enum power_supply_property psp,
				 const union power_supply_propval *val)
{
	struct max77779_fg_chip *chip = (struct max77779_fg_chip *)
					power_supply_get_drvdata(psy);
	struct gbatt_capacity_estimation *ce = &chip->cap_estimate;
	int rc = 0;

	mutex_lock(&chip->model_lock);
	pm_runtime_get_sync(chip->dev);
	if (!chip->init_complete || !chip->resume_complete) {
		pm_runtime_put_sync(chip->dev);
		mutex_unlock(&chip->model_lock);
		return -EAGAIN;
	}
	pm_runtime_put_sync(chip->dev);
	mutex_unlock(&chip->model_lock);

	switch (psp) {
	case GBMS_PROP_BATT_CE_CTRL:

		mutex_lock(&ce->batt_ce_lock);

		if (!chip->model_state_valid) {
			mutex_unlock(&ce->batt_ce_lock);
			return -EAGAIN;
		}

		if (val->intval) {

			if (!ce->cable_in) {
				rc = batt_ce_init(ce, chip);
				ce->cable_in = (rc == 0);
			}

		} else if (ce->cable_in) {
			if (ce->estimate_state == ESTIMATE_PENDING)
				cancel_delayed_work_sync(&ce->settle_timer);

			/* race with batt_ce_capacityfiltered_work() */
			batt_ce_stop_estimation(ce, ESTIMATE_NONE);
			batt_ce_dump_data(ce, chip->ce_log);
			ce->cable_in = false;
		}
		mutex_unlock(&ce->batt_ce_lock);

		mod_delayed_work(system_wq, &chip->model_work, msecs_to_jiffies(351));

		break;
	case GBMS_PROP_HEALTH_ACT_IMPEDANCE:
		mutex_lock(&chip->model_lock);
		rc = max77779_fg_health_update_ai(chip, val->intval);
		mutex_unlock(&chip->model_lock);
		break;
	case GBMS_PROP_FG_REG_LOGGING:
		max77779_fg_monitor_log_data(chip, !!val->intval);
		break;
	default:
		return -EINVAL;
	}

	if (rc < 0)
		return rc;

	return 0;
}

static int max77779_fg_property_is_writeable(struct power_supply *psy,
					  enum power_supply_property psp)
{
	switch (psp) {
	case GBMS_PROP_BATT_CE_CTRL:
	case GBMS_PROP_HEALTH_ACT_IMPEDANCE:
		return 1;
	default:
		break;
	}

	return 0;
}

/* TODO: b/284191528 - Add to common code file, take in array of registers as input */
static int max77779_fg_monitor_log_data(struct max77779_fg_chip *chip, bool force_log)
{
	u16 data, repsoc, vfsoc, avcap, repcap, fullcap, fullcaprep;
	u16 fullcapnom, qh0, qh, dqacc, dpacc, qresidual, fstat;
	u16 learncfg, tempco, mixcap, vfremcap, vcell, ibat;
	int ret = 0, charge_counter = -1;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_RepSOC, &data);
	if (ret < 0)
		return ret;

	repsoc = (data >> 8) & 0x00FF;
	if (repsoc == chip->pre_repsoc && !force_log)
		return ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_VFSOC, &vfsoc);
	if (ret < 0)
		return ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_AvCap, &avcap);
	if (ret < 0)
		return ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_RepCap, &repcap);
	if (ret < 0)
		return ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_FullCap, &fullcap);
	if (ret < 0)
		return ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_FullCapRep, &fullcaprep);
	if (ret < 0)
		return ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_FullCapNom, &fullcapnom);
	if (ret < 0)
		return ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_QH0, &qh0);
	if (ret < 0)
		return ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_QH, &qh);
	if (ret < 0)
		return ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_dQAcc, &dqacc);
	if (ret < 0)
		return ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_dPAcc, &dpacc);
	if (ret < 0)
		return ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_QResidual, &qresidual);
	if (ret < 0)
		return ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_FStat, &fstat);
	if (ret < 0)
		return ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_LearnCfg, &learncfg);
	if (ret < 0)
		return ret;

	ret = REGMAP_READ(&chip->regmap_debug, MAX77779_FG_DBG_nTempCo, &tempco);
	if (ret < 0)
		return ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_MixCap, &mixcap);
	if (ret < 0)
		return ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_VFRemCap, &vfremcap);
	if (ret < 0)
		return ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_VCell, &vcell);
	if (ret < 0)
		return ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_Current, &ibat);
	if (ret < 0)
		return ret;

	ret = max77779_fg_update_battery_qh_based_capacity(chip);
	if (ret == 0)
		charge_counter = reg_to_capacity_uah(chip->current_capacity, chip);

	gbms_logbuffer_prlog(chip->monitor_log, LOGLEVEL_INFO, 0, LOGLEVEL_INFO,
			     "%s %02X:%04X %02X:%04X %02X:%04X %02X:%04X %02X:%04X"
			     " %02X:%04X %02X:%04X %02X:%04X %02X:%04X %02X:%04X %02X:%04X"
			     " %02X:%04X %02X:%04X %02X:%04X %02X:%04X %02X:%04X %02X:%04X"
			     " %02X:%04X %02X:%04X CC:%d",
			     chip->max77779_fg_psy_desc.name, MAX77779_FG_RepSOC, data, MAX77779_FG_VFSOC,
			     vfsoc, MAX77779_FG_AvCap, avcap, MAX77779_FG_RepCap, repcap,
			     MAX77779_FG_FullCap, fullcap, MAX77779_FG_FullCapRep, fullcaprep,
			     MAX77779_FG_FullCapNom, fullcapnom, MAX77779_FG_QH0, qh0,
			     MAX77779_FG_QH, qh, MAX77779_FG_dQAcc, dqacc, MAX77779_FG_dPAcc, dpacc,
			     MAX77779_FG_QResidual, qresidual, MAX77779_FG_FStat, fstat,
			     MAX77779_FG_LearnCfg, learncfg, MAX77779_FG_DBG_nTempCo, tempco,
			     MAX77779_FG_MixCap, mixcap, MAX77779_FG_VFRemCap, vfremcap,
			     MAX77779_FG_VCell, vcell, MAX77779_FG_Current, ibat, charge_counter);

	chip->pre_repsoc = repsoc;

	return ret;
}

/*
 * A full reset restores the ICs to their power-up state the same as if power
 * had been cycled.
 */
static int max77779_fg_full_reset(struct max77779_fg_chip *chip)
{
	/* TODO: b/283488742 - porting reset command */
	/* REGMAP_WRITE(&chip->regmap, MAX17XXX_COMMAND,
		     max77779_fg_COMMAND_HARDWARE_RESET); */

	msleep(MAX77779_FG_TPOR_MS);

	return 0;
}

static irqreturn_t max77779_fg_irq_thread_fn(int irq, void *obj)
{
	struct max77779_fg_chip *chip = (struct max77779_fg_chip *)obj;
	u16 fg_status, fg_status_clr;
	int err = 0;

	if (!chip || (irq != -1 && irq != chip->primary->irq)) {
		WARN_ON_ONCE(1);
		return IRQ_NONE;
	}

	if (chip->gauge_type == -1)
		return IRQ_NONE;

	pm_runtime_get_sync(chip->dev);
	if (!chip->init_complete || !chip->resume_complete) {
		if (chip->init_complete && !chip->irq_disabled) {
			chip->irq_disabled = true;
			disable_irq_nosync(chip->primary->irq);
		}
		pm_runtime_put_sync(chip->dev);
		return IRQ_HANDLED;
	}

	pm_runtime_put_sync(chip->dev);

	err = REGMAP_READ(&chip->regmap, MAX77779_FG_Status, &fg_status);
	if (err)
		return IRQ_NONE;

	if (fg_status == 0) {
		/*
		 * Disable rate limiting for when interrupt is shared.
		 * NOTE: this might need to be re-evaluated at some later point
		 */
		return IRQ_NONE;
	}

	/* only used to report health */
	chip->health_status |= fg_status;

	/*
	 * write 0 to clear will loose interrupts when we don't write 1 to the
	 * bits that are not set. Oonly setting the bits marked as "host must clear"
	 * in the DS seems to work eg:
	 *
	 * fg_status_clr = fg_status
	 * fg_status_clr |= MAX77779_FG_Status_PONR_MASK | MAX77779_FG_Status_dSOCi_MASK
	 *                | MAX77779_FG_Status_Bi_MASK;
	 *
	 * If the above logic is sound, we probably need to set also the bits
	 * that config mark as "host must clear". Maxim to confirm.
	 */
	fg_status_clr = fg_status;

	if (fg_status & MAX77779_FG_Status_PONR_MASK) {
		const bool no_battery = chip->fake_battery == 0;

		mutex_lock(&chip->model_lock);
		chip->por = true;
		if (no_battery) {
			fg_status_clr &= MAX77779_FG_Status_PONR_CLEAR;
		} else {
			dev_warn(chip->dev, "POR is set(%04x), model reload:%d\n",
				 fg_status, chip->model_reload);
			/* trigger model load if not on-going */
			if (chip->model_reload != MAX77779_LOAD_MODEL_REQUEST) {
				/* TODO: implement model loading when spec ready b/271044091 */
				/* err = max77779_fg_model_reload(chip, true);
				if (err < 0) */
					fg_status_clr &= MAX77779_FG_Status_PONR_CLEAR;
			}
		}
		mutex_unlock(&chip->model_lock);
	}

	if (fg_status & MAX77779_FG_Status_Imn_MASK)
		pr_debug("IMN is set\n");

	if (fg_status & MAX77779_FG_Status_Bst_MASK)
		pr_debug("BST is set\n");

	if (fg_status & MAX77779_FG_Status_Imx_MASK)
		pr_debug("IMX is set\n");

	if (fg_status & MAX77779_FG_Status_dSOCi_MASK) {
		fg_status_clr &= MAX77779_FG_Status_dSOCi_CLEAR;
		pr_debug("DSOCI is set\n");
	}
	if (fg_status & MAX77779_FG_Status_Vmn_MASK) {
		if (chip->RConfig & MAX77779_FG_Config_VS_MASK)
			fg_status_clr &= MAX77779_FG_Status_Vmn_CLEAR;
		pr_debug("VMN is set\n");
	}
	if (fg_status & MAX77779_FG_Status_Tmn_MASK) {
		if (chip->RConfig & MAX77779_FG_Config_TS_MASK)
			fg_status_clr &= MAX77779_FG_Status_Tmn_CLEAR;
		pr_debug("TMN is set\n");
	}
	if (fg_status & MAX77779_FG_Status_Smn_MASK) {
		if (chip->RConfig & MAX77779_FG_Config_SS_MASK)
			fg_status_clr &= MAX77779_FG_Status_Smn_CLEAR;
		pr_debug("SMN is set\n");
	}
	if (fg_status & MAX77779_FG_Status_Bi_MASK)
		pr_debug("BI is set\n");

	if (fg_status & MAX77779_FG_Status_Vmx_MASK) {
		if (chip->RConfig & MAX77779_FG_Config_VS_MASK)
			fg_status_clr &= MAX77779_FG_Status_Vmx_CLEAR;
		pr_debug("VMX is set\n");
	}
	if (fg_status & MAX77779_FG_Status_Tmx_MASK) {
		if (chip->RConfig & MAX77779_FG_Config_TS_MASK)
			fg_status_clr &= MAX77779_FG_Status_Tmx_CLEAR;
		pr_debug("TMX is set\n");
	}
	if (fg_status & MAX77779_FG_Status_Smx_MASK) {
		if (chip->RConfig & MAX77779_FG_Config_SS_MASK)
			fg_status_clr &= MAX77779_FG_Status_Smx_CLEAR;
		pr_debug("SMX is set\n");
	}

	if (fg_status & MAX77779_FG_Status_Br_MASK)
		pr_debug("BR is set\n");

	/* NOTE: should always clear everything even if we lose state */
	MAX77779_FG_REGMAP_WRITE(&chip->regmap, MAX77779_FG_Status, fg_status_clr);

	/* SOC interrupts need to go through all the time */
	if (fg_status & MAX77779_FG_Status_dSOCi_MASK)
		max77779_fg_monitor_log_data(chip, false);

	if (chip->psy)
		power_supply_changed(chip->psy);

	/*
	 * oneshot w/o filter will unmask on return but gauge will take up
	 * to 351 ms to clear ALRM1.
	 * NOTE: can do this masking on gauge side (Config, 0x1D) and using a
	 * workthread to re-enable.
	 */
	if (irq != -1)
		msleep(MAX77779_FG_TICLR_MS);


	return IRQ_HANDLED;
}

/* used to find batt_node and chemistry dependent FG overrides */
static int max77779_fg_read_batt_id(int *batt_id, const struct max77779_fg_chip *chip)
{
	bool defer;
	int rc = 0;
	struct device_node *node = chip->dev->of_node;
	u32 temp_id = 0;

	/* force the value in kohm */
	rc = of_property_read_u32(node, "max77779,force-batt-id", &temp_id);
	if (rc == 0) {
		dev_warn(chip->dev, "forcing battery RID %d\n", temp_id);
		*batt_id = temp_id;
		return 0;
	}

	/* return the value in kohm */
	rc = gbms_storage_read(GBMS_TAG_BRID, &temp_id, sizeof(temp_id));
	defer = (rc == -EPROBE_DEFER) ||
		(rc == -EINVAL) ||
		((rc == 0) && (temp_id == -EINVAL));
	if (defer)
		return -EPROBE_DEFER;

	if (rc < 0) {
		dev_err(chip->dev, "failed to get batt-id rc=%d\n", rc);
		*batt_id = -1;
		return -EPROBE_DEFER;
	}

	*batt_id = temp_id;
	return 0;
}

static struct device_node *max77779_fg_find_batt_node(struct max77779_fg_chip *chip)
{
	const int batt_id = chip->batt_id;
	const struct device *dev = chip->dev;
	struct device_node *config_node, *child_node;
	u32 batt_id_kohm;
	int ret;

	config_node = of_find_node_by_name(dev->of_node, "max77779,config");
	if (!config_node) {
		dev_warn(dev, "Failed to find max77779,config setting\n");
		return NULL;
	}

	for_each_child_of_node(config_node, child_node) {
		ret = of_property_read_u32(child_node, "max77779,batt-id-kohm", &batt_id_kohm);
		if (ret != 0)
			continue;

		if (batt_id == batt_id_kohm)
			return child_node;
	}

	return NULL;
}

static int get_irq_none_cnt(void *data, u64 *val)
{
	struct max77779_fg_chip *chip = (struct max77779_fg_chip *)data;

	*val = chip->debug_irq_none_cnt;
	return 0;
}

static int set_irq_none_cnt(void *data, u64 val)
{
	struct max77779_fg_chip *chip = (struct max77779_fg_chip *)data;

	if (val == 0)
		chip->debug_irq_none_cnt = 0;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(irq_none_cnt_fops, get_irq_none_cnt,
			set_irq_none_cnt, "%llu\n");


static int debug_fg_reset(void *data, u64 val)
{
	struct max77779_fg_chip *chip = data;
	int ret;

	if (val == 1)
		ret = max77779_fg_full_reset(chip);
	else
		ret = -EINVAL;

	return ret;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_fg_reset_fops, NULL, debug_fg_reset, "%llu\n");

static int debug_ce_start(void *data, u64 val)
{
	struct max77779_fg_chip *chip = (struct max77779_fg_chip *)data;

	batt_ce_start(&chip->cap_estimate, val);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_ce_start_fops, NULL, debug_ce_start, "%llu\n");

/* Model reload will be disabled if the node is not found */
static int max77779_fg_init_model(struct max77779_fg_chip *chip)
{
	const bool no_battery = chip->fake_battery == 0;
	void *model_data;

	if (no_battery)
		return 0;

	/* ->batt_id negative for no lookup */
	if (chip->batt_id >= 0) {
		chip->batt_node = max77779_fg_find_batt_node(chip);
		pr_debug("node found=%d for ID=%d\n",
			 !!chip->batt_node, chip->batt_id);
	}

	/* TODO: split allocation and initialization */
	model_data = max77779_init_data(chip->dev, chip->batt_node ?
					chip->batt_node : chip->dev->of_node,
					&chip->regmap);
	if (IS_ERR(model_data))
		return PTR_ERR(model_data);

	chip->model_data = model_data;

	if (!chip->batt_node) {
		dev_warn(chip->dev, "No child node for ID=%d\n", chip->batt_id);
		chip->model_reload = MAX77779_LOAD_MODEL_DISABLED;
	} else {
		pr_debug("model_data ok for ID=%d\n", chip->batt_id);
		chip->model_reload = MAX77779_LOAD_MODEL_IDLE;
	}

	return 0;
}

/* change battery_id and cause reload of the FG model */
static int debug_batt_id_set(void *data, u64 val)
{
	struct max77779_fg_chip *chip = (struct max77779_fg_chip *)data;
	int ret;

	mutex_lock(&chip->model_lock);

	/* reset state (if needed) */
	if (chip->model_data)
		max77779_free_data(chip->model_data);
	chip->batt_id = val;

	/* re-init the model data (lookup in DT) */
	ret = max77779_fg_init_model(chip);
	if (ret == 0)
		max77779_fg_model_reload(chip, true);

	mutex_unlock(&chip->model_lock);

	dev_info(chip->dev, "Force model for batt_id=%llu (%d)\n", val, ret);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_batt_id_fops, NULL, debug_batt_id_set, "%llu\n");

static int debug_fake_battery_set(void *data, u64 val)
{
	struct max77779_fg_chip *chip = (struct max77779_fg_chip *)data;

	chip->fake_battery = (int)val;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_fake_battery_fops, NULL,
			debug_fake_battery_set, "%llu\n");

static void max77779_fg_reglog_dump(struct max17x0x_reglog *regs,
				    size_t size, char *buff)
{
	int i, len = 0;

	for (i = 0; i < NB_REGMAP_MAX; i++) {
		if (size <= len)
			break;
		if (test_bit(i, regs->valid))
			len += scnprintf(&buff[len], size - len, "%02X:%04X\n",
					 i, regs->data[i]);
	}

	if (len == 0)
		scnprintf(buff, size, "No record\n");
}

static ssize_t debug_get_reglog_writes(struct file *filp, char __user *buf,
				       size_t count, loff_t *ppos)
{
	char *buff;
	ssize_t rc = 0;
	struct max17x0x_reglog *reglog = (struct max17x0x_reglog *)filp->private_data;

	buff = kmalloc(count, GFP_KERNEL);
	if (!buff)
		return -ENOMEM;

	max77779_fg_reglog_dump(reglog, count, buff);
	rc = simple_read_from_buffer(buf, count, ppos, buff, strlen(buff));

	kfree(buff);

	return rc;
}

BATTERY_DEBUG_ATTRIBUTE(debug_reglog_writes_fops,
			debug_get_reglog_writes, NULL);

static int debug_sync_model(void *data, u64 val)
{
	struct max77779_fg_chip *chip = data;
	int ret;

	if (!chip->model_data)
		return -EINVAL;

	/* re-read new state from Fuel gauge, save to storage  */
	ret = max77779_model_read_state(chip->model_data);
	if (ret == 0) {
		ret = max77779_model_check_state(chip->model_data);
		if (ret < 0)
			pr_warn("%s: warning invalid state %d\n", __func__, ret);

		ret = max77779_save_state_data(chip->model_data);
	}

	return ret;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_sync_model_fops, NULL, debug_sync_model, "%llu\n");


static ssize_t max77779_fg_show_debug_data(struct file *filp, char __user *buf,
					   size_t count, loff_t *ppos)
{
	struct max77779_fg_chip *chip = (struct max77779_fg_chip *)filp->private_data;
	char msg[8];
	u16 data;
	int ret;

	ret = REGMAP_READ(&chip->regmap, chip->debug_reg_address, &data);
	if (ret < 0)
		return ret;

	ret = scnprintf(msg, sizeof(msg), "%x\n", data);

	return simple_read_from_buffer(buf, count, ppos, msg, ret);
}

static ssize_t max77779_fg_set_debug_data(struct file *filp,
					  const char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct max77779_fg_chip *chip = (struct max77779_fg_chip *)filp->private_data;
	char temp[8] = { };
	u16 data;
	int ret;

	ret = simple_write_to_buffer(temp, sizeof(temp) - 1, ppos, user_buf, count);
	if (!ret)
		return -EFAULT;

	ret = kstrtou16(temp, 16, &data);
	if (ret < 0)
		return ret;

	ret =  MAX77779_FG_REGMAP_WRITE(&chip->regmap, chip->debug_reg_address, data);
	if (ret < 0)
		return ret;

	return count;
}

BATTERY_DEBUG_ATTRIBUTE(debug_reg_data_fops, max77779_fg_show_debug_data,
			max77779_fg_set_debug_data);

static ssize_t max77779_fg_show_dbg_debug_data(struct file *filp, char __user *buf,
					   size_t count, loff_t *ppos)
{
	struct max77779_fg_chip *chip = (struct max77779_fg_chip *)filp->private_data;
	char msg[8];
	u16 data;
	int ret;

	ret = REGMAP_READ(&chip->regmap_debug, chip->debug_dbg_reg_address, &data);
	if (ret < 0)
		return ret;

	ret = scnprintf(msg, sizeof(msg), "%x\n", data);

	return simple_read_from_buffer(buf, count, ppos, msg, ret);
}

static ssize_t max77779_fg_set_dbg_debug_data(struct file *filp,
					  const char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct max77779_fg_chip *chip = (struct max77779_fg_chip *)filp->private_data;
	char temp[8] = { };
	u16 data;
	int ret;

	ret = simple_write_to_buffer(temp, sizeof(temp) - 1, ppos, user_buf, count);
	if (!ret)
		return -EFAULT;

	ret = kstrtou16(temp, 16, &data);
	if (ret < 0)
		return ret;

	ret =  MAX77779_FG_REGMAP_WRITE(&chip->regmap_debug, chip->debug_dbg_reg_address, data);
	if (ret < 0)
		return ret;

	return count;
}

BATTERY_DEBUG_ATTRIBUTE(debug_reg_dbg_data_fops, max77779_fg_show_dbg_debug_data,
			max77779_fg_set_dbg_debug_data);

static ssize_t max77779_fg_show_reg_all(struct file *filp, char __user *buf,
					size_t count, loff_t *ppos)
{
	struct max77779_fg_chip *chip = (struct max77779_fg_chip *)filp->private_data;
	const struct max17x0x_regmap *map = &chip->regmap;
	u32 reg_address;
	unsigned int data;
	char *tmp;
	int ret = 0, len = 0;

	if (!map->regmap) {
		pr_err("Failed to read, no regmap\n");
		return -EIO;
	}

	tmp = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	for (reg_address = 0; reg_address <= 0xFF; reg_address++) {
		ret = regmap_read(map->regmap, reg_address, &data);
		if (ret < 0)
			continue;

		len += scnprintf(tmp + len, PAGE_SIZE - len, "%02x: %04x\n", reg_address, data);
	}

	if (len > 0)
		len = simple_read_from_buffer(buf, count,  ppos, tmp, strlen(tmp));

	kfree(tmp);

	return len;
}

BATTERY_DEBUG_ATTRIBUTE(debug_reg_all_fops, max77779_fg_show_reg_all, NULL);

static ssize_t max77779_fg_show_dbg_reg_all(struct file *filp, char __user *buf,
					size_t count, loff_t *ppos)
{
	struct max77779_fg_chip *chip = (struct max77779_fg_chip *)filp->private_data;
	const struct max17x0x_regmap *map = &chip->regmap_debug;
	u32 reg_address;
	unsigned int data;
	char *tmp;
	int ret = 0, len = 0;

	if (!map->regmap) {
		pr_err("Failed to read, no regmap\n");
		return -EIO;
	}

	tmp = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	for (reg_address = 0; reg_address <= 0xFF; reg_address++) {
		ret = regmap_read(map->regmap, reg_address, &data);
		if (ret < 0)
			continue;

		len += scnprintf(tmp + len, PAGE_SIZE - len, "%02x: %04x\n", reg_address, data);
	}

	if (len > 0)
		len = simple_read_from_buffer(buf, count,  ppos, tmp, strlen(tmp));

	kfree(tmp);

	return len;
}

BATTERY_DEBUG_ATTRIBUTE(debug_reg_all_dbg_fops, max77779_fg_show_dbg_reg_all, NULL);

static ssize_t max77779_fg_force_psy_update(struct file *filp,
					    const char __user *user_buf,
					    size_t count, loff_t *ppos)
{
	struct max77779_fg_chip *chip = (struct max77779_fg_chip *)filp->private_data;

	if (chip->psy)
		power_supply_changed(chip->psy);

	return count;
}

BATTERY_DEBUG_ATTRIBUTE(debug_force_psy_update_fops, NULL,
			max77779_fg_force_psy_update);

static int debug_cnhs_reset(void *data, u64 val)
{
	struct max77779_fg_chip *chip = data;
	u16 reset_val;
	int ret;

	reset_val = (u16)val;

	ret = gbms_storage_write(GBMS_TAG_CNHS, &reset_val,
				sizeof(reset_val));
	dev_info(chip->dev, "reset CNHS to %d, (ret=%d)\n", reset_val, ret);

	return ret > 0 ? 0 : ret;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_reset_cnhs_fops, NULL, debug_cnhs_reset, "%llu\n");

static int debug_gmsr_reset(void *data, u64 val)
{
	struct max77779_fg_chip *chip = data;
	int ret;

	ret = max77779_reset_state_data(chip->model_data);
	dev_info(chip->dev, "reset GMSR (ret=%d)\n", ret);

	return ret > 0 ? 0 : ret;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_reset_gmsr_fops, NULL, debug_gmsr_reset, "%llu\n");

/*
 * TODO: add the building blocks of google capacity
 *
 * case POWER_SUPPLY_PROP_DELTA_CC_SUM:
 *	val->intval = chip->cap_estimate.delta_cc_sum;
 *	break;
 * case POWER_SUPPLY_PROP_DELTA_VFSOC_SUM:
 *	val->intval = chip->cap_estimate.delta_vfsoc_sum;
 *	break;
 */

static ssize_t act_impedance_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count) {
	struct power_supply *psy = container_of(dev, struct power_supply, dev);
	struct max77779_fg_chip *chip = power_supply_get_drvdata(psy);
	int value, ret = 0;

	ret = kstrtoint(buf, 0, &value);
	if (ret < 0)
		return ret;

	mutex_lock(&chip->model_lock);

	ret = max77779_fg_health_update_ai(chip, value);
	if (ret == 0)
		chip->bhi_acim = 0;

	dev_info(chip->dev, "value=%d  (%d)\n", value, ret);

	mutex_unlock(&chip->model_lock);
	return count;
}

static ssize_t act_impedance_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct power_supply *psy = container_of(dev, struct power_supply, dev);
	struct max77779_fg_chip *chip = power_supply_get_drvdata(psy);

	return scnprintf(buf, PAGE_SIZE, "%d\n", max77779_fg_health_get_ai(chip));
}

static DEVICE_ATTR_RW(act_impedance);

static int max77779_fg_init_sysfs(struct max77779_fg_chip *chip)
{
	struct dentry *de;

	de = debugfs_create_dir(chip->max77779_fg_psy_desc.name, 0);
	if (IS_ERR_OR_NULL(de))
		return -ENOENT;

	debugfs_create_file("irq_none_cnt", 0644, de, chip, &irq_none_cnt_fops);
	debugfs_create_file("fg_reset", 0400, de, chip, &debug_fg_reset_fops);
	debugfs_create_file("ce_start", 0400, de, chip, &debug_ce_start_fops);
	debugfs_create_file("fake_battery", 0400, de, chip, &debug_fake_battery_fops);
	debugfs_create_file("batt_id", 0600, de, chip, &debug_batt_id_fops);
	debugfs_create_file("force_psy_update", 0600, de, chip, &debug_force_psy_update_fops);

	if (chip->regmap.reglog)
		debugfs_create_file("regmap_writes", 0440, de,
					chip->regmap.reglog,
					&debug_reglog_writes_fops);

	debugfs_create_bool("model_ok", 0444, de, &chip->model_ok);
	debugfs_create_file("sync_model", 0400, de, chip, &debug_sync_model_fops);

	/* new debug interface */
	debugfs_create_u32("address", 0600, de, &chip->debug_reg_address);
	debugfs_create_u32("debug_address", 0600, de, &chip->debug_dbg_reg_address);
	debugfs_create_file("data", 0600, de, chip, &debug_reg_data_fops);
	debugfs_create_file("debug_data", 0600, de, chip, &debug_reg_dbg_data_fops);

	/* dump all registers */
	debugfs_create_file("registers", 0444, de, chip, &debug_reg_all_fops);
	debugfs_create_file("debug_registers", 0444, de, chip, &debug_reg_all_dbg_fops);

	/* reset fg eeprom data for debugging */
	debugfs_create_file("cnhs_reset", 0400, de, chip, &debug_reset_cnhs_fops);
	debugfs_create_file("gmsr_reset", 0400, de, chip, &debug_reset_gmsr_fops);

	/* capacity fade */
	debugfs_create_u32("bhi_fcn_count", 0644, de, &chip->bhi_fcn_count);

	return 0;
}

static u16 max77779_fg_read_rsense(const struct max77779_fg_chip *chip)
{
	u32 rsense_default = 0;
	u16 rsense = 200;
	int ret;

	ret = of_property_read_u32(chip->dev->of_node, "max77779,rsense-default",
				   &rsense_default);
	if (ret == 0)
		rsense = rsense_default;

	return rsense;
}

static int max77779_fg_dump_param(struct max77779_fg_chip *chip)
{
	int ret;
	u16 data;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_Config, &chip->RConfig);
	if (ret < 0)
		return ret;

	dev_info(chip->dev, "Config: 0x%04x\n", chip->RConfig);

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_IChgTerm, &data);
	if (ret < 0)
		return ret;

	dev_info(chip->dev, "IChgTerm: %d\n", reg_to_micro_amp(data, chip->RSense));

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_VEmpty, &data);
	if (ret < 0)
		return ret;

	dev_info(chip->dev, "VEmpty: VE=%dmV VR=%dmV\n",
		 reg_to_vempty(data), reg_to_vrecovery(data));

	return 0;
}

static int max77779_fg_clear_por(struct max77779_fg_chip *chip)
{
	u16 data;
	int ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_Status, &data);
	if (ret < 0)
		return ret;

	if ((data & MAX77779_FG_Status_PONR_MASK) == 0)
		return 0;

	return regmap_update_bits(chip->regmap.regmap,
				  MAX77779_FG_Status,
				  MAX77779_FG_Status_PONR_MASK,
				  0x1);
}

/* read state from fg (if needed) and set the next update field */
static int max77779_fg_set_next_update(struct max77779_fg_chip *chip)
{
	int rc;
	u16 reg_cycle;

	/* do not save data when battery ID not clearly */
	if (chip->batt_id == DEFAULT_BATTERY_ID)
		return 0;

	rc = REGMAP_READ(&chip->regmap, MAX77779_FG_Cycles, &reg_cycle);
	if (rc < 0)
		return rc;

	if (chip->model_next_update && reg_cycle < chip->model_next_update)
		return 0;

	/* read new state from Fuel gauge, save to storage if needed */
	rc = max77779_model_read_state(chip->model_data);
	if (rc == 0) {
		rc = max77779_model_check_state(chip->model_data);
		if (rc < 0) {
			pr_debug("%s: fg model state is corrupt rc=%d\n",
				 __func__, rc);
			return -EINVAL;
		}
	}

	if (rc == 0 && chip->model_next_update)
		rc = max77779_save_state_data(chip->model_data);
	if (rc == 0)
		chip->model_next_update = (reg_cycle + (1 << 6)) &
					  ~((1 << 6) - 1);

	pr_debug("%s: reg_cycle=%d next_update=%d rc=%d\n", __func__,
		 reg_cycle, chip->model_next_update, rc);

	return 0;
}

static int max77779_fg_model_load(struct max77779_fg_chip *chip)
{
	int ret;

	/* TODO: b/283487421 - Implement loading procedure */

	/* retrieve model state from permanent storage only on boot */
	if (!chip->model_state_valid) {

		/*
		 * retrieve state from storage: retry on -EAGAIN as long as
		 * model_reload > _IDLE
		 */
		ret = max77779_load_state_data(chip->model_data);
		if (ret == -EAGAIN)
			return -EAGAIN;
		if (ret < 0)
			dev_warn(chip->dev, "Load Model Using Default State (%d)\n", ret);

		/* use the state from the DT when GMSR is invalid */
	}

	/* failure on the gauge: retry as long as model_reload > IDLE */
	ret = max77779_load_gauge_model(chip->model_data);
	if (ret < 0) {
		dev_err(chip->dev, "Load Model Failed ret=%d\n", ret);
		return -EAGAIN;
	}

	/* mark model state as "safe" */
	chip->reg_prop_capacity_raw = MAX77779_FG_RepSOC;
	chip->model_state_valid = true;
	return 0;
}

static void max77779_fg_model_work(struct work_struct *work)
{
	struct max77779_fg_chip *chip = container_of(work, struct max77779_fg_chip,
						     model_work.work);
	bool new_model = false;
	u16 reg_cycle;
	int rc;

	if (!chip->model_data)
		return;

	mutex_lock(&chip->model_lock);

	/* set model_reload to the #attempts, might change cycle count */
	if (chip->model_reload >= MAX77779_LOAD_MODEL_REQUEST) {

		rc = max77779_fg_model_load(chip);
		if (rc == 0) {
			rc = max77779_fg_clear_por(chip);

			dev_info(chip->dev, "Model OK, Clear Power-On Reset (%d)\n",
				 rc);

			/* TODO: keep trying to clear POR if the above fail */

			max77779_fg_restore_battery_cycle(chip);
			rc = REGMAP_READ(&chip->regmap, MAX77779_FG_Cycles, &reg_cycle);
			if (rc == 0 && reg_cycle >= 0) {
				chip->model_reload = MAX77779_LOAD_MODEL_IDLE;
				chip->model_ok = true;
				new_model = true;
				/* saved new value in max77779_fg_set_next_update */
				chip->model_next_update = reg_cycle > 0 ? reg_cycle - 1 : 0;
			}
		} else if (rc != -EAGAIN) {
			chip->model_reload = MAX77779_LOAD_MODEL_DISABLED;
			chip->model_ok = false;
		} else if (chip->model_reload > MAX77779_LOAD_MODEL_IDLE) {
			chip->model_reload -= 1;
		}
	}

	if (chip->model_reload >= MAX77779_LOAD_MODEL_REQUEST) {
		const unsigned long delay = msecs_to_jiffies(60 * 1000);

		mod_delayed_work(system_wq, &chip->model_work, delay);
	}

	if (new_model) {
		dev_info(chip->dev, "FG Model OK, ver=%d next_update=%d\n",
			 max77779_fg_model_version(chip->model_data),
			 chip->model_next_update);
		power_supply_changed(chip->psy);
	}

	mutex_unlock(&chip->model_lock);
}

static int read_chip_property_u32(const struct max77779_fg_chip *chip,
				  char *property, u32 *data32)
{
	int ret;

	if (chip->batt_node) {
		ret = of_property_read_u32(chip->batt_node, property, data32);
		if (ret == 0)
			return ret;
	}

	return of_property_read_u32(chip->dev->of_node, property, data32);
}

static int max77779_fg_check_config(struct max77779_fg_chip *chip)
{
	u16 data;
	int ret;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_Config, &data);
	if (ret == 0 && (data & MAX77779_FG_Config_Ten_MASK) == 0)
		return -EINVAL;

	return 0;
}

static int max77779_fg_log_event(struct max77779_fg_chip *chip, gbms_tag_t tag)
{
	u8 event_count;
	int ret = 0;

	ret = gbms_storage_read(tag, &event_count, sizeof(event_count));
	if (ret < 0)
		return ret;

	/* max count */
	if (event_count == 0xFE)
		return 0;

	/* initial value */
	if (event_count == 0xFF)
		event_count = 1;
	else
		event_count++;

	ret = gbms_storage_write(tag, &event_count, sizeof(event_count));
	if (ret < 0)
		return ret;

	dev_info(chip->dev, "tag:0x%X, event_count:%d\n", tag, event_count);

	return 0;
}

/* handle recovery of FG state */
static int max77779_fg_init_model_data(struct max77779_fg_chip *chip)
{
	int ret;

	if (!chip->model_data)
		return 0;

	if (!max77779_fg_model_check_version(chip->model_data)) {
		if (max77779_needs_reset_model_data(chip->model_data)) {
			ret = max77779_reset_state_data(chip->model_data);
			if (ret < 0)
				dev_err(chip->dev, "GMSR: failed to erase RC2 saved model data"
						" ret=%d\n", ret);
			else
				dev_warn(chip->dev, "GMSR: RC2 model data erased\n");
		}

		/* this is expected */
		ret = max77779_fg_full_reset(chip);

		dev_warn(chip->dev, "FG Version Changed, Reset (%d), Will Reload\n", ret);
		return 0;
	}

	/* TODO add retries */
	ret = max77779_model_read_state(chip->model_data);
	if (ret < 0) {
		dev_err(chip->dev, "FG Model Error (%d)\n", ret);
		return -EPROBE_DEFER;
	}

	/* this is a real failure and must be logged */
	ret = max77779_model_check_state(chip->model_data);
	if (ret < 0) {
		int rret = max77779_fg_full_reset(chip);

		dev_err(chip->dev, "FG State Corrupt (%d), Reset (%d) Will reload\n", ret, rret);

		ret = max77779_fg_log_event(chip, GBMS_TAG_SELC);
		if (ret < 0)
			dev_err(chip->dev, "Cannot log the event (%d)\n", ret);

		return 0;
	}

	ret = max77779_fg_check_config(chip);
	if (ret < 0) {
		ret = max77779_fg_full_reset(chip);

		dev_err(chip->dev, "Invalid config data, Reset (%d), Will reload\n", ret);

		ret = max77779_fg_log_event(chip, GBMS_TAG_CELC);
		if (ret < 0)
			dev_err(chip->dev, "Cannot log the event (%d)\n", ret);

		return 0;
	}

	ret = max77779_fg_set_next_update(chip);
	if (ret < 0)
		dev_warn(chip->dev, "Error on Next Update, Will retry\n");

	dev_info(chip->dev, "FG Model OK, ver=%d next_update=%d\n",
			max77779_model_read_version(chip->model_data),
			chip->model_next_update);

	chip->reg_prop_capacity_raw = MAX77779_FG_RepSOC;
	chip->model_state_valid = true;
	chip->model_ok = true;
	return 0;
}

static int max77779_fg_init_chip(struct max77779_fg_chip *chip)
{
	int ret;
	u16 data = 0;

	if (of_property_read_bool(chip->dev->of_node, "max77779,force-hard-reset"))
		max77779_fg_full_reset(chip);

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_Status, &data);
	if (ret < 0)
		return -EPROBE_DEFER;
	chip->por = (data & MAX77779_FG_Status_PONR_MASK) != 0;

	/* TODO: handle RSense 0 */
	chip->RSense = max77779_fg_read_rsense(chip);
	if (chip->RSense == 0)
		dev_err(chip->dev, "no default RSense value\n");

	/* set maxim,force-batt-id in DT to not delay the probe */
	ret = max77779_fg_read_batt_id(&chip->batt_id, chip);
	if (ret == -EPROBE_DEFER) {
		if (chip->batt_id_defer_cnt) {
			chip->batt_id_defer_cnt -= 1;
			return -EPROBE_DEFER;
		}

		chip->batt_id = DEFAULT_BATTERY_ID;
		dev_info(chip->dev, "default device battery ID = %d\n",
			 chip->batt_id);
	} else {
		dev_info(chip->dev, "device battery RID: %d kohm\n",
			 chip->batt_id);
	}

	/* TODO: b/283489811 - fix this */
	/* do not request the interrupt if can't read battery or not present */
	if (chip->batt_id == DEFAULT_BATTERY_ID || chip->batt_id == DUMMY_BATTERY_ID) {
		ret = MAX77779_FG_REGMAP_WRITE(&chip->regmap, MAX77779_FG_CONFIG2, 0x0);
		if (ret < 0)
			dev_warn(chip->dev, "Cannot write 0x0 to Config(%d)\n", ret);
	}

	/* fuel gauge model needs to know the batt_id */
	mutex_init(&chip->model_lock);

	/*
	 * FG model is ony used for integrated FG (MW). Loading a model might
	 * change the capacity drift WAR algo_ver and design_capacity.
	 * NOTE: design_capacity used for drift might be updated after loading
	 * a FG model.
	 */
	ret = max77779_fg_init_model(chip);
	if (ret < 0)
		dev_err(chip->dev, "Cannot init FG model (%d)\n", ret);

	ret = max77779_fg_dump_param(chip);
	if (ret < 0)
		return -EPROBE_DEFER;
	dev_info(chip->dev, "RSense value %d micro Ohm\n", chip->RSense * 10);

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_Status, &data);
	if (!ret && data & MAX77779_FG_Status_Br_MASK) {
		dev_info(chip->dev, "Clearing Battery Removal bit\n");
		regmap_update_bits(chip->regmap.regmap, MAX77779_FG_Status,
				   MAX77779_FG_Status_Br_MASK, 0x1);
	}
	if (!ret && data & MAX77779_FG_Status_Bi_MASK) {
		dev_info(chip->dev, "Clearing Battery Insertion bit\n");
		regmap_update_bits(chip->regmap.regmap, MAX77779_FG_Status,
				   MAX77779_FG_Status_Bi_MASK, 0x1);
	}

	max77779_fg_restore_battery_cycle(chip);

	/* triggers loading of the model in the irq handler on POR */
	if (!chip->por) {
		ret = max77779_fg_init_model_data(chip);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/* ------------------------------------------------------------------------- */

/* TODO b/284191528 - Add to common code file */
#define REG_HALF_HIGH(reg)     ((reg >> 8) & 0x00FF)
#define REG_HALF_LOW(reg)      (reg & 0x00FF)
static int max77779_fg_collect_history_data(void *buff, size_t size,
					 struct max77779_fg_chip *chip)
{
	struct max77779_fg_eeprom_history hist = { 0 };
	u16 data, designcap;
	int ret;

	if (chip->por)
		return -EINVAL;

	ret = REGMAP_READ(&chip->regmap_debug, MAX77779_FG_DBG_nTempCo, &data);
	if (ret)
		return ret;

	hist.tempco = data;

	ret = REGMAP_READ(&chip->regmap_debug, MAX77779_FG_DBG_nRComp0, &data);
	if (ret)
		return ret;

	hist.rcomp0 = data;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_TimerH, &data);
	if (ret)
		return ret;

	/* Convert LSB from 3.2hours(192min) to 5days(7200min) */
	hist.timerh = data * 192 / 7200;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_DesignCap, &designcap);
	if (ret)
		return ret;

	/* multiply by 100 to convert from mAh to %, LSB 0.125% */
	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_FullCapNom, &data);
	if (ret)
		return ret;

	data = data * 800 / designcap;
	hist.fullcapnom = data > MAX_HIST_FULLCAP ? MAX_HIST_FULLCAP : data;

	/* multiply by 100 to convert from mAh to %, LSB 0.125% */
	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_FullCapRep, &data);
	if (ret)
		return ret;

	data = data * 800 / designcap;
	hist.fullcaprep = data > MAX_HIST_FULLCAP ? MAX_HIST_FULLCAP : data;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_MixSOC, &data);
	if (ret)
		return ret;

	/* Convert LSB from 1% to 2% */
	hist.mixsoc = REG_HALF_HIGH(data) / 2;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_VFSOC, &data);
	if (ret)
		return ret;

	/* Convert LSB from 1% to 2% */
	hist.vfsoc = REG_HALF_HIGH(data) / 2;


	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_MaxMinVolt, &data);
	if (ret)
		return ret;

	/* LSB is 20mV, store values from 4.2V min */
	hist.maxvolt = (REG_HALF_HIGH(data) * 20 - 4200) / 20;
	/* Convert LSB from 20mV to 10mV, store values from 2.5V min */
	hist.minvolt = (REG_HALF_LOW(data) * 20 - 2500) / 10;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_MaxMinTemp, &data);
	if (ret)
		return ret;

	/* Convert LSB from 1degC to 3degC, store values from 25degC min */
	hist.maxtemp = (REG_HALF_HIGH(data) - 25) / 3;
	/* Convert LSB from 1degC to 3degC, store values from -20degC min */
	hist.mintemp = (REG_HALF_LOW(data) + 20) / 3;

	ret = REGMAP_READ(&chip->regmap, MAX77779_FG_MaxMinCurr, &data);
	if (ret)
		return ret;

	/* Convert LSB from 0.08A to 0.5A */
	hist.maxchgcurr = REG_HALF_HIGH(data) * 8 / 50;
	hist.maxdischgcurr = REG_HALF_LOW(data) * 8 / 50;

	memcpy(buff, &hist, sizeof(hist));
	return (size_t)sizeof(hist);
}

static int max77779_fg_prop_iter(int index, gbms_tag_t *tag, void *ptr)
{
	static gbms_tag_t keys[] = {GBMS_TAG_CLHI};
	const int count = ARRAY_SIZE(keys);

	if (index >= 0 && index < count) {
		*tag = keys[index];
		return 0;
	}

	return -ENOENT;
}

static int max77779_fg_prop_read(gbms_tag_t tag, void *buff, size_t size,
			      void *ptr)
{
	struct max77779_fg_chip *chip = (struct max77779_fg_chip *)ptr;
	int ret = -ENOENT;

	switch (tag) {
	case GBMS_TAG_CLHI:
		ret = max77779_fg_collect_history_data(buff, size, chip);
		break;

	default:
		break;
	}

	return ret;
}

static struct gbms_storage_desc max77779_fg_prop_dsc = {
	.iter = max77779_fg_prop_iter,
	.read = max77779_fg_prop_read,
};

/* ------------------------------------------------------------------------- */

/* this must be not blocking */
static void max77779_fg_read_serial_number(struct max77779_fg_chip *chip)
{
	char buff[32] = {0};
	int ret = gbms_storage_read(GBMS_TAG_MINF, buff, GBMS_MINF_LEN);

	if (ret >= 0)
		strncpy(chip->serial_number, buff, ret);
	else
		chip->serial_number[0] = '\0';
}

static void max77779_fg_init_work(struct work_struct *work)
{
	struct max77779_fg_chip *chip = container_of(work, struct max77779_fg_chip,
						  init_work.work);
	int ret = 0;

	if (chip->gauge_type != -1) {

		/* these don't require nvm storage */
		ret = gbms_storage_register(&max77779_fg_prop_dsc, "max7779fg", chip);
		if (ret == -EBUSY)
			ret = 0;

		if (ret == 0)
			ret = max77779_fg_init_chip(chip);
		if (ret == -EPROBE_DEFER) {
			schedule_delayed_work(&chip->init_work,
				msecs_to_jiffies(MAX77779_FG_DELAY_INIT_MS));
			return;
		}
	}

	/* serial number might not be stored in the FG */
	max77779_fg_read_serial_number(chip);

	mutex_init(&chip->cap_estimate.batt_ce_lock);
	chip->prev_charge_status = POWER_SUPPLY_STATUS_UNKNOWN;
	chip->fake_capacity = -EINVAL;
	chip->resume_complete = true;
	chip->init_complete = true;
	chip->bhi_acim = 0;

	max77779_fg_init_sysfs(chip);

	/*
	 * Handle any IRQ that might have been set before init
	 * NOTE: will clear the POR bit and trigger model load if needed
	 */
	max77779_fg_irq_thread_fn(-1, chip);

	dev_info(chip->dev, "init_work done\n");
}

/* TODO: fix detection of 17301 for non samples looking at FW version too */
static int max77779_read_gauge_type(struct max77779_fg_chip *chip)
{
	u8 reg = MAX77779_FG_DevName;
	struct i2c_msg xfer[2];
	uint8_t buf[2] = { };
	int ret, gauge_type;

	/* some maxim IF-PMIC corrupt reads w/o Rs b/152373060 */
	xfer[0].addr = chip->primary->addr;
	xfer[0].flags = 0;
	xfer[0].len = 1;
	xfer[0].buf = &reg;

	xfer[1].addr = chip->primary->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = 2;
	xfer[1].buf = buf;

	ret = i2c_transfer(chip->primary->adapter, xfer, 2);
	if (ret != 2)
		return -EIO;

	/* it might need devname later */
	chip->devname = buf[1] << 8 | buf[0];
	dev_info(chip->dev, "chip devname:0x%X\n", chip->devname);

	ret = of_property_read_u32(chip->dev->of_node, "max77779,gauge-type",
				   &gauge_type);
	if (ret == 0) {
		dev_warn(chip->dev, "forced gauge type to %d\n", gauge_type);
		return gauge_type;
	}
	/* TODO b/283488733 add max77779 devname support */
	ret = max77779_check_devname(chip->devname);
	if (ret)
		return MAX_M5_GAUGE_TYPE;

	switch (chip->devname >> 4) {
	case 0x404: /* max1730x sample */
	case 0x405: /* max1730x pass2 silicon initial samples */
	case 0x406: /* max1730x pass2 silicon */
		gauge_type = MAX1730X_GAUGE_TYPE;
		break;
	default:
		break;
	}

	switch (chip->devname & 0x000F) {
	case 0x1: /* max17201 or max17211 */
	case 0x5: /* max17205 or max17215 */
	default:
		gauge_type = MAX1720X_GAUGE_TYPE;
		break;
	}

	return gauge_type;
}

static bool max77779_fg_dbg_is_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
		case 0x9D ... 0x9E:
		case 0xA5 ... 0xA7:
		case 0xB1 ... 0xB3:
		case 0xC6:
		case 0xC8 ... 0xCA:
			return true;
	}
	return false;
}

const struct regmap_config max77779_fg_debug_regmap_cfg = {
	.reg_bits = 8,
	.val_bits = 16,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.max_register = MAX77779_FG_DBG_nThermCfg,
	.readable_reg = max77779_fg_dbg_is_reg,
	.volatile_reg = max77779_fg_dbg_is_reg,
};

static bool max77779_fg_is_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x00 ... 0x14:
	case 0x16 ... 0x1D:
	case 0x1F ... 0x27:
	case 0x29: /* ICHGTERM */
	case 0x2E ... 0x35:
	case 0x37:		/* VFSOC */
		return true;
	case 0x39 ... 0x3A:
	case 0x3D ... 0x3F:
	case 0x42:
	case 0x45 ... 0x48:
	case 0x4C ... 0x4E:
	case 0x52 ... 0x53:
	case 0x80 ... 0x9F: /* Model */
	case 0xA3: /* Model cfg */
	case 0xB0:
	case 0xB2:
	case 0xB4:
	case 0xBE:
	case 0xD0 ... 0xD3:
	case 0xD5 ... 0xDB:
	case 0xE0 ... 0xE1: /* FG_Func*/
	case 0xE9 ... 0xEA:
		return true;
	}

	return false;
}

const struct regmap_config max77779_fg_model_regmap_cfg = {
	.reg_bits = 8,
	.val_bits = 16,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.max_register = MAX77779_FG_Command_ack,
	.readable_reg = max77779_fg_is_reg,
	.volatile_reg = max77779_fg_is_reg,
};

const struct max17x0x_reg max77779_fg_model[] = {
	[MAX17X0X_TAG_avgc] = { ATOM_INIT_REG16(MAX77779_FG_AvgCurrent)},
	[MAX17X0X_TAG_cnfg] = { ATOM_INIT_REG16(MAX77779_FG_Config)},
	[MAX17X0X_TAG_mmdv] = { ATOM_INIT_REG16(MAX77779_FG_MaxMinVolt)},
	[MAX17X0X_TAG_vcel] = { ATOM_INIT_REG16(MAX77779_FG_VCell)},
	[MAX17X0X_TAG_temp] = { ATOM_INIT_REG16(MAX77779_FG_Temp)},
	[MAX17X0X_TAG_curr] = { ATOM_INIT_REG16(MAX77779_FG_Current)},
	[MAX17X0X_TAG_mcap] = { ATOM_INIT_REG16(MAX77779_FG_MixCap)},
	[MAX17X0X_TAG_vfsoc] = { ATOM_INIT_REG16(MAX77779_FG_VFSOC)},
};

int max77779_max17x0x_regmap_init(struct max17x0x_regmap *regmap,
															struct i2c_client *clnt,
															const struct regmap_config *regmap_config,
															bool tag)
{
	struct regmap *map;

	map = devm_regmap_init_i2c(clnt, regmap_config);
	if (IS_ERR(map))
		return IS_ERR_VALUE(map);

	if (tag) {
		regmap->regtags.max = ARRAY_SIZE(max77779_fg_model);
		regmap->regtags.map = max77779_fg_model;
	} else {
		regmap->regtags.max = 0;
		regmap->regtags.map = NULL;
	}

	regmap->regmap = map;
	return 0;
}

/* NOTE: NEED TO COME BEFORE REGISTER ACCESS */
static int max77779_fg_regmap_init(struct max77779_fg_chip *chip)
{
	int ret = max77779_max17x0x_regmap_init(&chip->regmap, chip->primary,
																				&max77779_fg_model_regmap_cfg,
																				true);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to re-initialize regmap (%ld)\n",
			IS_ERR_VALUE(chip->regmap.regmap));
		return -EINVAL;
	}

	ret = max77779_max17x0x_regmap_init(&chip->regmap_debug, chip->secondary,
																			&max77779_fg_debug_regmap_cfg,
																			false);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to re-initialize debug regmap (%ld)\n",
			IS_ERR_VALUE(chip->regmap_debug.regmap));
		return IS_ERR_VALUE(chip->regmap_debug.regmap);
	}
	return 0;
}

void *max77779_get_model_data(struct i2c_client *client)
{
	struct max77779_fg_chip *chip = i2c_get_clientdata(client);

	return chip ? chip->model_data : NULL;
}


static struct attribute *max77779_fg_attrs[] = {
	&dev_attr_act_impedance.attr,
	&dev_attr_offmode_charger.attr,
	&dev_attr_resistance_id.attr,
	&dev_attr_resistance.attr,
	&dev_attr_gmsr.attr,
	NULL,
};

static const struct attribute_group max77779_fg_attr_grp = {
	.attrs = max77779_fg_attrs,
};

static int max77779_fg_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct max77779_fg_chip *chip;
	struct device *dev = &client->dev;
	struct power_supply_config psy_cfg = { };
	const char *psy_name = NULL;
	char monitor_name[32];
	int ret = 0;
	u32 data32;

	if (client->irq < 0)
		return -EPROBE_DEFER;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = dev;
	chip->fake_battery = of_property_read_bool(dev->of_node, "max77779,no-battery") ? 0 : -1;
	chip->primary = client;
	chip->batt_id_defer_cnt = DEFAULT_BATTERY_ID_RETRIES;
	i2c_set_clientdata(client, chip);

	chip->secondary = i2c_new_ancillary_device(chip->primary, "ndbg", MAX77779_FG_NDGB_ADDRESS);
	if (IS_ERR(chip->secondary)) {
		dev_err(dev, "Error setting up ancillary i2c bus(%ld)\n", IS_ERR_VALUE(chip->secondary));
		goto i2c_unregister_primary;
	}
	i2c_set_clientdata(chip->secondary, chip);

	/* needs chip->primary */
	ret = max77779_fg_regmap_init(chip);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize regmap(s)\n");
		goto i2c_unregister;
	}

	/* NOTE: < 0 not available, it could be a bare MLB */
	chip->gauge_type = max77779_read_gauge_type(chip);
	if (chip->gauge_type < 0)
		chip->gauge_type = -1;

	ret = of_property_read_u32(dev->of_node, "max77779,status-charge-threshold-ma",
				   &data32);
	if (ret == 0)
		chip->status_charge_threshold_ma = data32;
	else
		chip->status_charge_threshold_ma = DEFAULT_STATUS_CHARGE_MA;

	if (of_property_read_bool(dev->of_node, "max77779,log_writes")) {
		bool debug_reglog;

		debug_reglog = max77779_fg_reglog_init(chip);
		dev_info(dev, "write log %savailable\n",
			 debug_reglog ? "" : "not ");
	}

	if (client->irq) {
		ret = devm_request_threaded_irq(chip->dev, chip->primary->irq, NULL,
						max77779_fg_irq_thread_fn,
						IRQF_TRIGGER_LOW |
						IRQF_SHARED |
						IRQF_ONESHOT,
						MAX77779_FG_I2C_DRIVER_NAME,
						chip);
		dev_info(chip->dev, "FG irq handler registered at %d (%d)\n",
						chip->primary->irq, ret);
		if (ret == 0)
			enable_irq_wake(chip->primary->irq);
	} else {
		dev_err(dev, "cannot allocate irq\n");
		goto i2c_unregister;
	}

	psy_cfg.drv_data = chip;
	psy_cfg.of_node = chip->dev->of_node;

	ret = of_property_read_string(dev->of_node,
				      "max77779,dual-battery", &psy_name);
	if (ret == 0)
		chip->max77779_fg_psy_desc.name = devm_kstrdup(dev, psy_name, GFP_KERNEL);
	else
		chip->max77779_fg_psy_desc.name = "max77779fg";

	dev_info(dev, "max77779_fg_psy_desc.name=%s\n", chip->max77779_fg_psy_desc.name);

	chip->max77779_fg_psy_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	chip->max77779_fg_psy_desc.get_property = max77779_fg_get_property;
	chip->max77779_fg_psy_desc.set_property = max77779_fg_set_property;
	chip->max77779_fg_psy_desc.property_is_writeable = max77779_fg_property_is_writeable;
	chip->max77779_fg_psy_desc.properties = max77779_fg_battery_props;
	chip->max77779_fg_psy_desc.num_properties = ARRAY_SIZE(max77779_fg_battery_props);

	if (of_property_read_bool(dev->of_node, "max77779,psy-type-unknown"))
		chip->max77779_fg_psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;

	chip->psy = devm_power_supply_register(dev, &chip->max77779_fg_psy_desc,
					       &psy_cfg);
	if (IS_ERR(chip->psy)) {
		dev_err(dev, "Couldn't register as power supply\n");
		ret = PTR_ERR(chip->psy);
		goto i2c_unregister;
	}

	ret = sysfs_create_group(&chip->psy->dev.kobj, &max77779_fg_attr_grp);
	if (ret)
		dev_err(dev, "Failed to create sysfs group\n");

	/*
	 * TODO:
	 *	POWER_SUPPLY_PROP_CHARGE_FULL_ESTIMATE -> GBMS_TAG_GCFE
	 *	POWER_SUPPLY_PROP_RES_FILTER_COUNT -> GBMS_TAG_RFCN
	 */

	/* M5 battery model needs batt_id and is setup during init() */
	chip->model_reload = MAX77779_LOAD_MODEL_DISABLED;

	chip->ce_log = logbuffer_register(chip->max77779_fg_psy_desc.name);
	if (IS_ERR(chip->ce_log)) {
		ret = PTR_ERR(chip->ce_log);
		dev_err(dev, "failed to obtain logbuffer, ret=%d\n", ret);
		chip->ce_log = NULL;
	}

	scnprintf(monitor_name, sizeof(monitor_name), "%s_%s",
		  chip->max77779_fg_psy_desc.name, "monitor");
	chip->monitor_log = logbuffer_register(monitor_name);
	if (IS_ERR(chip->monitor_log)) {
		ret = PTR_ERR(chip->monitor_log);
		dev_err(dev, "failed to obtain logbuffer, ret=%d\n", ret);
		chip->monitor_log = NULL;
	}

	ret = of_property_read_u32(dev->of_node, "google,bhi-fcn-count",
				   &chip->bhi_fcn_count);
	if (ret < 0)
		chip->bhi_fcn_count = BHI_CAP_FCN_COUNT;

	/* use VFSOC until it can confirm that FG Model is running */
	chip->reg_prop_capacity_raw = MAX77779_FG_VFSOC;

	INIT_DELAYED_WORK(&chip->cap_estimate.settle_timer,
			  batt_ce_capacityfiltered_work);
	INIT_DELAYED_WORK(&chip->init_work, max77779_fg_init_work);
	/* TODO: b/283487421 - Implement model loading */
	// INIT_DELAYED_WORK(&chip->model_work, max77779_fg_model_work);

	schedule_delayed_work(&chip->init_work, 0);

	return 0;

i2c_unregister:
	i2c_unregister_device(chip->secondary);
i2c_unregister_primary:
	i2c_unregister_device(chip->primary);
	devm_kfree(dev, chip);

	return ret;
}

static int max77779_fg_remove(struct i2c_client *client)
{
	struct max77779_fg_chip *chip = i2c_get_clientdata(client);

	if (chip->ce_log) {
		logbuffer_unregister(chip->ce_log);
		chip->ce_log = NULL;
	}

	max77779_free_data(chip->model_data);
	cancel_delayed_work(&chip->init_work);
	/* TODO: b/283487421 - Implement model loading */
	// cancel_delayed_work(&chip->model_work);

	if (chip->primary->irq)
		free_irq(chip->primary->irq, chip);

	if (chip->secondary)
		i2c_unregister_device(chip->secondary);

	power_supply_unregister(chip->psy);

	return 0;
}

static const struct of_device_id max77779_of_match[] = {
	{ .compatible = "maxim,max77779fg"},
	{},
};
MODULE_DEVICE_TABLE(of, max77779_of_match);

static const struct i2c_device_id max77779_fg_id[] = {
	{"max77779_fg", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, max77779_fg_id);

#ifdef CONFIG_PM_SLEEP
static int max77779_pm_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max77779_fg_chip *chip = i2c_get_clientdata(client);

	pm_runtime_get_sync(chip->dev);
	chip->resume_complete = false;
	pm_runtime_put_sync(chip->dev);

	return 0;
}

static int max77779_pm_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max77779_fg_chip *chip = i2c_get_clientdata(client);

	pm_runtime_get_sync(chip->dev);
	chip->resume_complete = true;
	if (chip->irq_disabled) {
		enable_irq(chip->primary->irq);
		chip->irq_disabled = false;
	}
	pm_runtime_put_sync(chip->dev);

	return 0;
}
#endif

static const struct dev_pm_ops max77779_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(max77779_pm_suspend, max77779_pm_resume)
};

static struct i2c_driver max77779_fg_i2c_driver = {
	.driver = {
		   .name = "max77779-fg",
		   .of_match_table = max77779_of_match,
		   .pm = &max77779_pm_ops,
		   .probe_type = PROBE_PREFER_ASYNCHRONOUS,
		   },
	.id_table = max77779_fg_id,
	.probe = max77779_fg_probe,
	.remove = max77779_fg_remove,
};

module_i2c_driver(max77779_fg_i2c_driver);
MODULE_AUTHOR("AleX Pelosi <apelosi@google.com>");
MODULE_AUTHOR("Keewan Jung <keewanjung@google.com>");
MODULE_AUTHOR("Jenny Ho <hsiufangho@google.com>");
MODULE_AUTHOR("Daniel Okazaki <dtokazaki@google.com>");
MODULE_DESCRIPTION("MAX77779 Fuel Gauge");
MODULE_LICENSE("GPL");
