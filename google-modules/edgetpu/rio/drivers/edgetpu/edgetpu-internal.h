/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Edge TPU driver common internal definitions.
 *
 * Copyright (C) 2019 Google, Inc.
 */
#ifndef __EDGETPU_INTERNAL_H__
#define __EDGETPU_INTERNAL_H__

#include <linux/printk.h>

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/mm_types.h>
#include <linux/mutex.h>
#include <linux/refcount.h>
#include <linux/scatterlist.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <gcip/gcip-dma-fence.h>
#include <gcip/gcip-firmware.h>
#include <gcip/gcip-memory.h>
#include <gcip/gcip-thermal.h>
#include <iif/iif-manager.h>

#include "edgetpu.h"
#include "edgetpu-debug.h"
#include "edgetpu-wakelock.h"

#define get_dev_for_logging(etdev)                                                                 \
	((etdev)->etiface && (etdev)->etiface->etcdev ? (etdev)->etiface->etcdev : (etdev)->dev)

#define etdev_err(etdev, fmt, ...) dev_err(get_dev_for_logging(etdev), fmt, ##__VA_ARGS__)
#define etdev_warn(etdev, fmt, ...)                                            \
	dev_warn(get_dev_for_logging(etdev), fmt, ##__VA_ARGS__)
#define etdev_info(etdev, fmt, ...)                                            \
	dev_info(get_dev_for_logging(etdev), fmt, ##__VA_ARGS__)
#define etdev_dbg(etdev, fmt, ...) dev_dbg(get_dev_for_logging(etdev), fmt, ##__VA_ARGS__)
#define etdev_err_ratelimited(etdev, fmt, ...)                                 \
	dev_err_ratelimited(get_dev_for_logging(etdev), fmt, ##__VA_ARGS__)
#define etdev_warn_ratelimited(etdev, fmt, ...)                                \
	dev_warn_ratelimited(get_dev_for_logging(etdev), fmt, ##__VA_ARGS__)
#define etdev_info_ratelimited(etdev, fmt, ...)                                \
	dev_info_ratelimited(get_dev_for_logging(etdev), fmt, ##__VA_ARGS__)
#define etdev_dbg_ratelimited(etdev, fmt, ...)                                 \
	dev_dbg_ratelimited(get_dev_for_logging(etdev), fmt, ##__VA_ARGS__)
#define etdev_warn_once(etdev, fmt, ...)                                       \
	dev_warn_once(get_dev_for_logging(etdev), fmt, ##__VA_ARGS__)

typedef u64 tpu_addr_t;

struct edgetpu_device_group;
struct edgetpu_dev_iface;
struct edgetpu_soc_data;

#define EDGETPU_NUM_PERDIE_EVENTS	2
#define perdie_event_id_to_num(event_id)				      \
	(event_id - EDGETPU_PERDIE_EVENT_LOGS_AVAILABLE)

struct edgetpu_client {
	pid_t pid;
	pid_t tgid;
	/* Reference count */
	refcount_t count;
	/* protects group. */
	struct mutex group_lock;
	/*
	 * The virtual device group this client belongs to. Can be NULL if the
	 * client doesn't belong to any group.
	 */
	struct edgetpu_device_group *group;
	/* the device opened by this client */
	struct edgetpu_dev *etdev;
	/* the interface from which this client was opened */
	struct edgetpu_dev_iface *etiface;
	/* Per-client request to keep device active */
	struct edgetpu_wakelock wakelock;
	/* Bit field of registered per die events */
	u64 perdie_events;
};

/* Configurable parameters for an edgetpu interface */
struct edgetpu_iface_params {
	/*
	 * Interface-specific name.
	 * May be NULL for the default interface (etdev->dev_name will be used)
	 */
	const char *name;
};

/* edgetpu_dev#clients list entry. */
struct edgetpu_list_device_client {
	struct list_head list;
	struct edgetpu_client *client;
};

/* loop through etdev->clients (hold clients_lock prior). */
#define for_each_list_device_client(etdev, c)                                  \
	list_for_each_entry(c, &etdev->clients, list)

struct edgetpu_iommu_domain;
struct edgetpu_mapping;
struct edgetpu_mailbox_manager;
struct edgetpu_kci;
struct edgetpu_ikv;
struct edgetpu_pm;
struct edgetpu_mempool;
struct gcip_kci_response_element;

#define EDGETPU_DEVICE_NAME_MAX	64

/* ioremapped resource */
struct edgetpu_mapped_resource {
	void __iomem *mem;	/* starting virtual address */
	phys_addr_t phys;	/* starting physical address */
	resource_size_t size;	/* size in bytes */
};

enum edgetpu_dev_state {
	ETDEV_STATE_NOFW = 0,	/* no firmware running on device. */
	ETDEV_STATE_GOOD = 1,	/* healthy firmware running. */
	ETDEV_STATE_FWLOADING = 2, /* firmware is getting loaded on device. */
	ETDEV_STATE_BAD = 3,	/* firmware/device is in unusable state. */
	ETDEV_STATE_SHUTDOWN = 4, /* driver is shutting down, don't start firmware. */
};

/*
 * struct edgetpu_dev_prop
 * @lock:		Protects initialized and opaque.
 * @initialized:	Set to true when this struct object is initialized.
 * @opaque:		Device properties defined by runtime and firmware.
 */
struct edgetpu_dev_prop {
	struct mutex lock;
	bool initialized;
	u8 opaque[EDGETPU_DEV_PROP_SIZE];
};

/* a mark to know whether we read valid versions from the firmware header */
#define EDGETPU_INVALID_KCI_VERSION (~0u)

enum edgetpu_vii_format {
	EDGETPU_VII_FORMAT_UNKNOWN,
	EDGETPU_VII_FORMAT_FLATBUFFER,
	EDGETPU_VII_FORMAT_LITEBUF,
};

struct edgetpu_dev {
	struct device *dev;	   /* platform/pci bus device */
	uint num_ifaces;		   /* Number of device interfaces */
	uint num_cores; /* Number of cores */
	uint num_telemetry_buffers; /* Number of telemetry buffers */
        /*
         * Available frequencies the TPU can operate at.
         * Initialized in edgetpu_soc_early_init() and will not change after.
         */
        u32 num_active_states;
        u32 *active_states;
        u32 max_active_state;
	size_t log_buffer_size;
	size_t trace_buffer_size;
	/*
	 * Array of device interfaces
	 * First element is the default interface
	 */
	struct edgetpu_dev_iface *etiface;
	char dev_name[EDGETPU_DEVICE_NAME_MAX];
	struct edgetpu_mapped_resource regs; /* ioremapped TPU TOP CSRs */
	/* SoC-specific data */
	struct edgetpu_soc_data *soc_data;
	struct dentry *d_entry;    /* debugfs dir for this device */
	/* Built-in client for debugfs wakelock */
	struct edgetpu_client *debugfs_wakelock_client;
	struct mutex state_lock;   /* protects state of this device */
	enum edgetpu_dev_state state;
	struct mutex groups_lock;
	/* fields protected by @groups_lock */

	struct list_head groups;
	uint n_groups;		   /* number of entries in @groups */
	bool group_join_lockout;   /* disable group join while reinit */
	u32 vcid_pool;		   /* bitmask of VCID to be allocated */

	/* end of fields protected by @groups_lock */

	struct mutex clients_lock; /* protects clients */
	struct list_head clients;
	void *mmu_cookie;	   /* mmu driver private data */
	struct edgetpu_mailbox_manager *mailbox_manager;
	struct edgetpu_kci *etkci;
	struct edgetpu_ikv *etikv;
	struct edgetpu_iif *etiif;
	struct edgetpu_firmware *firmware; /* firmware management */
	struct gcip_fw_tracing *fw_tracing; /* firmware tracing */
	struct gcip_telemetry_ctx *telemetry;
	struct gcip_thermal *thermal;
	struct gcip_devfreq *devfreq;
	struct edgetpu_usage_stats *usage_stats; /* usage stats private data */
	struct edgetpu_pm *pm; /* Power management interface */
	/* Memory pool in instruction remap region */
	struct edgetpu_mempool *iremap_pool;
	struct edgetpu_sw_wdt *etdev_sw_wdt;	/* software watchdog */
	struct gcip_dma_fence_manager *gfence_mgr; /* DMA sync fences manager */
	/* version read from the firmware binary file */
	struct edgetpu_fw_version fw_version;
	/*
	 * When a client opens the device, the open handler must acquire this lock and ensure
	 * `vii_format` is not EDGETPU_VII_FORMAT_UNKNOWN. If it is, the handler must attempt to
	 * load firmware to initialize `vii_format`.
	 */
	struct mutex vii_format_uninitialized_lock;
	enum edgetpu_vii_format vii_format;
	atomic_t job_count;	/* times joined to a device group */
	/* To save device properties */
	struct edgetpu_dev_prop device_prop;

	/* counts of error events */
	uint firmware_crash_count;
	uint watchdog_timeout_count;

	/* Firmware debug service */
	struct edgetpu_fw_debug_mem fw_debug_mem;

	/*
	 * Whether the firmware CPU is running (reset-signal released) or is being held in reset.
	 *
	 * This field must only be accessed while holding a PM reference (edgetpu_pm_get()) or
	 * inside of the gcip_pm power_up/power_down handlers, which are called when the PM
	 * ref-count goes changes from or to 0 respectively.
	 */
	bool firmware_cpu_on;
};

struct edgetpu_dev_iface {
	struct cdev cdev; /* cdev char device structure */
	struct device *etcdev; /* edgetpu class char device */
	struct edgetpu_dev *etdev; /* Pointer to core device struct */
	dev_t devno; /* char device dev_t */
	const char *name; /* interface specific device name */
	struct dentry *d_entry; /* debugfs symlink if not default device name iface */
};

static inline const char *edgetpu_dma_dir_rw_s(enum dma_data_direction dir)
{
	static const char *tbl[4] = { "rw", "r", "w", "?" };

	return tbl[dir];
}

/* edgetpu device IO functions */

static inline u32 edgetpu_dev_read_32(struct edgetpu_dev *etdev,
				      uint reg_offset)
{
	return readl_relaxed(etdev->regs.mem + reg_offset);
}

/* Read 32-bit reg with memory barrier completing before following CPU reads. */
static inline u32 edgetpu_dev_read_32_sync(struct edgetpu_dev *etdev,
					   uint reg_offset)
{
	return readl(etdev->regs.mem + reg_offset);
}

static inline u64 edgetpu_dev_read_64(struct edgetpu_dev *etdev,
				      uint reg_offset)
{
	return readq_relaxed(etdev->regs.mem + reg_offset);
}

static inline void edgetpu_dev_write_32(struct edgetpu_dev *etdev,
					uint reg_offset, u32 value)
{
	writel_relaxed(value, etdev->regs.mem + reg_offset);
}

/* Write 32-bit reg with memory barrier completing CPU writes first. */
static inline void edgetpu_dev_write_32_sync(struct edgetpu_dev *etdev,
					     uint reg_offset, u32 value)
{
	writel(value, etdev->regs.mem + reg_offset);
}

static inline void edgetpu_dev_write_64(struct edgetpu_dev *etdev,
					uint reg_offset, u64 value)
{
	writeq_relaxed(value, etdev->regs.mem + reg_offset);
}

/* Checks if @file belongs to edgetpu driver */
bool is_edgetpu_file(struct file *file);

/* External drivers can hook up to edgetpu driver using these calls. */
int edgetpu_open(struct edgetpu_dev_iface *etiface, struct file *file);
long edgetpu_ioctl(struct file *file, uint cmd, ulong arg);

/* Handle firmware crash event */
void edgetpu_handle_firmware_crash(struct edgetpu_dev *etdev, enum gcip_fw_crash_type crash_type);

/* Handle notification of job lockup from firmware */
void edgetpu_handle_job_lockup(struct edgetpu_dev *etdev, u16 vcid);

/* Handle an individual client entering an unrecoverable state in firmware */
void edgetpu_handle_client_fatal_error_notify(struct edgetpu_dev *etdev, u32 client_id);

/* Handle a client inactivity timeout notification from firmware */
void edgetpu_handle_client_inactivity_timeout(struct edgetpu_dev *etdev, u32 client_id);

/* Bus (Platform/PCI) <-> Core API */

int __init edgetpu_init(void);
void __exit edgetpu_exit(void);
int edgetpu_device_add(struct edgetpu_dev *etdev,
		       const struct edgetpu_mapped_resource *regs,
		       const struct edgetpu_iface_params *iface_params,
		       uint num_ifaces);
void edgetpu_device_remove(struct edgetpu_dev *etdev);

/* Core -> Device FS API */

int __init edgetpu_fs_init(void);
void __exit edgetpu_fs_exit(void);
int edgetpu_fs_add(struct edgetpu_dev *etdev, const struct edgetpu_iface_params *etiparams,
		   int num_ifaces);

void edgetpu_fs_remove(struct edgetpu_dev *dev);
/* Get the top-level debugfs directory for the device class */
struct dentry *edgetpu_fs_debugfs_dir(void);

/* Core/Device/FS -> Chip API */

/* Device -> Core API */

/* Add current thread as new TPU client */
struct edgetpu_client *
edgetpu_client_add(struct edgetpu_dev_iface *etiface);

/* Remove TPU client */
void edgetpu_client_remove(struct edgetpu_client *client);

/* mmap() device/queue memory */
int edgetpu_mmap(struct edgetpu_client *client, struct vm_area_struct *vma);

/* Increase reference count of @client. */
struct edgetpu_client *edgetpu_client_get(struct edgetpu_client *client);

/* Decrease reference count and free @client if count reaches zero */
void edgetpu_client_put(struct edgetpu_client *client);

/*
 * Get error code corresponding to @etdev state. Caller holds
 * etdev->state_lock.
 */
int edgetpu_get_state_errno_locked(struct edgetpu_dev *etdev);

/*
 * "External mailboxes" below refers to mailboxes that are not handled
 * directly by the runtime, such as TZ or inter-IP mailboxes.
 */

/* Acquire external mailbox */
int edgetpu_acquire_ext_mailbox(struct edgetpu_client *client,
				struct edgetpu_ext_mailbox_ioctl *args);

/* Release external mailbox */
int edgetpu_release_ext_mailbox(struct edgetpu_client *client,
				struct edgetpu_ext_mailbox_ioctl *args);

/* External mailbox/secure client removal, called by edgetpu_client_remove() */
void edgetpu_ext_client_remove(struct edgetpu_client *client);

#endif /* __EDGETPU_INTERNAL_H__ */
