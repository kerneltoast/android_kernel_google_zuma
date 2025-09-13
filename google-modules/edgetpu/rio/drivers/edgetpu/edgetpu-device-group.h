/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Implements utilities for virtual device group of EdgeTPU.
 *
 * Copyright (C) 2019 Google, Inc.
 */
#ifndef __EDGETPU_DEVICE_GROUP_H__
#define __EDGETPU_DEVICE_GROUP_H__

#include <linux/atomic.h>
#include <linux/eventfd.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/refcount.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <gcip/gcip-fence-array.h>
#include <iif/iif-fence.h>

#include "edgetpu-ikv-additional-info.h"
#include "edgetpu-internal.h"
#include "edgetpu-mailbox.h"
#include "edgetpu-mapping.h"
#include "edgetpu-mmu.h"
#include "edgetpu.h"

/* Reserved VCID that uses the extra partition. */
#define EDGETPU_VCID_EXTRA_PARTITION 0
#define EDGETPU_VCID_EXTRA_PARTITION_HIGH 1

/* entry of edgetpu_device_group#clients */
struct edgetpu_list_group_client {
	struct list_head list;
	struct edgetpu_client *client;
};

enum edgetpu_device_group_status {
	/* Waiting to be finalized. */
	EDGETPU_DEVICE_GROUP_WAITING,
	/* Most operations can only apply on a finalized group. */
	EDGETPU_DEVICE_GROUP_FINALIZED,
	/*
	 * When a fatal error occurs, groups in FINALIZED status are transformed
	 * into this state. Operations on groups with this status mostly return
	 * ECANCELED. Once the client leaves an ERRORED group, the status is
	 * transitioned to DISBANDED.
	 */
	EDGETPU_DEVICE_GROUP_ERRORED,
	/* No operations except client leaving can be performed. */
	EDGETPU_DEVICE_GROUP_DISBANDED,
};

#define EDGETPU_EVENT_COUNT 2

/* eventfds registered for event notifications from kernel for a device group */
struct edgetpu_events {
	rwlock_t lock;
	struct eventfd_ctx *eventfds[EDGETPU_EVENT_COUNT];
};

struct edgetpu_device_group {
	/*
	 * Reference count.
	 * edgetpu_device_group_get() increases the counter by one and
	 * edgetpu_device_group_put() decreases it. This object will be freed
	 * when ref_count becomes zero.
	 */
	refcount_t ref_count;
	/* Group ID number for info/debugging purposes. */
	uint group_id;
	struct edgetpu_dev *etdev;	/* the device opened by the leader */
	/*
	 * Whether mailbox attaching and detaching have effects on this group.
	 * This field is configured according to the priority field when
	 * creating this group.
	 */
	bool mailbox_detachable;
	bool mailbox_attached;
	/*
	 * Whether group->etdev is inaccessible.
	 * Some group operations will access device CSRs. If the device is known to be
	 * inaccessible (typically not powered on) then set this field to true to
	 * prevent HW interactions.
	 *
	 * Is not protected by @lock because this is only written when releasing the
	 * leader of this group.
	 */
	bool dev_inaccessible;
	/* Virtual context ID to be sent to the firmware. */
	u16 vcid;

	/* Number of additional VII commands this client is allowed to enqueue. */
	atomic_t available_vii_credits;

	/* TODO(b/409706886) Increase parallelism of group->lock holder using down_read. */
	/*
	 * Protects everything in the following comment block
	 *
	 * Most operations acquire this @lock as a writer. Only a few select operations, which only
	 * read protected fields and have been tested to verify that they can safely run in
	 * parallel, acquire @lock as readers. These operations use their own locks to ensure at
	 * most one instance of each type of allowed operation is running at a time.
	 */
	struct rw_semaphore lock;
	/* fields protected by @lock */

	/* The only client in this group */
	struct edgetpu_client *client;
	enum edgetpu_device_group_status status;
	bool activated; /* whether this group's VII has ever been activated */
	struct edgetpu_vii vii;		/* VII mailbox */

	/* The IOMMU domain being associated to this group */
	struct edgetpu_iommu_domain *etdomain;
	/*
	 * External mailboxes associated with this group, only valid if
	 * external mailbox allocated and enabled.
	 */
	struct edgetpu_external_mailbox *ext_mailbox;

	/* Mask of errors set for this group. */
	uint fatal_errors;

	/* List of DMA fences owned by this group */
	struct list_head dma_fence_list;

	/* end of fields protected by @lock */

