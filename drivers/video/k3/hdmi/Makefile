obj-$(CONFIG_K3_HDMI_SII9134):= k3hdmi.o
k3hdmi-objs :=  k3_hdmi_hw.o k3_edid.o k3_hdcp.o k3_hdmi.o k3_hdmi_hw_dump.o

EXTRA_CFLAGS += -Idrivers/video/k3 -Idrivers/input/touchscreen/k3_ts_cts
EXTRA_CFLAGS += -Iarch/arm/mach-k3v2

ifeq ($(CONFIG_HUAWEI_MHL_SII9244),y)
EXTRA_CFLAGS += -DSUPPORT_MHL_SII9244
endif

ifeq ($(CONFIG_HDCP_ENABLE),y)
EXTRA_CFLAGS += -DUSE_HDCP=1
endif

ifeq ($(TARGET_PRODUCT),MediaPad)
EXTRA_CFLAGS += -DHDMI_MAX_VESA_TIMING_CODE=127
endif

ifeq ($(TARGET_BUILD_VARIANT),eng)
EXTRA_CFLAGS += -DENABLE_REG_DUMP=1
endif
