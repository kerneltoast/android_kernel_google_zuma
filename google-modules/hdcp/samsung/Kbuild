# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y       += -I$(srctree)/include/
ccflags-y       += -I$(KERNEL_SRC)/../private/google-modules/trusty/include

exynos-hdcp2-y += auth-control.o
exynos-hdcp2-y += auth-state.o
exynos-hdcp2-y += auth13.o
exynos-hdcp2-y += auth22.o
exynos-hdcp2-y += auth22-ake.o
exynos-hdcp2-y += auth22-lc.o
exynos-hdcp2-y += auth22-repeater.o
exynos-hdcp2-y += auth22-ske.o
exynos-hdcp2-y += auth22-stream.o
exynos-hdcp2-y += dpcd.o
exynos-hdcp2-y += main.o
exynos-hdcp2-y += selftest.o
exynos-hdcp2-y += teeif.o

obj-$(CONFIG_EXYNOS_HDCP2) += exynos-hdcp2.o
