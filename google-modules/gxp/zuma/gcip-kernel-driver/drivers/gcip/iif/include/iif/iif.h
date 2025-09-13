/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Defines the interface of the IIF driver.
 *
 * Copyright (C) 2023 Google LLC
 */

#ifndef __IIF_IIF_H__
#define __IIF_IIF_H__

#include <linux/ioctl.h>
#include <linux/types.h>

/* Interface Version. */
#define IIF_INTERFACE_VERSION_MAJOR 1
#define IIF_INTERFACE_VERSION_MINOR 3

#define IIF_IOCTL_BASE 'i'

/* The ioctl number for the fence FDs will start from here. */
#define IIF_FENCE_IOCTL_NUM_BASE 0x80

/* The maximum number of fences can be passed to one ioctl request. */
#define IIF_MAX_NUM_FENCES 64

/* The maximum timeout of reusable fences. */
#define IIF_FENCE_REUSABLE_MAX_TIMEOUT ULLONG_MAX

/*
 * If the count of a sync point is set to this, waiters will be notified every single fence from the
 * start timeline value.
 */
#define IIF_FENCE_SYNC_POINT_COUNT_ALL (ULLONG_MAX)

/*
 * By default, the fence will retire if there are no outstanding signalers, waiters and FDs. Its
 * purpose is to return the fence ID to the pool in early stage before the fence releases so that we
 * can minimize the possibility of the lack of fence ID. Therefore, the retirement condition will be
 * checked for each `signal()`, `waited()` and `close(fd)` call. However, when the user sets this
 * flag to a fence, the fence will retire only when it releases.
 *
 * If there is a possibility of a race condition between the signaler and waiters, this flag can be
 * considered to prevent the early fence retirement. For example, if the signaler signals a fence
 * before any waiter starts waiting on the fence, the IIF driver will think there are no outstanding
 * signalers and waiters, so it will let the fence retire. That means upcoming waiters may try to
 * wait on the retired fence which is invalid.
 *
 * Normally, that race condition doesn't have to be considered since the runtime will always hold
 * FDs while drivers/firmwares manipulating a fence and the fence won't retired in any case until
 * the runtime closes FDs. However, if there is an use case that utilizing a fence which won't be
 * exposed to the runtime (i.e., no FD is installed to the fence), this flag will be required to
 * prevent the race condition.
 */
#define IIF_FLAGS_RETIRE_ON_RELEASE (1u)

/*
 * Basically, IIF is supposed to be handled in the firmware-level if the signaler is non-AP.
 * However, the IIF kernel driver is still signaling the IIF kernel object so that the IP kernel
 * drivers can notice the fence unblock by registering poll callbacks or the runtime can wait for
 * the fence unblock by polling on the fence (i.e., epoll() syscall).
 *
 * If this flag is set, it will disable polling on the fence at the kernel-level so that the kernel
 * drivers or the runtime won't be notified even though they are polling on the fence. It is still
 * safe to utilize the fence functions below like normal cases, but just the fence will never notify
 * the ones polling on it.
 *
 * For example, it is still safe to call the `iif_fence_add_poll_callback()` function to register a
 * callback, but the callback will never be invoked. Also, the `iif_fence_is_signaled()` function
 * will still return true if the fence was signaled.
 *
 * Note that the `fence_unblocked()` operator registered to the `struct iif_manager` won't be
 * invoked for the fence with this flag.
 */
#define IIF_FLAGS_DISABLE_POLL (1u << 1)

/*
 * Make the fence as a direct IIF fence.
 *
 * This flag can be passed only when creating a fence. If the flag is set, the fence will be handled
 * as a direct mode (i.e., IP firmwares update the fence status directly and communicate with each
 * other directly to notify the fence signal.) instead of utilizing the underlying sync unit.
 *
 * This must not be used if one of signaler or waiter IPs is not supporting this.
 */
#define IIF_FLAGS_DIRECT (1u << 2)

/*
 * ioctls for /dev/iif.
 */

struct iif_create_fence_ioctl {
	/*
	 * Input:
	 * The type of the fence signaler IP. (See enum iif_ip_type)
	 */
	__u8 signaler_ip;
	/*
	 * Input:
	 * The number of the signalers.
	 */
	__u16 total_signalers;
	/*
	 * Output:
	 * The file descriptor of the created fence.
	 */
	__s32 fence;
};

