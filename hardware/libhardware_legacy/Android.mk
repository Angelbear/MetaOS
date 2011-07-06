# Copyright 2006 The Android Open Source Project

# Setting LOCAL_PATH will mess up all-subdir-makefiles, so do it beforehand.
SAVE_MAKEFILES := $(call all-subdir-makefiles)

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SHARED_LIBRARIES := libutils libbinder libcutils libwpa_client

LOCAL_C_INCLUDES := \
        vendor/marvell/generic/libMarvellWireless

ifneq ($(TARGET_SIMULATOR),true)
  LOCAL_CFLAGS  += -DQEMU_HARDWARE
  QEMU_HARDWARE := true
endif

ifneq ($(TARGET_SIMULATOR),true)
LOCAL_SHARED_LIBRARIES += libdl
endif

include $(SAVE_MAKEFILES)

# need "-lrt" on Linux simulator to pick up clock_gettime
ifeq ($(TARGET_SIMULATOR),true)
	ifeq ($(HOST_OS),linux)
		LOCAL_LDLIBS += -lrt -lpthread -ldl
	endif
endif

LOCAL_MODULE:= libhardware_legacy

include $(BUILD_SHARED_LIBRARY)

# static library for librpc
include $(CLEAR_VARS)

LOCAL_MODULE:= libpower

LOCAL_SRC_FILES += power/power.c

include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := wificonnect

LOCAL_SHARED_LIBRARIES := libhardware_legacy libcutils libnetutils

LOCAL_SRC_FILES += wificonnect.c

include $(BUILD_EXECUTABLE)