	/*
	 * Used to synchronize any mapping operations for this device group.
	 * @lock must be held for reading or writing whenever @mapping_lock is held.
	 */
	struct mutex mapping_lock;

	/*
	 * Used to synchronize any VII command sending or response fetching for this device group.
	 * @lock must be held for reading or writing whenever @vii_lock is held.
	 */
	struct mutex vii_lock;

	/* Lists of `struct edgetpu_ikv_response`s for consuming/cleanup respectively */
	struct list_head ready_ikv_resps;
	struct list_head pending_ikv_resps;
	/*
	 * Protects access to @ready_ikv_resps, @pending_ikv_resps, and the "processed" field of any
	 * responses currently enqueued in @pending_ikv_resps.
	 */
	spinlock_t ikv_resp_lock;

	/* TPU IOVA mapped to host DRAM space */
	struct edgetpu_mapping_root host_mappings;
	/* TPU IOVA mapped to buffers backed by dma-buf */
	struct edgetpu_mapping_root dmabuf_mappings;
	struct edgetpu_events events;
	/* Mailbox attributes used to create this group */
	struct edgetpu_mailbox_attr mbox_attr;

	/* List of task_structs waiting on a dma_fence to send a command. */
	struct list_head pending_cmd_tasks;
	/* Indicates to threads not to modify pending_cmd_tasks anymore. */
	bool is_clearing_pending_commands;
	/* Protects `pending_cmd_tasks` and `is_clearing_pending_commands`. */
	spinlock_t pending_cmd_tasks_lock;
};

/*
 * Entry of edgetpu_dev#groups.
 *
 * Files other than edgetpu-device-group.c shouldn't need to access this
 * structure. Use macro etdev_for_each_group to access the groups under an
 * etdev.
 */
struct edgetpu_list_group {
	struct list_head list;
	struct edgetpu_device_group *grp;
};

/* Macro to loop through etdev->groups. */
#define etdev_for_each_group(etdev, l, g)                                      \
	for (l = list_entry(etdev->groups.next, typeof(*l), list), g = l->grp; \
	     &l->list != &etdev->groups;                                       \
	     l = list_entry(l->list.next, typeof(*l), list), g = l->grp)

/* Loop through group->clients (hold group->lock prior). */
#define for_each_list_group_client(c, group) \
	list_for_each_entry(c, &group->clients, list)

/*
 * Returns if the group is waiting to be finalized.
 *
 * Caller holds @group->lock.
 */
static inline bool
edgetpu_device_group_is_waiting(const struct edgetpu_device_group *group)
{
	return group->status == EDGETPU_DEVICE_GROUP_WAITING;
}

/*
 * Returns if the group is finalized.
 *
 * Caller holds @group->lock.
 */
static inline bool
edgetpu_device_group_is_finalized(const struct edgetpu_device_group *group)
{
	return group->status == EDGETPU_DEVICE_GROUP_FINALIZED;
}

/*
 * Returns if the group is errored.
 *
 * Caller holds @group->lock.
 */
static inline bool
edgetpu_device_group_is_errored(const struct edgetpu_device_group *group)
{
	return group->status == EDGETPU_DEVICE_GROUP_ERRORED;
}

/*
 * Returns if the group is disbanded.
 *
 * Caller holds @group->lock.
 */
static inline bool
edgetpu_device_group_is_disbanded(const struct edgetpu_device_group *group)
{
	return group->status == EDGETPU_DEVICE_GROUP_DISBANDED;
}

/*
 * Return fatal error status for the group.
 *
 * Caller holds @group->lock.
 */
static inline uint edgetpu_group_get_fatal_errors_locked(struct edgetpu_device_group *group)
{
	return group->fatal_errors;
}

/*
 * Returns -ECANCELED if the status of group is ERRORED, otherwise returns -EINVAL.
 *
 * Caller holds @group->lock.
 */
static inline int edgetpu_group_errno(struct edgetpu_device_group *group)
{
	if (edgetpu_device_group_is_errored(group)) {
		etdev_err(group->etdev, "group %u error status 0x%x\n", group->group_id,
			  edgetpu_group_get_fatal_errors_locked(group));
		return -ECANCELED;
	}
	return -EINVAL;
}

/* Increases ref_count of @group by one and returns @group. */
static inline struct edgetpu_device_group *
edgetpu_device_group_get(struct edgetpu_device_group *group)
{
	WARN_ON_ONCE(!refcount_inc_not_zero(&group->ref_count));
	return group;
}