/*
 * Create an IIF fence.
 * This ioctl creates a single-shot direct fence.
 */
#define IIF_CREATE_FENCE _IOWR(IIF_IOCTL_BASE, 0, struct iif_create_fence_ioctl)

/*
 * The ioctl won't register @eventfd and will simply return the number of
 * remaining signalers of each fence.
 */
#define IIF_FENCE_REMAINING_SIGNALERS_NO_REGISTER_EVENTFD (~0u)

struct iif_fence_remaining_signalers_ioctl {
	/*
	 * Input:
	 * User-space pointer to an int array of inter-IP fence file descriptors
	 * to check whether there are remaining signalers to be submitted or
	 * not.
	 */
	__u64 fences;
	/*
	 * Input:
	 * The number of fences in `fence_array`.
	 * If > IIF_MAX_NUM_FENCES, the ioctl will fail with errno == EINVAL.
	 */
	__u32 fences_count;
	/*
	 * Input:
	 * The eventfd which will be triggered if there were fence(s) which
	 * haven't finished the signaler submission yet when the ioctl is called
	 * and when they eventually have finished the submission. Note that if
	 * all fences already finished the submission (i.e., all values in the
	 * returned @remaining_signalers are 0), this eventfd will be ignored.
	 *
	 * Note that if `IIF_FENCE_REMAINING_SIGNALERS_NO_REGISTER_EVENTFD` is
	 * passed, this ioctl will simply return the number of remaining
	 * signalers of each fence to @remaining_signalers.
	 */
	__u32 eventfd;
	/*
	 * Output:
	 * User-space pointer to an int array where the driver will write the
	 * number of remaining signalers to be submitted per fence. The order
	 * will be the same with @fences.
	 */
	__u64 remaining_signalers;
};

/*
 * Check whether there are remaining signalers to be submitted to fences.
 * If all signalers have been submitted, the runtime is expected to send waiter
 * commands right away. Otherwise, it will listen the eventfd to wait signaler
 * submission to be finished.
 */
#define IIF_FENCE_REMAINING_SIGNALERS \
	_IOWR(IIF_IOCTL_BASE, 1, struct iif_fence_remaining_signalers_ioctl)

struct iif_create_fence_with_params_ioctl {
	/*
	 * Input:
	 * The type of signaler. (See enum iif_fence_signaler_type)
	 */
	__u8 signaler_type;
	/*
	 * Input:
	 * The type of fence. (See enum iif_fence_type)
	 */
	__u8 fence_type;
	/*
	 * Input:
	 * The type of the fence signaler IP. (See enum iif_ip_type)
	 * It will be used only if the signaler type is IP.
	 */
	__u8 signaler_ip;
	/*
	 * Input:
	 * The number of the signalers.
	 * It will be used only if the signaler type is IP. In case of a reusable fence, even if a
	 * signaler command can signal the fence multiple times, it must be counted as 1 signaler.
	 */
	__u16 remaining_signalers;
	/*
	 * Input:
	 * The bitwise value where each bit represents an IP. (See enum iif_ip_type)
	 */
	__u16 waiters;
	/*
	 * Input:
	 * If the signaler is an IP and the fence is a reusable fence, a timeout error will be
	 * propagated to the waiters if the timeline value of the fence reaches it.
	 *
	 * Can pass `IIF_FENCE_REUSABLE_MAX_TIMEOUT` to not have timeout.
	 */
	__u64 timeout;
	/*
	 * Input:
	 * The bitwase value where each bit represents a property of the fence. (See IIF_FLAGS_*)
	 */
	__u32 flags;
	/*
	 * Output:
	 * The file descriptor of the created fence.
	 */
	__s32 fence;
	/* Reserved. */
	__u8 reserved[9];
};

/* Create an IIF fence with parameters. */
#define IIF_CREATE_FENCE_WITH_PARAMS \
	_IOWR(IIF_IOCTL_BASE, 2, struct iif_create_fence_with_params_ioctl)

#define IIF_INTERFACE_VERSION_BUILD_BUFFER_SIZE 64

