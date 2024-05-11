/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __BCL_H
#define __BCL_H

#if IS_ENABLED(CONFIG_GOOGLE_BCL)
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/pm_qos.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>
#include <soc/google/exynos_pm_qos.h>
#include <dt-bindings/power/s2mpg1x-power.h>
#include <dt-bindings/soc/google/zuma-bcl.h>

/* consistency checks in google_bcl_register_callback() */
#define bcl_cb_uvlo_read(bcl, m, v) (((bcl)->pmic_ops && (bcl)->intf_pmic_i2c) ? \
	(bcl)->pmic_ops->cb_uvlo_read((bcl)->intf_pmic_i2c, m, v) : -ENODEV)
#define bcl_cb_uvlo_write(bcl, m, v) (((bcl)->pmic_ops && (bcl)->intf_pmic_i2c) ? \
	(bcl)->pmic_ops->cb_uvlo_write((bcl)->intf_pmic_i2c, m, v) : -ENODEV)
#define bcl_cb_batoilo_read(bcl, v) (((bcl)->pmic_ops && (bcl)->intf_pmic_i2c) ? \
	(bcl)->pmic_ops->cb_batoilo_read((bcl)->intf_pmic_i2c, v) : -ENODEV)
#define bcl_cb_batoilo_write(bcl, v) (((bcl)->pmic_ops && (bcl)->intf_pmic_i2c) ? \
	(bcl)->pmic_ops->cb_batoilo_write((bcl)->intf_pmic_i2c, v) : -ENODEV)
#define bcl_cb_vdroop_ok(bcl, v) (((bcl)->pmic_ops && (bcl)->intf_pmic_i2c) ? \
	(bcl)->pmic_ops->cb_get_vdroop_ok((bcl)->intf_pmic_i2c, v) : -ENODEV)
#define bcl_cb_get_irq(bcl, v) (((bcl)->pmic_ops && (bcl)->intf_pmic_i2c) ? \
	(bcl)->pmic_ops->cb_get_irq((bcl)->intf_pmic_i2c, v) : -ENODEV)
#define bcl_cb_clr_irq(bcl) (((bcl)->pmic_ops && (bcl)->intf_pmic_i2c) ? \
	(bcl)->pmic_ops->cb_clr_irq((bcl)->intf_pmic_i2c) : -ENODEV)

/* helpers for UVLO1 and UVLO2 */
#define bcl_cb_uvlo1_read(bcl, v)	bcl_cb_uvlo_read(bcl, UVLO1, v)
#define bcl_cb_uvlo1_write(bcl, v)	bcl_cb_uvlo_write(bcl, UVLO1, v)
#define bcl_cb_uvlo2_read(bcl, v)	bcl_cb_uvlo_read(bcl, UVLO2, v)
#define bcl_cb_uvlo2_write(bcl, v)	bcl_cb_uvlo_write(bcl, UVLO2, v)

/* This driver determines if HW was throttled due to SMPL/OCP */

#define DELTA_10MS		(10 * NSEC_PER_MSEC)
#define DELTA_50MS		(50 * NSEC_PER_MSEC)
#define VSHUNT_MULTIPLIER	10000
#define MILLI_TO_MICRO		1000
#define IRQ_ENABLE_DELAY_MS	50

enum CPU_CLUSTER {
	LITTLE_CLUSTER,
	MID_CLUSTER,
	BIG_CLUSTER,
	CPU_CLUSTER_MAX,
};

enum SUBSYSTEM_SOURCE {
	SUBSYSTEM_CPU0,
	SUBSYSTEM_CPU1,
	SUBSYSTEM_CPU2,
	SUBSYSTEM_TPU,
	SUBSYSTEM_GPU,
	SUBSYSTEM_AUR,
	SUBSYSTEM_SOURCE_MAX,
};

enum CONCURRENT_PWRWARN_IRQ {
	NONE_BCL_BIN,
	MMWAVE_BCL_BIN,
	RFFE_BCL_BIN,
	MAX_CONCURRENT_PWRWARN_IRQ,
};

enum BCL_BATT_IRQ {
	UVLO1_IRQ_BIN,
	UVLO2_IRQ_BIN,
	BATOILO_IRQ_BIN,
	MAX_BCL_BATT_IRQ,
};

enum IRQ_DURATION_BIN {
	LT_5MS,
	BT_5MS_10MS,
	GT_10MS,
};

enum IRQ_TYPE {
	CORE_MAIN_PMIC,
	CORE_SUB_PMIC,
	IF_PMIC,
};

struct irq_duration_stats {
	atomic_t lt_5ms_count;
	atomic_t bt_5ms_10ms_count;
	atomic_t gt_10ms_count;
	ktime_t start_time;
};

extern const unsigned int subsystem_pmu[];
extern const unsigned int clk_stats_offset[];

struct ocpsmpl_stats {
	ktime_t _time;
	int capacity;
	int voltage;
};