/*
 * Decreases ref_count of @group by one.
 *
 * If @group->ref_count becomes 0, @group will be freed.
 */
void edgetpu_device_group_put(struct edgetpu_device_group *group);

/*
 * Creates a device group for @client.
 *
 * @client must not already have created a group.
 * @client->group will be set as the returned group on success.
 *
 * Call edgetpu_device_group_put() when the returned group is not needed.
 *
 * Returns a pointer to the new group, or a negative errno on error.
 * Returns -EINVAL if the client already created a group.
 */
struct edgetpu_device_group *
edgetpu_device_group_create(struct edgetpu_client *client, const struct edgetpu_mailbox_attr *attr);

/*
 * Disband the device group @client created.
 * The group will be marked as "disbanded".
 *
 * @client->group will be removed from @client->etdev->groups.
 * @client->group will be set as NULL.
 */
void edgetpu_device_group_disband(struct edgetpu_client *client);

/*
 * Finalizes the group.
 *
 * Returns 0 on success.
 */
int edgetpu_device_group_finalize(struct edgetpu_device_group *group);

/*
 * Maps buffer to a device group.
 *
 * @arg->device_address will be set as the mapped TPU VA on success.
 *
 * Returns zero on success or a negative errno on error.
 */
int edgetpu_device_group_map(struct edgetpu_device_group *group,
			     struct edgetpu_map_ioctl *arg);

/* Unmap a userspace buffer from a device group. */
int edgetpu_device_group_unmap(struct edgetpu_device_group *group,
			       tpu_addr_t tpu_addr,
			       edgetpu_map_flag_t flags);

/* Sync the buffer previously mapped by edgetpu_device_group_map. */
int edgetpu_device_group_sync_buffer(struct edgetpu_device_group *group,
				     const struct edgetpu_sync_ioctl *arg);

/* Clear all mappings for a device group. */
void edgetpu_mappings_clear_group(struct edgetpu_device_group *group);

/*
 * Return total size of all mappings for the group in bytes
 * @restrict32: only count mappings restricted to 32-bit CPU-accessible IOVA space.
 */
size_t edgetpu_group_mappings_total_size(struct edgetpu_device_group *group, bool restrict32);

/* Log mapping error with additional info for debugging. */
void edgetpu_device_group_log_map_error(struct edgetpu_device_group *group, size_t size,
					edgetpu_map_flag_t flags, int errorval);

/*
 * Return IOMMU domain for group mappings.
 *
 * Caller holds @group->lock to prevent race, the domain may be attached or detached to a PASID
 * by edgetpu_group_{detach/attach}_mailbox.
 */
static inline struct edgetpu_iommu_domain *
edgetpu_group_domain_locked(struct edgetpu_device_group *group)
{
	return group->etdomain;
}

/* dump mappings in @group */
void edgetpu_group_mappings_show(struct edgetpu_device_group *group,
				 struct seq_file *s);

/*
 * Sends a VII command on behalf of `group`.
 *
 * If @out_fence is not NULL, then the fence will be signaled when the response for this command
 * arrives. If the command is canceled or times out, then @out_fence will be errored.
 *
 * @release_callback will be called, with @release_data as an argument, immediately before the sent
 * command's response is released, regardless of whether any in-fences prevent the command from
 * being sent. If this function returns an error, @release_callback will NOT be called.
 *
 * Returns zero on success or a negative errno on error.
 */
int edgetpu_device_group_send_vii_command(struct edgetpu_device_group *group, void *cmd,
					  struct gcip_fence_array *in_fence_array,
					  struct gcip_fence_array *out_fence_array,
					  struct iif_fence *iif_dma_fence,
					  struct edgetpu_ikv_additional_info *additional_info,
					  void (*release_callback)(void *), void *release_data);

/*
 * Pops the oldest received VII response sent to `group`, and copies it to `resp`
 *
 * Returns zero, with the response copied into `resp` on success, or a negative errno on error.
 */
int edgetpu_device_group_get_vii_response(struct edgetpu_device_group *group, void *resp);

/*
 * Maps the VII mailbox CSR.
 *
 * Returns 0 on success.
 */
int edgetpu_mmap_csr(struct edgetpu_device_group *group,
		     struct vm_area_struct *vma, bool is_external);
/*
 * Maps the cmd/resp queue memory.
 *
 * Returns 0 on success.
 */