struct iif_interface_version_ioctl {
	/*
	 * Driver major version number.
	 *
	 * Increments whenever a non-backwards compatible change to the
	 * interface defined in this file changes.
	 */
	__u16 version_major;
	/*
	 * Driver minor version number.
	 *
	 * Increments whenever a backwards compatible change, such as the
	 * addition of a new IOCTL, is made to the interface defined in this
	 * file.
	 */
	__u16 version_minor;
	/*
	 * Driver build identifier.
	 *
	 * NULL-terminated string of the git hash of the commit the driver was
	 * built from. If the driver had uncommitted changes the string will
	 * end with "-dirty".
	 */
	char version_build[IIF_INTERFACE_VERSION_BUILD_BUFFER_SIZE];
};

/* Query the driver's interface version. */
#define IIF_GET_INTERFACE_VERSION _IOR(IIF_IOCTL_BASE, 3, struct iif_interface_version_ioctl)

/*
 * ioctls for inter-IP fence FDs.
 */

struct iif_fence_get_information_ioctl {
	/* The type of the signaler IP. (enum iif_ip_type) */
	__u8 signaler_ip;
	/* The number of total signalers. */
	__u16 total_signalers;
	/* The number of submitted signalers. */
	__u16 submitted_signalers;
	/* The number of signaled signalers. */
	__u16 signaled_signalers;
	/* The number of outstanding waiters. */
	__u16 outstanding_waiters;
	/*
	 * The signal status of fence.
	 * - 0: The fence hasn't been unblocked yet.
	 * - 1: The fence has been unblocked without any error.
	 * - Negative errno: The fence has been unblocked with an error.
	 */
	__s16 signal_status;
	/* Reserved. */
	__u8 reserved[5];
};

/*
 * Returns the fence information.
 *
 * This ioctl only works with direct single-shot fences. This interface will be
 * deprecated and please use the `IIF_FENCE_GET_INFORMATION_WITH_DETAILS` ioctl
 * instead.
 */
#define IIF_FENCE_GET_INFORMATION \
	_IOR(IIF_IOCTL_BASE, IIF_FENCE_IOCTL_NUM_BASE, struct iif_fence_get_information_ioctl)

/*
 * Submits a signaler to the fence.
 *
 * This ioctl is available only when the fence signaler is AP.
 *
 * The runtime should call this ioctl for every signaler command before they
 * start processing it. After that, when the command needs to signal the fence,
 * it can call the `IIF_FENCE_SIGNAL` ioctl. If the fence is a single-shot
 * fence, the signal ioctl should be called once per command. However, if the
 * fence is a reusable fence, the signal ioctl can be called multiple times per
 * command. Once the command has been completed, the runtime should call the
 * `IIF_FENCE_SIGNALER_COMPLETED` ioctl to notify the IIF driver that the
 * command has been completed.
 *
 * (See `IIF_FENCE_SIGNAL` and `IIF_FENCE_SIGNALER_COMPLETED` ioctls below.)
 *
 * I.e.,
 * For single-shot fence:
 *   ...
 *   ioctl(fence_fd, IIF_FENCE_SUBMIT_SIGNALER);
 *   process(signaler_command_0);
 *   ioctl(fence_fd, IIF_FENCE_SIGNAL);
 *   ioctl(fence_fd, IIF_FENCE_SIGNALER_COMPLETED);
 *   ...
 *   ioctl(fence_fd, IIF_FENCE_SUBMIT_SIGNALER);
 *   process(signaler_command_1);
 *   ioctl(fence_fd, IIF_FENCE_SIGNAL);
 *   ioctl(fence_fd, IIF_FENCE_SIGNALER_COMPLETED);
 *   ...
 *
 * For reusable fence:
 *   ...
 *   ioctl(fence_fd, IIF_FENCE_SUBMIT_SIGNALER);
 *   ioctl(fence_fd, IIF_FENCE_SUBMIT_SIGNALER);
 *   ...
 *   process(signaler_command_0);
 *   ioctl(fence_fd, IIF_FENCE_SIGNAL);
 *   ...
 *   process(signaler_command_1);
 *   ioctl(fence_fd, IIF_FENCE_SIGNAL);
 *   ...
 *   process(signaler_command_0);
 *   ioctl(fence_fd, IIF_FENCE_SIGNAL);
 *   ...
 *   process(signaler_command_1);
 *   ioctl(fence_fd, IIF_FENCE_SIGNAL);
 *   ...
 *   ioctl(fence_fd, IIF_FENCE_SIGNALER_COMPLETED);
 *   ioctl(fence_fd, IIF_FENCE_SIGNALER_COMPLETED);
 *   ...
 *
 * Return value:
 *   0      - Succeeded in signaling the fence.
 *   -EPERM - The signaler type of the fence is not AP or already all signalers
 *            have been submitted to the fence.
 */