enum RATIO_SOURCE {
	CPU0_CON,
	CPU1_HEAVY,
	CPU2_HEAVY,
	TPU_HEAVY,
	GPU_HEAVY,
	CPU1_LIGHT,
	CPU2_LIGHT,
	TPU_LIGHT,
	GPU_LIGHT
};

enum MPMM_SOURCE {
	LITTLE,
	MID,
	BIG,
	MPMMEN
};

typedef int (*pmic_set_uvlo_lvl_fn)(struct i2c_client *client, uint8_t mode, unsigned int lvl);
typedef int (*pmic_get_uvlo_lvl_fn)(struct i2c_client *client, uint8_t mode, unsigned int *lvl);
typedef int (*pmic_set_batoilo_lvl_fn)(struct i2c_client *client, unsigned int lvl);
typedef int (*pmic_get_batoilo_lvl_fn)(struct i2c_client *client, unsigned int *lvl);
typedef int (*pmic_get_vdroop_ok_fn)(struct i2c_client *client, bool *state);
typedef int (*pmic_get_irq_fn)(struct i2c_client *client, u8 *irq_val);
typedef int (*pmic_clr_irq_fn)(struct i2c_client *client);

struct bcl_ifpmic_ops {
	pmic_get_vdroop_ok_fn cb_get_vdroop_ok;
	pmic_set_uvlo_lvl_fn cb_uvlo_write;
	pmic_get_uvlo_lvl_fn cb_uvlo_read;
	pmic_set_batoilo_lvl_fn cb_batoilo_write;
	pmic_get_batoilo_lvl_fn cb_batoilo_read;
	pmic_get_irq_fn cb_get_irq;
	pmic_clr_irq_fn cb_clr_irq;
};

struct qos_throttle_limit {
	struct freq_qos_request cpu0_max_qos_req;
	struct freq_qos_request cpu1_max_qos_req;
	struct freq_qos_request cpu2_max_qos_req;
	struct exynos_pm_qos_request gpu_qos_max;
	struct exynos_pm_qos_request tpu_qos_max;
	int cpu0_limit;
	int cpu1_limit;
	int cpu2_limit;
	int gpu_limit;
	int tpu_limit;
	bool throttle;
};

struct bcl_zone {
	struct device *device;
	struct delayed_work irq_triggered_work;
	struct delayed_work irq_untriggered_work;
	struct delayed_work irq_work;
	struct delayed_work enable_irq_work;
	struct thermal_zone_device *tz;
	struct thermal_zone_of_device_ops tz_ops;
	struct qos_throttle_limit *bcl_qos;
	struct ocpsmpl_stats bcl_stats;
	atomic_t bcl_cnt;
	int bcl_prev_lvl;
	int bcl_cur_lvl;
	int bcl_lvl;
	int bcl_pin;
	int bcl_irq;
	int irq_type;
	int polarity;
	void *parent;
	int idx;
	bool disabled;
};

struct bcl_core_conf {
	unsigned int con_heavy;
	unsigned int con_light;
	unsigned int clkdivstep;
	unsigned int vdroop_flt;
	unsigned int clk_stats;
	unsigned int clk_out;
	void __iomem *base_mem;
};

struct bcl_device {
	struct device *device;
	struct device *main_dev;
	struct device *sub_dev;
	struct device *mitigation_dev;
	struct odpm_info *main_odpm;
	struct odpm_info *sub_odpm;
	void __iomem *sysreg_cpucl0;
	struct power_supply *batt_psy;
	const struct bcl_ifpmic_ops *pmic_ops;

	struct notifier_block psy_nb;
	struct bcl_zone *zone[TRIGGERED_SOURCE_MAX];
	struct delayed_work init_work;
	struct delayed_work soc_work;
	struct thermal_zone_device *soc_tz;
	struct thermal_zone_of_device_ops soc_tz_ops;

	int trip_high_temp;
	int trip_low_temp;
	int trip_val;
	struct mutex state_trans_lock;
	struct mutex sysreg_lock;

	struct i2c_client *main_pmic_i2c;
	struct i2c_client *sub_pmic_i2c;
	struct i2c_client *main_meter_i2c;
	struct i2c_client *sub_meter_i2c;
	struct i2c_client *intf_pmic_i2c;

	struct mutex ratio_lock;
	struct bcl_core_conf core_conf[SUBSYSTEM_SOURCE_MAX];

	bool batt_psy_initialized;
	bool enabled;
	bool ready;

	unsigned int main_offsrc1;
	unsigned int main_offsrc2;
	unsigned int sub_offsrc1;
	unsigned int sub_offsrc2;
	unsigned int pwronsrc;
	unsigned int irq_delay;

	unsigned int vdroop1_pin;
	unsigned int vdroop2_pin;
	unsigned int modem_gpio1_pin;
	unsigned int modem_gpio2_pin;
	unsigned int rffe_channel;