int edgetpu_mmap_queue(struct edgetpu_device_group *group, enum gcip_mailbox_queue_type type,
		       struct vm_area_struct *vma, bool is_external);

/* Set group eventfd for event notification */
int edgetpu_group_set_eventfd(struct edgetpu_device_group *group, uint event_id,
			      int eventfd);

/* Unset previously-set group eventfd */
void edgetpu_group_unset_eventfd(struct edgetpu_device_group *group,
				 uint event_id);

/* Notify group of event */
void edgetpu_group_notify(struct edgetpu_device_group *group, uint event_id);

/* Is device in any group (and may be actively processing requests) */
bool edgetpu_in_any_group(struct edgetpu_dev *etdev);

/*
 * Enable or disable device group join lockout (as during f/w load).
 * Returns false if attempting to lockout group join but device is already
 * joined to a group.
 */
bool edgetpu_set_group_join_lockout(struct edgetpu_dev *etdev, bool lockout);

/* Notify @group about a fatal error for that group. */
void edgetpu_group_fatal_error_notify(struct edgetpu_device_group *group,
				      uint error_mask);
/* Notify all device groups of @etdev about a failure on the die */
void edgetpu_fatal_error_notify(struct edgetpu_dev *etdev, uint error_mask);

/* Return fatal error signaled bitmask for device group */
uint edgetpu_group_get_fatal_errors(struct edgetpu_device_group *group);

/*
 * Detach and release the mailbox resources of VII from @group.
 * Some group operations would be disabled when a group has no mailbox attached.
 *
 * Caller holds @group->lock.
 */
void edgetpu_group_detach_mailbox_locked(struct edgetpu_device_group *group);
/*
 * Before detaching the mailbox, send CLOSE_DEVICE KCI that claims the mailbox
 * is going to be unused.
 *
 * The KCI command is sent even when @group is configured as mailbox
 * non-detachable.
 */
void edgetpu_group_close_and_detach_mailbox(struct edgetpu_device_group *group);
/*
 * Request and attach the mailbox resources of VII to @group.
 *
 * Return 0 on success.
 *
 * Caller holds @group->lock.
 */
int edgetpu_group_attach_mailbox_locked(struct edgetpu_device_group *group);
/*
 * After (successfully) attaching the mailbox, send OPEN_DEVICE KCI.
 *
 * The KCI command is sent even when @group is configured as mailbox
 * non-detachable (because the mailbox was successfully "attached").
 */
int edgetpu_group_attach_and_open_mailbox(struct edgetpu_device_group *group);

/*
 * Checks whether @group has mailbox detached.
 *
 * Caller holds @group->lock.
 */
static inline bool
edgetpu_group_mailbox_detached_locked(const struct edgetpu_device_group *group)
{
	return !group->mailbox_attached;
}

/*
 * Returns whether @group is finalized and has mailbox attached.
 *
 * Caller holds @group->lock.
 */
static inline bool
edgetpu_group_finalized_and_attached(const struct edgetpu_device_group *group)
{
	return edgetpu_device_group_is_finalized(group) &&
	       !edgetpu_group_mailbox_detached_locked(group);
}

/*
 * Add @task to @group's list of tasks that are blocking on a fence, running on @group's behalf.
 * @group can then cancel @task if @group shuts down before @task completes.
 *
 * Any task added this way must call `edgetpu_device_group_untrack_fence_task()` after any
 * potentially blocking work has completed but before the thread exits.
 *
 * If this function returns an error, the task must not be run.
 *
 * Returns 0 on success or a negative errno on error.
 */
int edgetpu_device_group_track_fence_task(struct edgetpu_device_group *group,
					  struct task_struct *task);

/*
 * Remove @task, previously added with `edgetpu_device_group_track_fence_task()`, from @group's
 * list of running tasks, so @group does not try to stop @task when @group releases.
 *
 * This function must be called inside the task itself, once it has completed all blocking work
 * and no longer needs to reference any of @group's state.
 */
void edgetpu_device_group_untrack_fence_task(struct edgetpu_device_group *group,
					     struct task_struct *task);

/*
 * Handle IOMMU fault: check client mappings for valid IOVA, log status.
 * Always returns a negative error code to tell caller to propceed with page fault error
 * processing for now.
 * TODO(b/264449079): Fault in page if appropriate, tell caller to retry TPU access.
 */
int edgetpu_device_group_handle_fault(struct edgetpu_dev *etdev, u64 iova, uint pasid);

#endif /* __EDGETPU_DEVICE_GROUP_H__ */