#define IIF_FENCE_SUBMIT_SIGNALER _IO(IIF_IOCTL_BASE, IIF_FENCE_IOCTL_NUM_BASE + 1)

struct iif_fence_signal_ioctl {
	/*
	 * Input:
	 * An error code to indicate that a signaler command has been processed
	 * normally or with an error.
	 *
	 * If AP has failed in processing a signaler command of the fence, they
	 * should pass an errno to here so that the IPs waiting on the fence
	 * can notice that they may not able to proceed their waiter commands.
	 *
	 * If there was no error, it must be set to 0.
	 */
	__s32 error;
	/*
	 * Output:
	 * The number of remaining signals to unblock the fence. If it is 0,
	 * it means that the fence has been unblocked and the IPs waiting on the
	 * fence have been notified.
	 *
	 * This field is meaningful only if the fence is a single-shot fence.
	 */
	__u16 remaining_signals;
};

/*
 * Signals the fence.
 *
 * This ioctl is available only when the fence signaler is AP.
 *
 * The runtime should call this ioctl for every signaler command after they
 * have finished processing it.
 *
 * Return value:
 *   0      - Succeeded in signaling the fence.
 *   -EBUSY - The fence is already unblocked.
 *   -EPERM - The signaler type of the fence is not AP and signaling it by the
 *            runtime is not allowed.
 */
#define IIF_FENCE_SIGNAL \
	_IOWR(IIF_IOCTL_BASE, IIF_FENCE_IOCTL_NUM_BASE + 2, struct iif_fence_signal_ioctl)

struct iif_fence_set_flags_ioctl {
	/*
	 * Input:
	 * The bitmask to update the fence flags.
	 */
	__u32 flags;
	/*
	 * Input:
	 * If 0, the bitmask passed to @flag will be set to @fence. Otherwise,
	 * the bitmask will be cleared from @fence.
	 */
	__u8 clear;
};

/*
 * Sets bitwise flags to the fence.
 *
 * See IIF_FLAGS_* defines to get the meaning of each bit.
 *
 * Returns 0 on success.
 */
#define IIF_FENCE_SET_FLAGS \
	_IOW(IIF_IOCTL_BASE, IIF_FENCE_IOCTL_NUM_BASE + 3, struct iif_fence_set_flags_ioctl)

/*
 * Gets bitwise flags from the fence.
 *
 * See IIF_FLAGS_* defines to get the meaning of each bit.
 *
 * Returns 0 on success.
 */
#define IIF_FENCE_GET_FLAGS _IOR(IIF_IOCTL_BASE, IIF_FENCE_IOCTL_NUM_BASE + 4, __u32)

/*
 * Creates a DMA fence which will be signaled when the IIF is signaled.
 *
 * The file descriptor of the created DMA fence will be set to the passed user pointer.
 *
 * Returns 0 on success.
 */
#define IIF_FENCE_BRIDGE_DMA_FENCE _IOR(IIF_IOCTL_BASE, IIF_FENCE_IOCTL_NUM_BASE + 5, __s32)

/*
 * Notifies the IIF driver that a signaler command has been completed.
 *
 * This ioctl is available only when the fence signaler is AP.
 *
 * The runtime should call this ioctl for every signaler command after they
 * finished processing it.
 *
 * Note that this ioctl will be NO-OP if the fence is a single-shot direct fence
 * and calling `IIF_FENCE_SIGNAL` ioctl will imply this ioctl is called for the
 * backward compatibility. Therefore, it is not mandatory to call it if the
 * fence is a single-shot direct fence, but it is recommended to call it always
 * to keep the compatibility with all kind of fences.
 *
 * Returns 0 on success.
 */
#define IIF_FENCE_SIGNALER_COMPLETED _IO(IIF_IOCTL_BASE, IIF_FENCE_IOCTL_NUM_BASE + 6)