	/* debug */
	struct dentry *debug_entry;
	unsigned int gpu_clk_out;
	unsigned int tpu_clk_out;
	unsigned int aur_clk_out;
	u8 add_perph;
	u64 add_addr;
	u64 add_data;
	void __iomem *base_add_mem[SUBSYSTEM_SOURCE_MAX];

	int main_irq_base, sub_irq_base;
	u8 main_setting[METER_CHANNEL_MAX];
	u8 sub_setting[METER_CHANNEL_MAX];
	u64 main_limit[METER_CHANNEL_MAX];
	u64 sub_limit[METER_CHANNEL_MAX];
	int main_pwr_warn_irq[METER_CHANNEL_MAX];
	int sub_pwr_warn_irq[METER_CHANNEL_MAX];
	bool main_pwr_warn_triggered[METER_CHANNEL_MAX];
	bool sub_pwr_warn_triggered[METER_CHANNEL_MAX];
	struct delayed_work main_pwr_irq_work;
	struct delayed_work sub_pwr_irq_work;
	struct irq_duration_stats ifpmic_irq_bins[MAX_BCL_BATT_IRQ][MAX_CONCURRENT_PWRWARN_IRQ];
	struct irq_duration_stats pwrwarn_main_irq_bins[METER_CHANNEL_MAX];
	struct irq_duration_stats pwrwarn_sub_irq_bins[METER_CHANNEL_MAX];
	const char *main_rail_names[METER_CHANNEL_MAX];
	const char *sub_rail_names[METER_CHANNEL_MAX];

	int cpu0_cluster;
	int cpu1_cluster;
	int cpu2_cluster;
};

extern void google_bcl_irq_update_lvl(struct bcl_device *bcl_dev, int index, unsigned int lvl);
extern int google_set_db(struct bcl_device *data, unsigned int value, enum MPMM_SOURCE index);
extern unsigned int google_get_db(struct bcl_device *data, enum MPMM_SOURCE index);
extern struct bcl_device *google_retrieve_bcl_handle(void);
extern int google_bcl_register_ifpmic(struct bcl_device *bcl_dev,
				      const struct bcl_ifpmic_ops *pmic_ops);
extern int google_init_gpu_ratio(struct bcl_device *data);
extern int google_init_tpu_ratio(struct bcl_device *data);
extern int google_init_aur_ratio(struct bcl_device *data);
bool bcl_is_subsystem_on(unsigned int addr);
bool bcl_disable_power(int cluster);
bool bcl_enable_power(int cluster);
bool bcl_is_cluster_on(int cluster);
int pmic_write(int pmic, struct bcl_device *bcl_dev, u8 reg, u8 value);
int pmic_read(int pmic, struct bcl_device *bcl_dev, u8 reg, u8 *value);
int meter_write(int pmic, struct bcl_device *bcl_dev, u8 reg, u8 value);
int meter_read(int pmic, struct bcl_device *bcl_dev, u8 reg, u8 *value);
u64 settings_to_current(struct bcl_device *bcl_dev, int pmic, int idx, u32 setting);
void google_bcl_qos_update(struct bcl_zone *zone, bool throttle);
int google_bcl_setup_qos(struct bcl_device *bcl_dev);
void google_bcl_remove_qos(struct bcl_device *bcl_dev);
void google_init_debugfs(struct bcl_device *bcl_dev);
#else
struct bcl_device;

static inline settings_to_current(struct bcl_device *bcl_dev, int pmic, int idx, u32 setting)
{
	return 0;
}

static inline int meter_write(int pmic, struct bcl_device *bcl_dev, u8 reg, u8 value)
{
	return 0;
}

int meter_read(int pmic, struct bcl_device *bcl_dev, u8 reg, u8 *value)
{
	return 0;
}

static inline int pmic_write(int pmic, struct bcl_device *bcl_dev, u8 reg, u8 value)
{
	return 0;
}

int pmic_read(int pmic, struct bcl_device *bcl_dev, u8 reg, u8 *value)
{
	return 0;
}

static inline bool bcl_is_subsystem_on(unsigned int addr)
{
	return true;
}
static inline void bcl_disable_power(void)
{
}
static inline void bcl_enable_power(void)
{
}
static inline void google_bcl_irq_update_lvl(struct bcl_device *bcl_dev, int index,
					     unsigned int lvl)
{
}
static inline struct bcl_device *google_retrieve_bcl_handle(void)
{
	return NULL;
}
static int google_bcl_register_ifpmic(struct bcl_device *bcl_dev,
				      const struct bcl_ifpmic_ops *pmic_ops)
{
	return 0;
}
static inline int google_init_gpu_ratio(struct bcl_device *data)
{
	return 0;
}
static inline int google_init_tpu_ratio(struct bcl_device *data)
{
	return 0;
}
static inline int google_init_aur_ratio(struct bcl_device *data)
{
	return 0;
}
#endif /* IS_ENABLED(CONFIG_GOOGLE_BCL) */

#endif /* __BCL_H */