struct iif_fence_add_sync_point_ioctl {
	/*
	 * Input:
	 * The timeline value that the fence will start to notify waiters.
	 *
	 * It must be bigger than 0.
	 */
	__u64 timeline;
	/*
	 * Input:
	 * Waiters will be notified for @count times from @timeline value. If
	 * the user passes `IIF_FENCE_SYNC_POINT_COUNT_ALL`, waiters will be
	 * notified for every single signal from @timeline.
	 *
	 * It must be bigger than 0.
	 */
	__u64 count;
};

/*
 * Adds a sync point to notify waiters of the fence.
 */
#define IIF_FENCE_ADD_SYNC_POINT \
	_IOW(IIF_IOCTL_BASE, IIF_FENCE_IOCTL_NUM_BASE + 7, struct iif_fence_add_sync_point_ioctl)

struct iif_fence_get_information_with_details_ioctl {
	/*
	 * The type of signaler. (See enum iif_fence_signaler_type)
	 */
	__u8 signaler_type;
	/*
	 * The type of fence. (See enum iif_fence_type)
	 */
	__u8 fence_type;
	/*
	 * The type of the fence signaler IP. (See enum iif_ip_type)
	 *
	 * It is meaningful only if the signaler type is IP.
	 */
	__u8 signaler_ip;
	/*
	 * The number of the signalers.
	 *
	 * It is meaningful only if the signaler type is IP. In case of a
	 * reusable fence, even if a signaler command can signal the fence
	 * multiple times, it must be counted as 1 signaler.
	 */
	__u16 total_signalers;
	/*
	 * The bitwise value where each bit represents an IP.
	 * (See enum iif_ip_type)
	 */
	__u16 waiters;
	/*
	 * If the signaler is an IP and the fence is a reusable fence, a timeout
	 * error will be propagated to the waiters if the timeline value of the
	 * fence reaches it.
	 *
	 * It will be `IIF_FENCE_REUSABLE_MAX_TIMEOUT` if the fence doesn't have
	 * timeout.
	 */
	__u64 timeout;
	/*
	 * The bitwase value where each bit represents a property of the fence.
	 * (See IIF_FLAGS_*)
	 */
	__u32 flags;
	/*
	 * The number of submitted signalers.
	 *
	 * This is the number of signalers which called `submit_signaler()`.
	 */
	__u16 submitted_signalers;
	/*
	 * The number of signaled signalers.
	 *
	 * This is the number of signalers which called `signaler_completed()`.
	 *
	 * In case of direct single-shot fence, this is the number of signalers
	 * which called `signal()`.
	 */
	__u16 signaled_signalers;
	/*
	 * The number of outstanding waiters.
	 *
	 * The number of waiters which are waiting for the fence to be
	 * unblocked (single-shot) or to reach a sync-point they are waiting for
	 * (reusable).
	 */
	__u16 outstanding_waiters;
	union {
		/*
		 * True if the fence is unblocked. (For single-shot fences)
		 */
		__u8 signaled;
		/*
		 * The fence timeline. (For reusable fences)
		 *
		 * Note that this value will be updated only when the fence
		 * reaches any registered sync-point and the underlying
		 * sync-unit notifies the IIF driver. (i.e., it won't be updated
		 * for every single signal)
		 *
		 * The runtime should check this value whenever `poll()` syscall
		 * is notified to see whether the fence actually reached the
		 * timeline they are waiting for.
		 */
		__u64 timeline;
	};
	/*
	 * The fence error.
	 *
	 * It will be set when the fence has been signaled with an error.
	 *
	 * For single-shot fences, even though this field is set, @signaled can
	 * be false. In that case, the runtime shouldn't consider the fence as
	 * unblocked. It is dependenet on the implementation details of the
	 * underlying sync-unit.
	 *
	 * For reusable fences, if this field is set, the waiters should
	 * consider the fence has been errored out and can stop waiting on the
	 * fence even if @timeline hasn't reached the value they are waiting
	 * for. The `poll()` syscall will be notified immediately if the fence
	 * is errored out.
	 */
	__s16 error;
	/* Reserved. */
	__u8 reserved[29];
};

/*
 * Returns the fence information.
 *
 * Returns 0 on success.
 */
#define IIF_FENCE_GET_INFORMATION_WITH_DETAILS             \
	_IOR(IIF_IOCTL_BASE, IIF_FENCE_IOCTL_NUM_BASE + 8, \
	     struct iif_fence_get_information_with_details_ioctl)

#endif /* __IIF_IIF_H__ */
